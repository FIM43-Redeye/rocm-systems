// MIT License
//
// Copyright (c) 2024-2025 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
// undefine NDEBUG so asserts are implemented
#ifdef NDEBUG
#    undef NDEBUG
#endif

#include <rocprofiler-sdk/cxx/codeobj/code_printing.hpp>
#include <rocprofiler-sdk/cxx/operators.hpp>

#include <rocprofiler-sdk/buffer.h>
#include <rocprofiler-sdk/callback_tracing.h>
#include <rocprofiler-sdk/experimental/thread_trace.h>
#include <rocprofiler-sdk/fwd.h>
#include <rocprofiler-sdk/registration.h>
#include <rocprofiler-sdk/rocprofiler.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

#define ROCPROFILER_CALL(result, msg)                                                              \
    if(auto ec = (result); ec != ROCPROFILER_STATUS_SUCCESS)                                       \
    {                                                                                              \
        std::cerr << "rocprofiler-sdk error at " << __FILE__ << ":" << __LINE__                    \
                  << " :: " << #result << std::endl;                                               \
        std::cerr << "rocprofiler-sdk error code " << ec << ": "                                   \
                  << rocprofiler_get_status_string(ec) << " :: " << msg << std::endl;              \
        abort();                                                                                   \
    }

#define CHECK_NOTNULL(x)                                                                           \
    if(!(x))                                                                                       \
    {                                                                                              \
        abort();                                                                                   \
    };

constexpr uint64_t TARGET_CU   = 1;           // CU (gfx9) or WGP (gfx10+)
constexpr uint64_t SHADER_MASK = 0x1;         // Only enable SE=0
constexpr uint64_t BUFFER_SIZE = 0x20000000;  // 512MB
constexpr uint64_t SIMD_MASK   = 0x7;         // Simd=={0,1,2}
constexpr int64_t POLLING_RATE = 36;

static_assert(((SIMD_MASK + 1) & SIMD_MASK) == 0 && "SIMD_MASK must be one less than POT");

using pcinfo_t = rocprofiler_thread_trace_decoder_pc_t;
using inst_t   = rocprofiler_thread_trace_decoder_inst_t;
using perf_t   = rocprofiler_thread_trace_decoder_perfevent_t;

namespace Results
{
struct Latency
{
    uint64_t latency{0};
    double matrix{0};
    uint64_t hitcount{0};
};

std::mutex mut;

// Maps address to latency
using LatencyTable = std::map<rocprofiler_thread_trace_decoder_pc_t, Latency>;
// Used to disassemble instructions at (id, vaddr) pair
using AddressTable = rocprofiler::sdk::codeobj::disassembly::CodeobjAddressTranslate;

AddressTable* table{nullptr};
LatencyTable* latencies{nullptr};

void
gen_output_stream()
{
    auto _lk = std::unique_lock{mut};

    CHECK_NOTNULL(table);
    CHECK_NOTNULL(latencies);

    const char*   OUTPUT_OFSTREAM = "thread_trace.csv";
    std::ofstream file(OUTPUT_OFSTREAM);

    if(!file.is_open())
        std::cout << "Could not open log file: " << OUTPUT_OFSTREAM << ", writing to stdout\n";
    else
        std::cout << "Writing log to: " << OUTPUT_OFSTREAM << std::endl;

    std::ostream& output = file.is_open() ? file : std::cout;

    // Sort map by instruction cost
    using Element = std::pair<pcinfo_t, Latency>;

    std::vector<Element> sorted(latencies->begin(), latencies->end());
    std::stable_sort(sorted.begin(), sorted.end(), [](const Element& a, const Element& b) {
        return a.second.latency > b.second.latency;
    });

    uint64_t total_raw_latency = 0;
    double total_matrix_latency = 0;
    for (auto& latency : sorted)
    {
        total_raw_latency += latency.second.latency;
        total_matrix_latency += latency.second.matrix;
    }

    output << "Addr, Instruction, Hitcount, \"Latency %\",\"MfmaIdle %\"\n";
    for(auto& [addr, latency] : sorted)
    {
        auto inst = table->get(addr.code_object_id, addr.address);

        auto   comment = inst->comment;
        size_t pos     = comment.rfind('/');
        if(pos != std::string::npos && pos + 1 < comment.size()) comment = comment.substr(pos + 1);

        output << std::hex << "0x" << addr.address << std::dec << ",\"" << inst->inst << "\"," << latency.hitcount << ","
               << int(10000.0*latency.latency/total_raw_latency + 0.5)*0.01f << ","
               << int(10000.0*latency.matrix/total_matrix_latency + 0.5)*0.01f << "\n";
    }
};
}  // namespace Results

