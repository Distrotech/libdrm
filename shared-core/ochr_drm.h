/*
 * Copyright 1998-2003 VIA Technologies, Inc. All Rights Reserved.
 * Copyright 2001-2003 S3 Graphics, Inc. All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * VIA, S3 GRAPHICS, AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#ifndef _OCHR_DRM_H_
#define _OCHR_DRM_H_
#include <ttm/ttm_placement_user.h>
#include <ttm/ttm_fence_user.h>

/*
 * With the arrival of libdrm there is a need to version this file.
 * As usual, bump MINOR for new features, MAJOR for changes that create
 * backwards incompatibilities, (which should be avoided whenever possible).
 */

#define DRM_VIA_DRIVER_DATE		"20090119"

#define DRM_VIA_DRIVER_MAJOR		0
#define DRM_VIA_DRIVER_MINOR		1
#define DRM_VIA_DRIVER_PATCHLEVEL	0
#define DRM_VIA_DRIVER_VERSION	  (((VIA_DRM_DRIVER_MAJOR) << 16) | (VIA_DRM_DRIVER_MINOR))

#define DRM_VIA_MAX_MIP                    16
#define DRM_VIA_NR_SCANOUTS                4
#define DRM_VIA_NR_XVMC_PORTS	           10
#define DRM_VIA_NR_XVMC_LOCKS	           5
#define DRM_VIA_MAX_CACHELINE_SIZE	   64
#define DRM_VIA_XVMCLOCKPTR(saPriv,lockNo)					\
	((volatile struct drm_hw_lock *)(((((unsigned long) (saPriv)->xvmc_lock_area) + \
				      (DRM_VIA_MAX_CACHELINE_SIZE - 1)) &	\
				     ~(DRM_VIA_MAX_CACHELINE_SIZE - 1)) +	\
				    DRM_VIA_MAX_CACHELINE_SIZE*(lockNo)))

/* VIA specific ioctls */
#define DRM_VIA_VT              0x00
#define DRM_VIA_GET_PARAM       0x01
#define DRM_VIA_EXTENSION       0x02

#define DRM_IOCTL_VIA_VT          DRM_IOW( DRM_COMMAND_BASE + DRM_VIA_VT, struct drm_via_vt)
#define DRM_IOCTL_VIA_GET_PARAM   DRM_IOWR(DRM_COMMAND_BASE + DRM_VIA_GET_PARAM, struct drm_via_getparam_arg)
#define DRM_IOCTL_VIA_EXTENSION   DRM_IOWR(DRM_COMMAND_BASE + DRM_VIA_EXTENSION, struct drm_via_extension_arg)

/*
 * TTM Ioctls.
 */

/* Indices into buf.Setup where various bits of state are mirrored per
 * context and per buffer.  These can be fired at the card as a unit,
 * or in a piecewise fashion as required.
 */

struct drm_via_futex {
	uint32_t ms;
	uint32_t lock;
	uint32_t val;
	enum {
		VIA_FUTEX_WAIT = 0x00,
		VIA_FUTEX_WAKE = 0X01
	} op;
};

/**
 * struct drm_via_vt
 *
 * @enter: 1 for entervt, 0 for leavevt.
 *
 * Argument to the DRM_VIA_VT ioctl.
 */

struct drm_via_vt {
	int32_t enter;
};

/**
 * struct drm_via_scanout
 *
 * @stamp: sequence number identifying last change.
 * @handle: buffer object handle
 * @width: scanout buffer width
 * @height: scanout buffer height
 * @stride: scanout buffer stride in bytes
 * @depth: scanout buffer depth. 15, 16 or 32.
 *
 * Part of the shared memory area.
 */

struct drm_via_scanout {
	uint64_t stamp;
	uint32_t handle;
	uint32_t width;
	uint32_t height;
	uint32_t stride;
	uint32_t depth;
	uint32_t pad64;
};

/**
 * struct drm_via_sarea_xvmc
 *
 * @xvmc_lock_area: Area for XvMC decoder locks.
 * @xvmc_displaying: Currently displaying surface.
 * @xvmc_sub_pic_on: 1: subpicture on, 0: subpicture off.
 * @xvmc_ctx: Last context to upload Mpeg state
 *
 * The xvmc part of the driver-specific sarea. The offset of this structure
 * into the driver specific sarea can be obtained using the extension
 * ioctl.
 */

struct drm_via_sarea_xvmc {
	char xvmc_lock_area[DRM_VIA_MAX_CACHELINE_SIZE *
			    (DRM_VIA_NR_XVMC_LOCKS + 1)];
	uint32_t xvmc_displaying[DRM_VIA_NR_XVMC_PORTS];
	uint32_t xvmc_sub_pic_on[DRM_VIA_NR_XVMC_PORTS];
	uint32_t xvmc_ctx[DRM_VIA_NR_XVMC_LOCKS];
};

