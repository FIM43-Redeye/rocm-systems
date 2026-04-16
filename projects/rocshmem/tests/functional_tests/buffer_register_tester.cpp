/******************************************************************************
 * Copyright (c) Advanced Micro Devices, Inc. All rights reserved.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *****************************************************************************/

#include "buffer_register_tester.hpp"

#include <rocshmem/rocshmem.hpp>

using namespace rocshmem;

/******************************************************************************
 * Device TEST CASES
 *****************************************************************************/
__global__ void UserBufferTest(int loop, int skip, long long int *start_time,
                               long long int *end_time, char *source,
                               char *dest, size_t size, TestType type,
                               ShmemContextType ctx_type, int wf_size) {
  __shared__ rocshmem_ctx_t ctx;
  int wg_id = get_flat_grid_id();
  int t_id  = get_flat_block_id();
  int wf_id = t_id / wf_size;
  rocshmem_wg_ctx_create(ctx_type, &ctx);

  /**
   * Shared array to capture the start time for each wavefront
   * Max threads per block = 1024, wavefront size = 64 or 32 depending
   * on the GPUs. Using 32 since its safer for the dimensioning of the array,
   * the last 16 elements will not be used on GPUs with a wf size of 64.
   * Maximum array size required = 1024/32 = 32
   */
  __shared__ long long int wf_start_time[32];

  /**
   * Calculate start index for each thread within the grid
   */
  size_t offset = size * get_flat_id();
  source += offset;
  dest += offset;

  for (int i = 0; i < loop + skip; i++) {
    if (i == skip) {
      __syncthreads();
      // Ensures all RMA calls from the skip loops are completed
      if(is_thread_zero_in_block()) {
        rocshmem_ctx_quiet(ctx);
      }
      __syncthreads();
      // Capture the start time of each wavefront to identify the earliest one
      wf_start_time[wf_id] = wall_clock64();
    }

    rocshmem_ctx_putmem(ctx, dest, source, size, 1);
  }

  __syncthreads();
  if(is_thread_zero_in_block()) {
    rocshmem_ctx_quiet(ctx);
  }

  /**
   * End time of the last wavefront is recorded by overwriting
   * the value previously set by earlier wavefronts.
   */
  end_time[wg_id] = wall_clock64();

  // Find the earliest start time
  int num_wfs = (get_flat_block_size() - 1 ) / wf_size + 1;
  for (int i = num_wfs / 2; i > 0; i >>= 1 ) {
    if(t_id < i) {
      wf_start_time[t_id] = min(wf_start_time[t_id], wf_start_time[t_id + i]);
    }
  }
  __syncthreads();

  if (t_id == 0) {
    start_time[wg_id] = wf_start_time[0];
  }

  rocshmem_wg_ctx_destroy(&ctx);
}

/******************************************************************************
 * HOST TEST CASES
 *****************************************************************************/

void register_nullptr() {

  int err = rocshmem_buffer_register(nullptr, 1024);

  if (ROCSHMEM_ERROR != err) {
    fprintf(stderr, "rocSHMEM should not be able to register a NULL pointer\n");
    exit(-1);
  }
}

void register_buffer_len_of_zero(void *user_buffer) {

  int err = rocshmem_buffer_register(user_buffer, 0);

  if (ROCSHMEM_ERROR != err) {
    fprintf(stderr, "rocSHMEM should not be able to register a buffer of length zero\n");
    exit(-1);
  }
}

void register_buffer_twice(void *user_buffer, size_t size) {

  int err = rocshmem_buffer_register(user_buffer, size);

  if (ROCSHMEM_ERROR == err) {
    fprintf(stderr, "rocSHMEM buffer registration error\n");
    exit(-1);
  }

  err = rocshmem_buffer_register(user_buffer, size);

  if (ROCSHMEM_ERROR != err) {
    fprintf(stderr, "rocSHMEM should not be able to register a buffer twice\n");
    exit(-1);
  }

  err = rocshmem_buffer_unregister(user_buffer);

  if (ROCSHMEM_ERROR == err) {
    fprintf(stderr, "rocSHMEM buffer dereegistration error\n");
    exit(-1);
  }
}

