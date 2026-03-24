/*
 * Copyright © 2020 Advanced Micro Devices, Inc.
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
#include "libhsakmt.h"
#include <string.h>
#include <errno.h>
#include <alloca.h>
#include <fcntl.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <errno.h>

#ifdef USE_DRM_AMDGPU_SVM
#include "hsakmt/drm/amdgpu_svm.h"
#include "fmm.h"

static int svm_is_access_attr(HSAuint32 type)
{
	return type == HSA_SVM_ATTR_ACCESS ||
		type == HSA_SVM_ATTR_ACCESS_IN_PLACE ||
		type == HSA_SVM_ATTR_NO_ACCESS;
}

static int svm_is_location_attr(HSAuint32 type)
{
	return type == HSA_SVM_ATTR_PREFERRED_LOC ||
		type == HSA_SVM_ATTR_PREFETCH_LOC;
}

static HSAKMT_STATUS
hsaKmtSVMSetAttrCtx_drm(HsaKFDContext *ctx,
		 void *start_addr, HSAuint64 size,
		 unsigned int nattr,
		 HSA_SVM_ATTRIBUTE *attrs)
{
	struct drm_amdgpu_svm_attribute *drm_attrs;
	HSAuint64 s_attr;
	HSAKMT_STATUS r;
	HSAuint32 i;
	HsaAMDGPUDeviceHandle deviceHandle = 0;

	CHECK_KFD_OPEN();
	CHECK_KFD_MINOR_VERSION(5);

	pr_debug("%s: address 0x%p size 0x%lx\n", __func__, start_addr, size);

	if (!start_addr || !size)
		return HSAKMT_STATUS_INVALID_PARAMETER;
	if ((uint64_t)start_addr & (PAGE_SIZE - 1))
		return HSAKMT_STATUS_INVALID_PARAMETER;
	if (size & (PAGE_SIZE - 1))
		return HSAKMT_STATUS_INVALID_PARAMETER;
	if (nattr && !attrs)
		return HSAKMT_STATUS_INVALID_PARAMETER;

	s_attr = sizeof(*drm_attrs) * nattr;
	drm_attrs = alloca(s_attr);

	for (i = 0; i < nattr; i++) {
		drm_attrs[i].type = attrs[i].type;
		drm_attrs[i].value = attrs[i].value;

		if (!svm_is_location_attr(attrs[i].type) &&
		    !svm_is_access_attr(attrs[i].type))
			continue;

		if (attrs[i].type == HSA_SVM_ATTR_PREFERRED_LOC &&
		    attrs[i].value == INVALID_NODEID) {
			drm_attrs[i].value = AMDGPU_SVM_LOCATION_UNDEFINED;
			continue;
		}
		r = hsakmt_validate_nodeid(ctx, attrs[i].value, &drm_attrs[i].value);
		if (r != HSAKMT_STATUS_SUCCESS) {
			pr_debug("invalid node ID: %d\n", attrs[i].value);
			return r;
		} else if (svm_is_access_attr(attrs[i].type)) {
			if (!drm_attrs[i].value) {
				pr_debug("CPU node invalid for access attribute\n");
				return HSAKMT_STATUS_INVALID_NODE_UNIT;
			} else if (!deviceHandle) {
				// Get the device handle for the first valid GPU node
				r = hsaKmtGetAMDGPUDeviceHandleCtx(ctx, attrs[i].value, &deviceHandle);
				if (r != HSAKMT_STATUS_SUCCESS) {
					pr_debug("failed to get AMDGPU device handle for node ID: %d\n", attrs[i].value);
					return r;
				}
			}
		}
	}

	/* No access attr provided; fall back to the first GPU's device handle */
	if (!deviceHandle) {
		r = hsakmt_fmm_get_default_amdgpu_device_handle(ctx, &deviceHandle);
		if (r != HSAKMT_STATUS_SUCCESS) {
			pr_debug("failed to get default AMDGPU device handle\n");
			return r;
		}
	}

	if (amdgpu_svm_set_attr(deviceHandle, (uint64_t)start_addr, size, nattr, drm_attrs)) {
		pr_debug("op set range attrs failed %s\n", strerror(errno));
		return HSAKMT_STATUS_ERROR;
	}

	return HSAKMT_STATUS_SUCCESS;
}

