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

#ifndef LIBRARY_INCLUDE_SHMEM_H
#define LIBRARY_INCLUDE_SHMEM_H

#include <rocshmem/rocshmem.hpp>

const int SHMEM_SUCCESS = 1;

typedef enum {
  SHMEM_MTYPE_SYSTEM,
  SHMEM_MTYPE_CPU,
  SHMEM_MTYPE_GPU,
  // Need something for querying if the user requetss something the library does not suppport
  SHMEM_MTYPE_NULL,
} shmem_mem_type_t;

void shmem_init(void) {
  rocshmem_init();
}

void shmem_finalize(void) {
  rocshmem_finalize();
}

int shmem_my_pe(void) {
  return rocshmem_my_pe();
}

int shmem_num_pes(void) {
  return rocshmem_n_pes();
}

void *shmem_malloc(size_t size) {
  char *env_heap_mtype = getenv("SHMEM_DEFAULT_HEAP_MTYPE");

  // rocSHMEM defaults to GPU heap
  if ((NULL == env) || (0 == strcmp(env_heap_mtype, "GPU")) {
    return rocshmem_malloc(size);
  }

  // If enviroment variable is set to CPU then rocSHMEM returns NULL
  return NULL;
}

void *shmem_malloc_device(size_t size) {
  return rocshmem_malloc(size);
}

void *shmem_malloc_mtype(uint64_t mtype, size_t size) {
  if (mtype == (uint64_t) SHMEM_MTYPE_GPU) {
    return rocshmem_malloc(size);
  }
  return NULL;
}

void shmem_free(void *ptr) {
  return rocshmem_free(ptr);
}

int shmem_query_gpu_awareness(shmem_team *team) {
  return true;
}

// We need a corresponding API for GPU sentric
int shmem_query_gpu_centric(shmem_team *team) {
  return true;
}

// How about one query API?
struct shmem_library_info_t {
  bool gpu_aware;
  bool gpu_centric;
  enum shmem_mtypes default_heap_mtype;
  /* ect. */
};

int shmem_query_library(struct shmem_library_info_t *info) {
  char *env_heap_mtype;

  info->gpu_aware   = true;
  info->gpu_centric = true;

  env_heap_mtype = getenv("SHMEM_DEFAULT_HEAP_MTYPE");

  // rocSHMEM defaults to GPU heap
  if ((NULL == env) || (0 == strcmp(env_heap_mtype, "GPU")) {
    info->default_heap_mtype = SHMEM_MTYPE_GPU;
  } else {
    info->default_heap_mtype = SHMEM_MTYPE_NULL;
  }

  return SHMEM_SUCCESS;
}

#endif  // LIBRARY_INCLUDE_SHMEM_H