void deregister_buffer_twice(void *user_buffer, size_t size) {

  int err = rocshmem_buffer_register(user_buffer, size);

  if (ROCSHMEM_ERROR == err) {
    fprintf(stderr, "rocSHMEM buffer registration error\n");
    exit(-1);
  }

  err = rocshmem_buffer_unregister(user_buffer);

  if (ROCSHMEM_ERROR == err) {
    fprintf(stderr, "rocSHMEM buffer dereegistration error\n");
    exit(-1);
  }

  err = rocshmem_buffer_unregister(user_buffer);

  if (ROCSHMEM_ERROR != err) {
    fprintf(stderr, "rocSHMEM should not be able to deregister a buffer twice\n");
    exit(-1);
  }
}

void register_heap_memory(void *user_buffer, size_t size) {

  void *heap_buf = rocshmem_malloc(size);
  if (nullptr == heap_buf) {
    fprintf(stderr, "rocSHMEM memory allocation error\n");
    exit(-1);
  }

  int err = rocshmem_buffer_register(user_buffer, size);

  if (ROCSHMEM_ERROR != err) {
    rocshmem_free(heap_buf);
    fprintf(stderr, "rocSHMEM should not be able to register heap memory\n");
    exit(-1);
  }

  rocshmem_free(heap_buf);
}

void register_overlapping_regions(void *user_buffer, size_t size) {
  int err = rocshmem_buffer_register(user_buffer, size);

  if (ROCSHMEM_ERROR == err) {
    fprintf(stderr, "rocSHMEM memory allocation error\n");
    exit(-1);
  }

  void *user_buffer_offset = (void*)((uintptr_t) user_buffer + size);
  err = rocshmem_buffer_register(user_buffer_offset, size);

  if (ROCSHMEM_ERROR == err) {
    fprintf(stderr, "rocSHMEM should not be able to register overlapping user memory regions\n");
    exit(-1);
  }
}

/******************************************************************************
 * HOST TESTER CLASS METHODS
 *****************************************************************************/
BufferRegisterTester::BufferRegisterTester(TesterArguments args) : Tester(args) {
  size_t buff_size = max_msg_size * args.wg_size * args.num_wgs;

  CHECK_HIP(hipMalloc(&user_buffer, max((max_msg_size * 2), buff_size)));

  dest = (char *)rocshmem_malloc(buff_size);

  if (dest == nullptr) {
    std::cerr << "Error allocating memory from symmetric heap" << std::endl;
    std::cerr << " dest: " << dest << std::endl;
    rocshmem_global_exit(1);
  }

  for(size_t i = 0; i < buff_size; i++) {
    user_buffer[i] = static_cast<char>('a' + i % 26);
  }
}

BufferRegisterTester::~BufferRegisterTester() {
  CHECK_HIP(hipFree(user_buffer));

  rocshmem_free(dest);
}

void BufferRegisterTester::resetBuffers([[maybe_unused]] size_t size) {
  size_t buff_size = size * args.wg_size * args.num_wgs;
  memset(dest, '1', buff_size);
}

void BufferRegisterTester::launchKernel(dim3 gridSize, dim3 blockSize, int loop, size_t size) {

  // Host Test Cases
  register_nullptr();
  register_buffer_len_of_zero(user_buffer);

  register_buffer_twice(user_buffer, size);
  deregister_buffer_twice(user_buffer, size);

  register_heap_memory(user_buffer, size);

  register_overlapping_regions(user_buffer, size);


  // Device Test Cases
  size_t shared_bytes = 0;

  hipLaunchKernelGGL(UserBufferTest, gridSize, blockSize, shared_bytes, stream,
                     loop, args.skip, start_time, end_time, user_buffer, dest,
                     size, _type, _shmem_context, wf_size);

  num_msgs = (loop + args.skip) * gridSize.x * blockSize.x;
  num_timed_msgs = loop * gridSize.x * blockSize.x;
}

void BufferRegisterTester::verifyResults(size_t size) {
  if (args.myid == 1) {
    size_t buff_size = size * args.wg_size * args.num_wgs;
    size_t verify_wg_size = std::min((size_t) 1024, buff_size);
    size_t verify_num_wgs = buff_size / verify_wg_size;

    hipLaunchKernelGGL(verify_results_kernel_char, verify_num_wgs, verify_wg_size, 0, stream,
                       user_buffer, dest, buff_size, verification_error);
    CHECK_HIP(hipStreamSynchronize(stream));

    if (*verification_error) {
      for (uint64_t i = 0; i < buff_size; i++) {
        if (dest[i] != user_buffer[i]) {
          std::cerr << "Data validation error at idx " << i << std::endl;
          std::cerr << " Got " << dest[i] << ", Expected "
                    << user_buffer[i] << std::endl;
          exit(-1);
        }
      }
      *verification_error = false;
    }
  }
}