static HSAKMT_STATUS
hsaKmtSVMGetAttrCtx_drm(HsaKFDContext *ctx,
		 void *start_addr, HSAuint64 size,
		 unsigned int nattr,
		 HSA_SVM_ATTRIBUTE *attrs)
{
	struct drm_amdgpu_svm_attribute *drm_attrs;
	HSAuint64 s_attr;
	HSAKMT_STATUS r;
	HSAuint32 i;
	HsaAMDGPUDeviceHandle deviceHandle = 0;

	CHECK_KFD_OPEN();
	CHECK_KFD_MINOR_VERSION(5);

	pr_debug("%s: address 0x%p size 0x%lx\n", __func__, start_addr, size);

	if (!start_addr || !size)
		return HSAKMT_STATUS_INVALID_PARAMETER;
	if ((uint64_t)start_addr & (PAGE_SIZE - 1))
		return HSAKMT_STATUS_INVALID_PARAMETER;
	if (size & (PAGE_SIZE - 1))
		return HSAKMT_STATUS_INVALID_PARAMETER;
	if (nattr && !attrs)
		return HSAKMT_STATUS_INVALID_PARAMETER;

	s_attr = sizeof(*drm_attrs) * nattr;
	drm_attrs = alloca(s_attr);

	if (nattr)
		memcpy(drm_attrs, attrs, s_attr);

	for (i = 0; i < nattr; i++) {
		if (!svm_is_access_attr(attrs[i].type))
		    continue;

		r = hsakmt_validate_nodeid(ctx, attrs[i].value, &drm_attrs[i].value);
		if (r != HSAKMT_STATUS_SUCCESS) {
			pr_debug("invalid node ID: %d\n", attrs[i].value);
			return r;
		} else if (!drm_attrs[i].value) {
			pr_debug("CPU node invalid for access attribute\n");
			return HSAKMT_STATUS_INVALID_NODE_UNIT;
		} else if (!deviceHandle) {
			// Get the device handle for the first valid GPU node
			r = hsaKmtGetAMDGPUDeviceHandleCtx(ctx, attrs[i].value, &deviceHandle);
			if (r != HSAKMT_STATUS_SUCCESS) {
				pr_debug("failed to get AMDGPU device handle for node ID: %d\n", attrs[i].value);
				return r;
			}
		}
	}

	/* No access attr provided; fall back to the first GPU's device handle */
	if (!deviceHandle) {
		r = hsakmt_fmm_get_default_amdgpu_device_handle(ctx, &deviceHandle);
		if (r != HSAKMT_STATUS_SUCCESS) {
			pr_debug("failed to get default AMDGPU device handle\n");
			return r;
		}
	}

	if (amdgpu_svm_get_attr(deviceHandle, (uint64_t)start_addr, size, nattr, drm_attrs)) {
		pr_debug("op get range attrs failed %s\n", strerror(errno));
		return HSAKMT_STATUS_ERROR;
	}

	memcpy(attrs, drm_attrs, s_attr);

	for (i = 0; i < nattr; i++) {
		if (!svm_is_location_attr(attrs[i].type) &&
		    !svm_is_access_attr(attrs[i].type))
			continue;

		switch (attrs[i].value) {
		case AMDGPU_SVM_LOCATION_SYSMEM:
			attrs[i].value = 0;
			break;
		case AMDGPU_SVM_LOCATION_UNDEFINED:
			attrs[i].value = INVALID_NODEID;
			break;
		default:
			r = hsakmt_gpuid_to_nodeid(ctx, attrs[i].value, &attrs[i].value);
			if (r != HSAKMT_STATUS_SUCCESS) {
				pr_debug("invalid GPU ID: %d\n",
					 attrs[i].value);
				return r;
			}
		}
	}

	return HSAKMT_STATUS_SUCCESS;
}
#endif

/* Helper functions for calling KFD SVM ioctl */