/**
 * struct drm_via_sarea
 *
 * @scanouts: Scanout buffer info.
 * @pfCurrentOffset: For page-flipping.
 *
 * via shared memory area.
 */

struct drm_via_sarea {
	struct drm_via_scanout scanouts[DRM_VIA_NR_SCANOUTS];

	/*
	 * Below is for XvMC.
	 * We want the lock integers alone on, and aligned to, a cache line.
	 * Therefore this somewhat strange construct.
	 */

	/* Used by the 3d driver only at this point, for pageflipping:
	 */
	uint32_t pfCurrentOffset;
};

enum drm_via_params {
	DRM_VIA_PARAM_VRAM_SIZE,
	DRM_VIA_PARAM_TT_SIZE,
	DRM_VIA_PARAM_AGP_SIZE,
	DRM_VIA_PARAM_HAS_IRQ,
	DRM_VIA_PARAM_SAREA_SIZE,
};

/**
 * struct drm_via_getparam_arg
 *
 * @value: Returned value.
 * @param: Requested parameter. (int) enum drm_via_params
 *
 * Argument to the DRM_VIA_GET_PARAM Ioctl.
 */

struct drm_via_getparam_arg {
	uint64_t value;
	uint32_t param;
	uint32_t pad64;
};

/**
 * struct drm_via_extension_rep
 *
 * @exists: extension exists.
 * @driver_ioctl_offset: Driver ioctl number of first ioctl in the extension.
 * @major: Major version of the extension.
 * @minor: Minor version of the extension.
 * @pl: Patchlevel version of the extension.
 *
 * Output from the DRM_VIA_EXTENSION ioctl.
 */

struct drm_via_extension_rep {
	int32_t exists;
	uint32_t driver_ioctl_offset;
	uint32_t driver_sarea_offset;
	uint32_t major;
	uint32_t minor;
	uint32_t pl;
	uint32_t pad64;
};

#define DRM_VIA_EXT_NAME_LEN 128

/**
 * union drm_via_extension_arg
 *
 * @extension: Input: Name of the extension.
 * @rep: Ouput: Reply.
 */

union drm_via_extension_arg {
	char extension[DRM_VIA_EXT_NAME_LEN];
	struct drm_via_extension_rep rep;
};

/*
 * Below is execbuf stuff.
 */

#define VIA_RELOC_BUF_SIZE 8192

enum drm_via_reloc_type {
	VIA_RELOC_ZBUF,
	VIA_RELOC_DSTBUF,
	VIA_RELOC_PF,
	VIA_RELOC_2D,
	VIA_RELOC_TEX,
	VIA_RELOC_YUV,
	VIA_RELOC_NUMTYPE
};

#define VIA_USE_PRESUMED     (1 << 0)
#define VIA_PRESUMED_AGP     (1 << 1)

/**
 * struct drm_via_validate_req
 *
 * @set_flags: Validation flags to set.
 * @clear_flags: Validation flags to clear.
 * @next: User space pointer to the next struct drm_via_validate_req in
 * a linked list, cast to an uint64_t.
 * @presumed_gpu_offset: Presumed gpu offset, used in the command stream,
 * of the buffer.
 * @presumed_flags: Flags indicating how the presumed gpu offset should be
 * interpreted (and if).
 * @cmdbuf_first: dword offset of the first state packet referencing this
 * buffer in the command stream. Used for command stream splitting.
 * @cmdbuf_last: dword offset of the last + 1 state packet referencing this
 * buffer in the command stream. Used for command stream splitting.
 *
 * Information record used for buffer object validation prior to
 * command submission.
 */

struct drm_via_validate_req {
	uint64_t set_flags;
	uint64_t clear_flags;
	uint64_t next;
	uint64_t presumed_gpu_offset;
	uint32_t buffer_handle;
	uint32_t presumed_flags;
	uint32_t cmdbuf_first;
	uint32_t cmdbuf_last;
};

/**
 * struct drm_via_validate_rep
 *
 * @gpu_offset: Last known gpu offset of the buffer.
 * @placement: Last known TTM placement flag of the buffer.
 * @fence_type_mask: Set of fence type flags used to determine buffer idle.
 */

struct drm_via_validate_rep {
	uint64_t gpu_offset;
	uint32_t placement;
	uint32_t fence_type_mask;
};

/**
 * struct drm_via_validate_rep
 */

struct drm_via_validate_arg {
	int handled;
	int ret;
	union {
		struct drm_via_validate_req req;
		struct drm_via_validate_rep rep;
	} d;
};

struct drm_via_reloc_header {
	uint64_t next_header;
	uint32_t used;
	uint32_t num_relocs;
};

struct drm_via_reloc_bufaddr {
	uint32_t index;
	uint32_t delta;
};

/*
 * Relocation types.
 */

struct drm_via_base_reloc {
	enum drm_via_reloc_type type;
	uint32_t offset;
};

struct drm_via_yuv_reloc {
	struct drm_via_base_reloc base;
	struct drm_via_reloc_bufaddr addr;
	int32_t planes;
	uint32_t shift;
	uint32_t plane_offs[4];
};