namespace Decoder
{
rocprofiler_thread_trace_decoder_id_t decoder{};

struct events_cache_t
{
    std::vector<perf_t> perfevents{};
    std::array<std::vector<inst_t>, 4> insts{};
    std::mutex mut{};
};

void
shader_data_callback(rocprofiler_agent_id_t /* agent */,
                     int64_t /* se_id */,
                     void*  se_data,
                     size_t data_size,
                     rocprofiler_user_data_t /* userdata */)
{
    CHECK_NOTNULL(Results::latencies);

    events_cache_t cache{};

    auto parse = [](rocprofiler_thread_trace_decoder_record_type_t record_type_id,
                    void*                                          events,
                    uint64_t                                       num_events,
                    void*                                          userdata
) {
        auto& _cache = *static_cast<events_cache_t*>(userdata);
        auto  _lk    = std::unique_lock{_cache.mut};

        if(record_type_id == ROCPROFILER_THREAD_TRACE_DECODER_RECORD_PERFEVENT)
        {
            _cache.perfevents.reserve(_cache.perfevents.size() + num_events);
            for(size_t i = 0; i < num_events; i++)
            {
                auto& perf = static_cast<perf_t*>(events)[i];
                if (perf.CU == TARGET_CU) _cache.perfevents.push_back(perf);
            }
        }

        if(record_type_id != ROCPROFILER_THREAD_TRACE_DECODER_RECORD_WAVE) return;

        for(size_t w = 0; w < num_events; w++)
        {
            auto  lk   = std::unique_lock{Results::mut};
            auto& wave = static_cast<rocprofiler_thread_trace_decoder_wave_t*>(events)[w];
            auto& simd = _cache.insts.at(wave.simd);

            simd.reserve(simd.size() + wave.instructions_size);
            for(size_t i = 0; i < wave.instructions_size; i++)
            {
                auto& inst    = wave.instructions_array[i];
                auto& latency = (*Results::latencies)[inst.pc];
                latency.latency += inst.duration;
                latency.hitcount += 1;

                simd.push_back(inst);
            }
        }
    };

    ROCPROFILER_CALL(rocprofiler_trace_decode(decoder, parse, se_data, data_size, &cache), "Decode run");

    auto& perfevents = cache.perfevents;

    for (auto& simd :  cache.insts)
    {
        std::stable_sort(simd.begin(), simd.end(), [](const inst_t& a, const inst_t& b) {
            return a.time < b.time;
        });
    }
    std::stable_sort(perfevents.begin(), perfevents.end(), [](const perf_t& a, const perf_t& b) {
        return a.time < b.time;
    });

    for (size_t simd_id = 0; simd_id < 4; simd_id++)
    {
        auto& simd = cache.insts.at(simd_id);
        if (simd.size() && perfevents.size())
        {
            int64_t perf_iter      = 0;
            int64_t next_mfma_idle = 0;
            auto    lk             = std::unique_lock{Results::mut};

            for (auto& inst : simd)
            {
                while (perf_iter < perfevents.size() && perfevents.at(perf_iter).time <= inst.time) perf_iter++;

                int duration = inst.duration;
                auto iter2 = perf_iter;
                while (iter2 < perfevents.size() && perfevents.at(iter2).time < inst.time + inst.duration + 2*POLLING_RATE)
                {
                    auto& perf = perfevents.at(iter2);
                    int64_t overlap = std::min(inst.time + inst.duration - perf.time + POLLING_RATE, perf.time - inst.time);
                    if (overlap > 0) duration -= std::min<int>((&perf.events0)[simd_id], overlap);
                    iter2++;
                }

                if (duration > 0) Results::latencies->at(inst.pc).matrix += duration;
            }
        }
    }
}

}  // namespace Decoder

