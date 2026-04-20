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
 * Thread-safe. Creates the SDK PMC provider (which queries supported counters,
 * intersects with user settings, and configures the SDK profile internally),
 * then creates the collector and adds it to the active sampling loop.
 *
 * Called by tool_init() after creating the device_counting_service context.
 *
 * @param context_handle The rocprofiler context handle.
 * @param agent_handles Agent handle + device index pairs (one per GPU).
 */
void
register_gpu_perf_counter_source(
    uint64_t                                                            context_handle,
    const std::vector<device_providers::rocprofiler_sdk::agent_handle>& agent_handles);

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
