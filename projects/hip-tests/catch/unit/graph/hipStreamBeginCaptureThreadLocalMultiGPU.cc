/*
 * Copyright (c) Advanced Micro Devices, Inc., or its affiliates.
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @addtogroup hipStreamBeginCapture hipStreamBeginCapture
 * @{
 * @ingroup GraphTest
 *
 * Thread-safety tests for hipStreamBeginCapture with hipStreamCaptureModeThreadLocal
 * on multi-GPU systems.
 *
 * Regression tests for ROCM-1945:
 *   hipStreamCaptureModeThreadLocal was not thread-safe across devices.  When multiple
 *   threads captured graphs simultaneously on different GPUs, kernel launches during
 *   capture touched the legacy/default stream (stream 0) and produced:
 *
 *     UpdateStreams failed for device id: X
 *
 *   Root cause: INVALIDATE_ALL_CAPTURING_AND_RETURN iterated g_allCapturingStreams
 *   (all threads, all modes) and blindly invalidated ThreadLocal captures belonging
 *   to other threads.
 */

#include <hip_test_common.hh>
#include <hip_test_kernels.hh>

#include <future>
#include <thread>

namespace {

static __global__ void addKernel(int* dst, int* src, int n) {
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i < n) dst[i] = dst[i] + src[i];
}

constexpr int kN = 256;

}  // anonymous namespace

// ================================================================================================
/**
 * Test: Thread A holds a ThreadLocal capture while Thread B completes its own capture
 * and calls hipGraphLaunch.  hipGraphLaunch internally touches the null stream and
 * triggers CHECK_STREAM_CAPTURING() -> INVALIDATE_ALL_CAPTURING_AND_RETURN.
 *
 * Regression for hip_graph_concurrent failure (ROCM-1945):
 *   Old code iterated g_allCapturingStreams and invalidated Thread A's ThreadLocal capture.
 *   Thread A's subsequent hipStreamEndCapture then failed, causing UpdateStreams to fail.
 *
 * Expected: Thread A's capture survives Thread B's graph launch and completes successfully.
 */
TEST_CASE("hipGraphLaunch_ThreadLocal_OtherCaptureNotInvalidated",
          "[graph][capture][multithreaded][multi_device]") {
  const int deviceCount = HipTest::getDeviceCount();
  if (deviceCount < 2) {
    HipTest::HIP_SKIP_TEST(
        "hipGraphLaunch_ThreadLocal_OtherCaptureNotInvalidated requires at least 2 GPUs — "
        "skipping.");
    return;
  }

  hipStream_t streamA = nullptr, streamB = nullptr;
  int* dA = nullptr;
  int* dB_src = nullptr;
  int* dB_dst = nullptr;
  hipGraph_t graphA = nullptr, graphB = nullptr;
  hipGraphExec_t execB = nullptr;
  hipError_t captureErrA = hipSuccess;
  hipError_t captureErrB = hipSuccess;
  hipError_t launchErrB = hipSuccess;

  HIP_CHECK(hipSetDevice(0));
  HIP_CHECK(hipStreamCreate(&streamA));
  HIP_CHECK(hipMalloc(&dA, kN * sizeof(int)));

  HIP_CHECK(hipSetDevice(1));
  HIP_CHECK(hipStreamCreate(&streamB));
  HIP_CHECK(hipMalloc(&dB_src, kN * sizeof(int)));
  HIP_CHECK(hipMalloc(&dB_dst, kN * sizeof(int)));
  HIP_CHECK(hipMemset(dB_src, 1, kN * sizeof(int)));
  HIP_CHECK(hipMemset(dB_dst, 0, kN * sizeof(int)));

  // Use promise/future pairs for C++17-compatible one-shot signalling.
  // captureStarted: A signals B that capture is active.
  // launchDone:     B signals A that graph launch is complete.
  std::promise<void> captureStarted, launchDone;
  auto captureReady = captureStarted.get_future();
  auto launchReady = launchDone.get_future();

  std::thread threadA([&]() {
    HIP_CHECK_THREAD(hipSetDevice(0));
    captureErrA = hipStreamBeginCapture(streamA, hipStreamCaptureModeThreadLocal);

    captureStarted.set_value();  // signal B: A's capture is active
    launchReady.wait();          // wait for B: graph launch done

    if (captureErrA == hipSuccess) {
      addKernel<<<(kN + 63) / 64, 64, 0, streamA>>>(dA, dA, kN);
      captureErrA = hipStreamEndCapture(streamA, &graphA);
    }
  });

  std::thread threadB([&]() {
    HIP_CHECK_THREAD(hipSetDevice(1));
    captureReady.wait();  // wait for A's capture to start

    captureErrB = hipStreamBeginCapture(streamB, hipStreamCaptureModeThreadLocal);
    if (captureErrB == hipSuccess) {
      addKernel<<<(kN + 63) / 64, 64, 0, streamB>>>(dB_dst, dB_src, kN);
      captureErrB = hipStreamEndCapture(streamB, &graphB);
    }
    if (captureErrB == hipSuccess) {
      captureErrB = hipGraphInstantiate(&execB, graphB, nullptr, nullptr, 0);
    }
    if (captureErrB == hipSuccess) {
      // This hipGraphLaunch internally triggers CHECK_STREAM_CAPTURING on the null stream.
      // It must NOT invalidate Thread A's ThreadLocal capture.
      launchErrB = hipGraphLaunch(execB, streamB);
      HIP_CHECK_THREAD(hipStreamSynchronize(streamB));
    }

    launchDone.set_value();  // signal A: graph launch complete
  });

  threadA.join();
  threadB.join();

  INFO("captureErrA=" << captureErrA << " captureErrB=" << captureErrB
                      << " launchErrB=" << launchErrB);
  REQUIRE(captureErrB == hipSuccess);
  REQUIRE(launchErrB == hipSuccess);
  REQUIRE(captureErrA == hipSuccess);

  HIP_CHECK(hipSetDevice(0));
  if (graphA) HIP_CHECK(hipGraphDestroy(graphA));
  HIP_CHECK(hipFree(dA));
  HIP_CHECK(hipStreamDestroy(streamA));

  HIP_CHECK(hipSetDevice(1));
  if (execB) HIP_CHECK(hipGraphExecDestroy(execB));
  if (graphB) HIP_CHECK(hipGraphDestroy(graphB));
  HIP_CHECK(hipFree(dB_src));
  HIP_CHECK(hipFree(dB_dst));
  HIP_CHECK(hipStreamDestroy(streamB));
}