HSAKMT_STATUS HSAKMTAPI
hsaKmtSVMSetAttrCtx(HsaKFDContext *ctx,
		 void *start_addr, HSAuint64 size, unsigned int nattr,
		 HSA_SVM_ATTRIBUTE *attrs)
{
#ifdef USE_DRM_AMDGPU_SVM
	return hsaKmtSVMSetAttrCtx_drm(ctx, start_addr, size, nattr, attrs);
#else
	struct kfd_ioctl_svm_args *args;
	HSAuint64 s_attr;
	HSAKMT_STATUS r;
	HSAuint32 i;

	CHECK_KFD_OPEN();
	CHECK_KFD_MINOR_VERSION(5);

	pr_debug("%s: address 0x%p size 0x%lx\n", __func__, start_addr, size);

	if (!start_addr || !size)
		return HSAKMT_STATUS_INVALID_PARAMETER;
	if ((uint64_t)start_addr & (PAGE_SIZE - 1))
		return HSAKMT_STATUS_INVALID_PARAMETER;
	if (size & (PAGE_SIZE - 1))
		return HSAKMT_STATUS_INVALID_PARAMETER;

	s_attr = sizeof(*attrs) * nattr;
	args = alloca(sizeof(*args) + s_attr);

	args->start_addr = (uint64_t)start_addr;
	args->size = size;
	args->op = KFD_IOCTL_SVM_OP_SET_ATTR;
	args->nattr = nattr;
	memcpy(args->attrs, attrs, s_attr);

	for (i = 0; i < nattr; i++) {
		if (attrs[i].type != KFD_IOCTL_SVM_ATTR_PREFERRED_LOC &&
		    attrs[i].type != KFD_IOCTL_SVM_ATTR_PREFETCH_LOC &&
		    attrs[i].type != KFD_IOCTL_SVM_ATTR_ACCESS &&
		    attrs[i].type != KFD_IOCTL_SVM_ATTR_ACCESS_IN_PLACE &&
		    attrs[i].type != KFD_IOCTL_SVM_ATTR_NO_ACCESS)
		    continue;

		if (attrs[i].type == KFD_IOCTL_SVM_ATTR_PREFERRED_LOC &&
		    attrs[i].value == INVALID_NODEID) {
			args->attrs[i].value = KFD_IOCTL_SVM_LOCATION_UNDEFINED;
			continue;
		}
		//attrs[i].value is a node ID, the svm ioctl needs gpu ID
		r = hsakmt_validate_nodeid(ctx, attrs[i].value, &args->attrs[i].value);
		if (r != HSAKMT_STATUS_SUCCESS) {
			pr_debug("invalid node ID: %d\n", attrs[i].value);
			return r;
		} else if (!args->attrs[i].value &&
			   (attrs[i].type == KFD_IOCTL_SVM_ATTR_ACCESS ||
			    attrs[i].type == KFD_IOCTL_SVM_ATTR_ACCESS_IN_PLACE ||
			    attrs[i].type == KFD_IOCTL_SVM_ATTR_NO_ACCESS)) {
			pr_debug("CPU node invalid for access attribute\n");
			return HSAKMT_STATUS_INVALID_NODE_UNIT;
		}
	}

	/* Driver does one copy_from_user, with extra attrs size */
	r = hsakmt_ioctl(ctx->fd, AMDKFD_IOC_SVM + (s_attr << _IOC_SIZESHIFT), args);
	if (r) {
		pr_debug("op set range attrs failed %s\n", strerror(errno));
		return HSAKMT_STATUS_ERROR;
	}

	return HSAKMT_STATUS_SUCCESS;
#endif
}

HSAKMT_STATUS HSAKMTAPI
hsaKmtSVMGetAttrCtx(HsaKFDContext *ctx,
		 void *start_addr, HSAuint64 size, unsigned int nattr,
		 HSA_SVM_ATTRIBUTE *attrs)
{
#ifdef USE_DRM_AMDGPU_SVM
	return hsaKmtSVMGetAttrCtx_drm(ctx, start_addr, size, nattr, attrs);
#else
	struct kfd_ioctl_svm_args *args;
	HSAuint64 s_attr;
	HSAKMT_STATUS r;
	HSAuint32 i;

	CHECK_KFD_OPEN();
	CHECK_KFD_MINOR_VERSION(5);

	pr_debug("%s: address 0x%p size 0x%lx\n", __func__, start_addr, size);

	if (!start_addr || !size)
		return HSAKMT_STATUS_INVALID_PARAMETER;
	if ((uint64_t)start_addr & (PAGE_SIZE - 1))
		return HSAKMT_STATUS_INVALID_PARAMETER;
	if (size & (PAGE_SIZE - 1))
		return HSAKMT_STATUS_INVALID_PARAMETER;

	s_attr = sizeof(*attrs) * nattr;
	args = alloca(sizeof(*args) + s_attr);

	args->start_addr = (uint64_t)start_addr;
	args->size = size;
	args->op = KFD_IOCTL_SVM_OP_GET_ATTR;
	args->nattr = nattr;
	memcpy(args->attrs, attrs, s_attr);

	for (i = 0; i < nattr; i++) {
		if (attrs[i].type != KFD_IOCTL_SVM_ATTR_ACCESS &&
		    attrs[i].type != KFD_IOCTL_SVM_ATTR_ACCESS_IN_PLACE &&
		    attrs[i].type != KFD_IOCTL_SVM_ATTR_NO_ACCESS)
		    continue;

		r = hsakmt_validate_nodeid(ctx, attrs[i].value, &args->attrs[i].value);
		if (r != HSAKMT_STATUS_SUCCESS) {
			pr_debug("invalid node ID: %d\n", attrs[i].value);
			return r;
		} else if (!args->attrs[i].value) {
			pr_debug("CPU node invalid for access attribute\n");
			return HSAKMT_STATUS_INVALID_NODE_UNIT;
		}
	}

	/* Driver does one copy_from_user, with extra attrs size */
	r = hsakmt_ioctl(ctx->fd, AMDKFD_IOC_SVM + (s_attr << _IOC_SIZESHIFT), args);
	if (r) {
		pr_debug("op get range attrs failed %s\n", strerror(errno));
		return HSAKMT_STATUS_ERROR;
	}

	memcpy(attrs, args->attrs, s_attr);

	for (i = 0; i < nattr; i++) {
		if (attrs[i].type != KFD_IOCTL_SVM_ATTR_PREFERRED_LOC &&
		    attrs[i].type != KFD_IOCTL_SVM_ATTR_PREFETCH_LOC &&
		    attrs[i].type != KFD_IOCTL_SVM_ATTR_ACCESS &&
		    attrs[i].type != KFD_IOCTL_SVM_ATTR_ACCESS_IN_PLACE &&
		    attrs[i].type != KFD_IOCTL_SVM_ATTR_NO_ACCESS)
			continue;

		switch (attrs[i].value) {
		case KFD_IOCTL_SVM_LOCATION_SYSMEM:
			attrs[i].value = 0;
			break;
		case KFD_IOCTL_SVM_LOCATION_UNDEFINED:
			attrs[i].value = INVALID_NODEID;
			break;
		default:
			r = hsakmt_gpuid_to_nodeid(ctx, attrs[i].value, &attrs[i].value);
			if (r != HSAKMT_STATUS_SUCCESS) {
				pr_debug("invalid GPU ID: %d\n",
					 attrs[i].value);
				return r;
			}
		}
	}

	return HSAKMT_STATUS_SUCCESS;
#endif
}

