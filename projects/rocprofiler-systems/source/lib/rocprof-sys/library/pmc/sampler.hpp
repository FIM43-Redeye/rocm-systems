// Copyright (c) Advanced Micro Devices, Inc.
// SPDX-License-Identifier: MIT

#pragma once

#include "core/components/fwd.hpp"
#include "core/state.hpp"
#include "library/pmc/device_providers/rocprofiler_sdk/provider.hpp"

#include <cstdint>
#include <vector>

namespace rocprofsys
{
namespace pmc
{

std::atomic<State>&
get_state();

void
setup();

void
config();

void
sample();

void
shutdown();

void
post_process();

void set_state(State);

void
pause();

void
postfork_child_cleanup();

void
postfork_parent_reinit();

/**
 * @brief Register an SDK PMC device counting source.
 *
 * Thread-safe. Creates the SDK PMC provider and collector internally,
 * then adds it to the active sampling loop. Called by tool_init() when
 * the device_counting_service context is ready.
 *
 * @param context The rocprofiler context for device_counting_service.
 * @param agent_ids Agent handle values (one per GPU).
 * @param profile_configs Matching profile config handle values.
 * @param device_indices Matching logical device indices.
 * @param counter_names_per_agent Resolved counter names per agent (parallel to
 * agent_ids).
 * @param instance_infos_per_agent Pre-built instance_id→qualified_name mappings
 * per agent, built from v1 counter info dimension instances.
 * @param counter_meta_per_agent Per-counter metadata (block, expression,
 * is_constant, is_derived) extracted from rocprofiler_counter_info_v1_t.
 */
void
register_gpu_perf_counter_source(
    uint64_t context_handle, const std::vector<uint64_t>& agent_ids,
    const std::vector<uint64_t>&                 profile_configs,
    const std::vector<size_t>&                   device_indices,
    const std::vector<std::vector<std::string>>& counter_names_per_agent,
    const std::vector<
        std::vector<device_providers::rocprofiler_sdk::counter_instance_info>>&
        instance_infos_per_agent,
    const std::vector<std::vector<device_providers::rocprofiler_sdk::counter_metadata>>&
        counter_meta_per_agent);

}  // namespace pmc
}  // namespace rocprofsys

#if !defined(ROCPROFSYS_EXTERN_COMPONENTS) ||                                            \
    (defined(ROCPROFSYS_EXTERN_COMPONENTS) && ROCPROFSYS_EXTERN_COMPONENTS > 0)

#    include <timemory/components/base.hpp>
#    include <timemory/components/data_tracker/components.hpp>
#    include <timemory/operations.hpp>

ROCPROFSYS_DECLARE_EXTERN_COMPONENT(
    TIMEMORY_ESC(data_tracker<double, rocprofsys::component::backtrace_gpu_busy_gfx>),
    true, double)

ROCPROFSYS_DECLARE_EXTERN_COMPONENT(
    TIMEMORY_ESC(data_tracker<double, rocprofsys::component::backtrace_gpu_busy_umc>),
    true, double)

ROCPROFSYS_DECLARE_EXTERN_COMPONENT(
    TIMEMORY_ESC(data_tracker<double, rocprofsys::component::backtrace_gpu_busy_mm>),
    true, double)

ROCPROFSYS_DECLARE_EXTERN_COMPONENT(
    TIMEMORY_ESC(data_tracker<double, rocprofsys::component::backtrace_gpu_temp>), true,
    double)

ROCPROFSYS_DECLARE_EXTERN_COMPONENT(
    TIMEMORY_ESC(data_tracker<double, rocprofsys::component::backtrace_gpu_power>), true,
    double)

ROCPROFSYS_DECLARE_EXTERN_COMPONENT(
    TIMEMORY_ESC(data_tracker<double, rocprofsys::component::backtrace_gpu_memory>), true,
    double)

ROCPROFSYS_DECLARE_EXTERN_COMPONENT(
    TIMEMORY_ESC(data_tracker<double, rocprofsys::component::backtrace_gpu_vcn>), true,
    double)

ROCPROFSYS_DECLARE_EXTERN_COMPONENT(
    TIMEMORY_ESC(data_tracker<double, rocprofsys::component::backtrace_gpu_jpeg>), true,
    double)

#endif
