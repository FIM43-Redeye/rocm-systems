/*
 * Copyright © 2026 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including
 * the next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */


/* per-fd drm svm ioctl, should be moved to libdrm.
 * This implementation is only used to validate the DRM SVM functionality.
 */

#ifndef __AMDGPU_DRM_SVM_H__
#define __AMDGPU_DRM_SVM_H__

#include "drm.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define DRM_AMDGPU_GEM_SVM		0x1a
#define DRM_IOCTL_AMDGPU_GEM_SVM	DRM_IOWR(DRM_COMMAND_BASE + DRM_AMDGPU_GEM_SVM, struct drm_amdgpu_gem_svm)

/*
 * SVM (Shared Virtual Memory) IOCTL definitions
 */

/* SVM memory access flags */
#define AMDGPU_SVM_FLAG_HOST_ACCESS		0x00000001
#define AMDGPU_SVM_FLAG_COHERENT		0x00000002
#define AMDGPU_SVM_FLAG_HIVE_LOCAL		0x00000004
#define AMDGPU_SVM_FLAG_GPU_RO			0x00000008
#define AMDGPU_SVM_FLAG_GPU_EXEC		0x00000010
#define AMDGPU_SVM_FLAG_GPU_READ_MOSTLY		0x00000020
#define AMDGPU_SVM_FLAG_GPU_ALWAYS_MAPPED	0x00000040
#define AMDGPU_SVM_FLAG_EXT_COHERENT		0x00000080

/* SVM IOCTL operations */
#define AMDGPU_SVM_OP_SET_ATTR		0
#define AMDGPU_SVM_OP_GET_ATTR		1
#define AMDGPU_SVM_OP_UNMAP		2

/* SVM attribute types */
#define AMDGPU_SVM_ATTR_PREFERRED_LOC		0
#define AMDGPU_SVM_ATTR_PREFETCH_LOC		1
#define AMDGPU_SVM_ATTR_ACCESS			2
#define AMDGPU_SVM_ATTR_ACCESS_IN_PLACE		3
#define AMDGPU_SVM_ATTR_NO_ACCESS		4
#define AMDGPU_SVM_ATTR_SET_FLAGS		5
#define AMDGPU_SVM_ATTR_CLR_FLAGS		6
#define AMDGPU_SVM_ATTR_GRANULARITY		7

/* Special values for preferred/prefetch location */
#define AMDGPU_SVM_LOCATION_SYSMEM		0
#define AMDGPU_SVM_LOCATION_UNDEFINED		0xffffffff


/**
 * struct drm_amdgpu_svm_attribute - SVM range attribute
 *
 * @type: Attribute type (AMDGPU_SVM_ATTR_*)
 * @value: Attribute value (interpretation depends on type)
 */
struct drm_amdgpu_svm_attribute {
	__u32 type;
	__u32 value;
};

/**
 * struct drm_amdgpu_gem_svm - AMDGPU SVM IOCTL arguments
 *
 * @start_addr: Start address of the SVM range
 * @size: Size of the SVM range in bytes
 * @attrs_ptr: User pointer to array of struct drm_amdgpu_svm_attribute
 * @op: Operation (AMDGPU_SVM_OP_SET_ATTR or AMDGPU_SVM_OP_GET_ATTR)
 * @nattr: Number of attributes in the attrs_ptr array
 */
struct drm_amdgpu_gem_svm {
	__u64 start_addr;
	__u64 size;
	__u32 operation;
	__u32 nattr;
	__u64 attrs_ptr;
};
#if defined(__cplusplus)
}
#endif

#endif /* __AMDGPU_DRM_SVM_H__ */