static HSAKMT_STATUS
hsaKmtSetGetXNACKModeCtx(HsaKFDContext *ctx, HSAint32 * enable)
{
	struct kfd_ioctl_set_xnack_mode_args args;

	CHECK_KFD_OPEN();
	CHECK_KFD_MINOR_VERSION(5);

	args.xnack_enabled = *enable;

	if (hsakmt_ioctl(ctx->fd, AMDKFD_IOC_SET_XNACK_MODE, &args)) {
		if (errno == EPERM) {
			pr_debug("set mode not supported %s\n",
				 strerror(errno));
			return HSAKMT_STATUS_NOT_SUPPORTED;
		} else if (errno == EBUSY) {
			pr_debug("hsakmt_ioctl queues not empty %s\n",
				 strerror(errno));
		}
		return HSAKMT_STATUS_ERROR;
	}

	*enable = args.xnack_enabled;

	return HSAKMT_STATUS_SUCCESS;
}

HSAKMT_STATUS HSAKMTAPI
hsaKmtSetXNACKModeCtx(HsaKFDContext *ctx, HSAint32 enable)
{
	return hsaKmtSetGetXNACKModeCtx(ctx, &enable);
}

HSAKMT_STATUS HSAKMTAPI
hsaKmtGetXNACKModeCtx(HsaKFDContext *ctx, HSAint32 * enable)
{
	*enable = -1;
	return hsaKmtSetGetXNACKModeCtx(ctx, enable);
}


HSAKMT_STATUS HSAKMTAPI
hsaKmtSVMSetAttr(void *start_addr, HSAuint64 size, unsigned int nattr,
		 HSA_SVM_ATTRIBUTE *attrs)
{
	return hsaKmtSVMSetAttrCtx(&hsakmt_primary_kfd_ctx, start_addr, size, nattr, attrs);
}

HSAKMT_STATUS HSAKMTAPI
hsaKmtSVMGetAttr(void *start_addr, HSAuint64 size, unsigned int nattr,
		 HSA_SVM_ATTRIBUTE *attrs)
{
	return hsaKmtSVMGetAttrCtx(&hsakmt_primary_kfd_ctx, start_addr, size, nattr, attrs);
}

static HSAKMT_STATUS
hsaKmtSetGetXNACKMode(HSAint32 * enable)
{
	return hsaKmtSetGetXNACKModeCtx(&hsakmt_primary_kfd_ctx, enable);
}

HSAKMT_STATUS HSAKMTAPI
hsaKmtSetXNACKMode(HSAint32 enable)
{
	return hsaKmtSetGetXNACKMode(&enable);
}

HSAKMT_STATUS HSAKMTAPI
hsaKmtGetXNACKMode(HSAint32 * enable)
{
	*enable = -1;
	return hsaKmtSetGetXNACKMode(enable);
}