struct drm_via_zbuf_reloc {
	struct drm_via_base_reloc base;
	struct drm_via_reloc_bufaddr addr;
};

struct drm_via_2d_reloc {
	struct drm_via_base_reloc base;
	struct drm_via_reloc_bufaddr addr;
	uint32_t bpp;
	uint32_t pos;
};

struct drm_via_texture_reloc {
	struct drm_via_base_reloc base;
	uint32_t low_mip;
	uint32_t hi_mip;
	uint32_t reg_tex_fm;
	uint32_t pad64;
	struct drm_via_reloc_bufaddr addr[DRM_VIA_MAX_MIP];
};

/*
 * Execbuf arg.
 */

struct drm_via_ttm_fence_rep {
	uint32_t handle;
	uint32_t fence_class;
	uint32_t fence_type;
	uint32_t signaled_types;
	uint32_t error;
};

struct drm_via_clip_rect {
	int32_t x1, x2, y1, y2;
};

#define DRM_VIA_HAVE_CLIP     (1 << 0)
#define DRM_VIA_FENCE_NO_USER (1 << 1)
#define DRM_VIA_WAIT_BARRIER  (1 << 2)

struct drm_via_ttm_execbuf_control {
	struct drm_via_ttm_fence_rep rep;
	uint32_t first_clip;
	uint32_t vram_avail;
	uint32_t agp_avail;
	uint32_t pad_64;
};

struct drm_via_ttm_execbuf_arg {

	uint64_t buffer_list;
	uint64_t reloc_list;
	uint64_t cmd_buffer;

	uint64_t ls_buffer_list;
	uint64_t ls_reloc_list;
	uint64_t ls_buffer;

	uint64_t cliprect_addr;
	uint64_t control;

	uint32_t num_buffers;
	uint32_t num_ls_buffers;
	uint32_t cmd_buffer_size;
	uint32_t lost_state_size;
	uint32_t mechanism;
	uint32_t exec_flags;

	uint32_t cliprect_offset;
	uint32_t num_cliprects;

	uint32_t context;
	uint32_t pad64;
};

/*
 * Flag layout in the 64-bit VIA validate
 * flag argument.
 */

#define VIA_PLACEMENT_MASK   0x000000000000FFFFULL
#define VIA_PLACEMENT_SHIFT  0

/*
 * Additional placement domain. A pre-bound
 * area of AGP memory for fast buffer object
 * creation and destruction.
 */

#define VIA_PL_FLAG_AGP      TTM_PL_FLAG_PRIV0

#define VIA_ACCESS_MASK      0x0000000000FF0000ULL
#define VIA_ACCESS_SHIFT     16
#define VIA_ACCESS_READ      (TTM_ACCESS_READ << VIA_ACCESS_SHIFT)
#define VIA_ACCESS_WRITE     (TTM_ACCESS_WRITE << VIA_ACCESS_SHIFT)

/*
 * Validation access flags that indicate that the buffer
 * will be accessed by the following engines:
 */

#define VIA_VALMODE_MASK     0x0000FFFF00000000ULL
#define VIA_VALMODE_SHIFT    32

#define VIA_VAL_FLAG_HQV0    (1ULL << (VIA_VALMODE_SHIFT + 0))
#define VIA_VAL_FLAG_HQV1    (1ULL << (VIA_VALMODE_SHIFT + 1))
#define VIA_VAL_FLAG_MPEG0   (1ULL << (VIA_VALMODE_SHIFT + 2))
#define VIA_VAL_FLAG_MPEG1   (1ULL << (VIA_VALMODE_SHIFT + 3))

/*
 * The command reader.
 * It has two submission mechanisms that are serialized,
 * and user-space can indicate when a specific mechanism
 * needs to be used.
 */

#define VIA_ENGINE_CMD      0
#define _VIA_MECHANISM_AGP  0
#define _VIA_MECHANISM_PCI  1

#define VIA_ENGINE_DMA0     1
#define VIA_ENGINE_DMA1     2
#define VIA_ENGINE_DMA2     3
#define VIA_ENGINE_DMA3     4
#define VIA_NUM_ENGINES     5

/*
 * These fence types are defined
 * for engine 0 and 1. Indicates that the
 * HQV or MPEG engine is done with the
 * commands associated with the fence.
 */

#define VIA_FENCE_TYPE_HQV0  (1 << 1)
#define VIA_FENCE_TYPE_HQV1  (1 << 2)
#define VIA_FENCE_TYPE_MPEG0 (1 << 3)
#define VIA_FENCE_TYPE_MPEG1 (1 << 4)

/*
 * This fence type is for the DMA engines.
 * Indicates that the device mapping for
 * the system memory is released, and it's
 * OK for reuse by the CPU.
 */

#define VIA_FENCE_TYPE_SYSMEM (1 << 1)

#endif				/* _VIA_DRM_H_ */