namespace ThreadTracer
{
rocprofiler_client_id_t* client_id   = nullptr;
rocprofiler_context_id_t tracing_ctx = {};

std::mutex mut;
std::unordered_set<uint64_t> target_kernels{};
std::vector<rocprofiler_agent_id_t> agent_list{};

void
tool_codeobj_tracing_callback(rocprofiler_callback_tracing_record_t record,
                              rocprofiler_user_data_t* /* user_data */,
                              void* /* userdata */)
{
    if(record.kind == ROCPROFILER_CALLBACK_TRACING_CODE_OBJECT &&
       record.operation == ROCPROFILER_CODE_OBJECT_LOAD &&
       record.phase == ROCPROFILER_CALLBACK_PHASE_LOAD)
    {
        CHECK_NOTNULL(Results::table);
        auto* data =
            static_cast<rocprofiler_callback_tracing_code_object_load_data_t*>(record.payload);

        if(data->storage_type == ROCPROFILER_CODE_OBJECT_STORAGE_TYPE_FILE)
        {
            Results::table->addDecoder(
                data->uri, data->code_object_id, data->load_delta, data->load_size);
            return;
        }

        auto* memorybase = reinterpret_cast<const void*>(data->memory_base);
        CHECK_NOTNULL(memorybase);

        ROCPROFILER_CALL(rocprofiler_thread_trace_decoder_codeobj_load(Decoder::decoder,
                                                                   data->code_object_id,
                                                                   data->load_delta,
                                                                   data->load_size,
                                                                   memorybase,
                                                                   data->memory_size), "load codeobj");

        Results::table->addDecoder(
            memorybase, data->memory_size, data->code_object_id, data->load_delta, data->load_size);
    }

    if(record.kind == ROCPROFILER_CALLBACK_TRACING_CODE_OBJECT &&
       record.operation == ROCPROFILER_CODE_OBJECT_DEVICE_KERNEL_SYMBOL_REGISTER &&
       record.phase == ROCPROFILER_CALLBACK_PHASE_LOAD)
    {
        using register_t = rocprofiler_callback_tracing_code_object_kernel_symbol_register_data_t;
        auto* sym_data = static_cast<register_t*>(record.payload);

        if (!sym_data->kernel_name) return;

        if (std::string_view(sym_data->kernel_name).find("Cijk") != std::string::npos)
        {
            auto _lk = std::unique_lock{mut};
            target_kernels.insert(sym_data->kernel_id);
        }
    }
}

rocprofiler_status_t
process_agent_counters(rocprofiler_agent_id_t    id,
           rocprofiler_counter_id_t* counters,
           size_t                    num_counters,
           void*                     userdata)
{
    for(size_t i = 0; i < num_counters; ++i)
    {
        auto _info = rocprofiler_counter_info_v1_t{};
        ROCPROFILER_CALL(rocprofiler_query_counter_info(
            counters[i], ROCPROFILER_COUNTER_INFO_VERSION_1, &_info), "query counter");

        if (!_info.name) continue;
        if (std::string_view(_info.name).find("SQ_VALU_MFMA_BUSY_CYCLES") == std::string::npos) continue;

        static_cast<rocprofiler_thread_trace_parameter_t*>(userdata)->counter_id = counters[i];
        return ROCPROFILER_STATUS_SUCCESS;
    }

    return ROCPROFILER_STATUS_ERROR;
}

rocprofiler_status_t
query_available_agents(rocprofiler_agent_version_t /* version */,
                       const void** agents,
                       size_t       num_agents,
                       void*        user_data)
{
    for(size_t idx = 0; idx < num_agents; idx++)
    {
        const auto* agent = static_cast<const rocprofiler_agent_v0_t*>(agents[idx]);
        if(agent->type != ROCPROFILER_AGENT_TYPE_GPU) continue;

        agent_list.push_back(agent->id);
    }
    return ROCPROFILER_STATUS_SUCCESS;
}


rocprofiler_thread_trace_control_flags_t
dispatch_callback(rocprofiler_agent_id_t /* agent_id  */,
                      rocprofiler_queue_id_t /* queue_id  */,
                      rocprofiler_async_correlation_id_t /* correlation_id */,
                      rocprofiler_kernel_id_t   kernel_id,
                      rocprofiler_dispatch_id_t dispatch_id,
                      void* /*userdata_config*/,
                      rocprofiler_user_data_t* userdata_shader)
{
    userdata_shader->value = dispatch_id;

    auto _lk = std::unique_lock{mut};
    if (target_kernels.find(kernel_id) == target_kernels.end())
        return ROCPROFILER_THREAD_TRACE_CONTROL_NONE;

    return ROCPROFILER_THREAD_TRACE_CONTROL_START_AND_STOP;
}

int
tool_init(rocprofiler_client_finalize_t /* fini_func */, void* /* tool_data */)
{
    Results::latencies = new Results::LatencyTable{};
    Results::table     = new Results::AddressTable{};

    ROCPROFILER_CALL(rocprofiler_thread_trace_decoder_create(&Decoder::decoder, "/opt/rocm/lib"), "Decoder create");

    ROCPROFILER_CALL(rocprofiler_create_context(&tracing_ctx), "context creation");

    ROCPROFILER_CALL(
        rocprofiler_configure_callback_tracing_service(tracing_ctx,
                                                       ROCPROFILER_CALLBACK_TRACING_CODE_OBJECT,
                                                       nullptr,
                                                       0,
                                                       ThreadTracer::tool_codeobj_tracing_callback,
                                                       nullptr),
        "code object tracing service configure");

    ROCPROFILER_CALL(rocprofiler_query_available_agents(ROCPROFILER_AGENT_INFO_VERSION_0,
                                                        &query_available_agents,
                                                        sizeof(rocprofiler_agent_t),
                                                        nullptr),
                     "Failed to find GPU agents");

    for (auto& agent : agent_list)
    {
        rocprofiler_thread_trace_parameter_t counter{};
        counter.type = ROCPROFILER_THREAD_TRACE_PARAMETER_PERFCOUNTER;

        ROCPROFILER_CALL(rocprofiler_iterate_agent_supported_counters(agent, process_agent_counters, &counter), "iterate counters");
        if (counter.counter_id.handle == 0) abort();

        auto parameters = std::vector<rocprofiler_thread_trace_parameter_t>{};
        parameters.push_back({ROCPROFILER_THREAD_TRACE_PARAMETER_TARGET_CU, {TARGET_CU}});
        parameters.push_back({ROCPROFILER_THREAD_TRACE_PARAMETER_SIMD_SELECT, {SIMD_MASK}});
        parameters.push_back({ROCPROFILER_THREAD_TRACE_PARAMETER_BUFFER_SIZE, {BUFFER_SIZE}});
        parameters.push_back(
            {ROCPROFILER_THREAD_TRACE_PARAMETER_SHADER_ENGINE_MASK, {SHADER_MASK}});
        parameters.push_back({ROCPROFILER_THREAD_TRACE_PARAMETER_PERFCOUNTERS_CTRL, {1}});
        parameters.push_back({ROCPROFILER_THREAD_TRACE_PARAMETER_PERFCOUNTER_EXCLUDE_MASK, {~(1ul<<TARGET_CU)}});

        for (int i=0; i<4; i++)
        {
            counter.simd_mask = 1 << i;
            if (SIMD_MASK & counter.simd_mask) parameters.push_back(counter);
        }

        ROCPROFILER_CALL(
            rocprofiler_configure_dispatch_thread_trace_service(tracing_ctx,
                                                              agent,
                                                              parameters.data(),
                                                              parameters.size(),
                                                              dispatch_callback,
                                                              Decoder::shader_data_callback,
                                                              nullptr),
            "thread trace service configure");
    }

    int valid_ctx = 0;
    ROCPROFILER_CALL(rocprofiler_context_is_valid(tracing_ctx, &valid_ctx), "validity check");
    assert(valid_ctx != 0);

    ROCPROFILER_CALL(rocprofiler_start_context(tracing_ctx), "context start");

    // no errors
    return 0;
}

void
tool_fini(void* /* tool_data */)
{
    rocprofiler_thread_trace_decoder_destroy(Decoder::decoder);

    Results::gen_output_stream();

    delete Results::latencies;
    delete Results::table;
}

}  // namespace ThreadTracer

extern "C" rocprofiler_tool_configure_result_t*
rocprofiler_configure(uint32_t /* version */,
                      const char* /* runtime_version */,
                      uint32_t                 priority,
                      rocprofiler_client_id_t* id)
{
    // only activate if main tool
    if(priority > 0) return nullptr;

    // set the client name
    id->name = "Thread Trace Sample";

    // store client info
    ThreadTracer::client_id = id;

    // create configure data
    static auto cfg =
        rocprofiler_tool_configure_result_t{sizeof(rocprofiler_tool_configure_result_t),
                                            &ThreadTracer::tool_init,
                                            &ThreadTracer::tool_fini,
                                            nullptr};

    // return pointer to configure data
    return &cfg;
}
