/* tdfx_drv.c -- tdfx driver -*- linux-c -*-
 * Created: Thu Oct  7 10:38:32 1999 by faith@precisioninsight.com
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *	Rickard E. (Rik) Faith <faith@valinux.com>
 *	Daryll Strauss <daryll@valinux.com>
 *	Gareth Hughes <gareth@valinux.com>
 *
 */

#include <linux/config.h>
#include "drmP.h"
#include "tdfx_drv.h"

#define DRIVER_AUTHOR		"VA Linux Systems, Inc."

#define DRIVER_NAME		"tdfx"
#define DRIVER_DESC		"3dfx Banshee/Voodoo3+"
#define DRIVER_DATE		"20001127"

#define DRIVER_MAJOR		1
#define DRIVER_MINOR		0
#define DRIVER_PATCHLEVEL	0

static drm_ioctl_desc_t		tdfx_ioctls[] = {
	[DRM_IOCTL_NR(DRM_IOCTL_VERSION)]     = { tdfx_version,    0, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_GET_UNIQUE)]  = { drm_getunique,   0, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_GET_MAGIC)]   = { drm_getmagic,    0, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_IRQ_BUSID)]   = { drm_irq_busid,   0, 1 },

	[DRM_IOCTL_NR(DRM_IOCTL_SET_UNIQUE)]  = { drm_setunique,   1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_BLOCK)]	      = { drm_block,       1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_UNBLOCK)]     = { drm_unblock,     1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_AUTH_MAGIC)]  = { drm_authmagic,   1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_ADD_MAP)]     = { drm_addmap,      1, 1 },

	[DRM_IOCTL_NR(DRM_IOCTL_ADD_CTX)]     = { tdfx_addctx,     1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_RM_CTX)]      = { tdfx_rmctx,      1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_MOD_CTX)]     = { tdfx_modctx,     1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_GET_CTX)]     = { tdfx_getctx,     1, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_SWITCH_CTX)]  = { tdfx_switchctx,  1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_NEW_CTX)]     = { tdfx_newctx,     1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_RES_CTX)]     = { tdfx_resctx,     1, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_ADD_DRAW)]    = { drm_adddraw,     1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_RM_DRAW)]     = { drm_rmdraw,      1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_LOCK)]	      = { tdfx_lock,       1, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_UNLOCK)]      = { tdfx_unlock,     1, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_FINISH)]      = { drm_finish,      1, 0 },

#if defined(CONFIG_AGP) || defined(CONFIG_AGP_MODULE)
	[DRM_IOCTL_NR(DRM_IOCTL_AGP_ACQUIRE)] = { drm_agp_acquire, 1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_AGP_RELEASE)] = { drm_agp_release, 1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_AGP_ENABLE)]  = { drm_agp_enable,  1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_AGP_INFO)]    = { drm_agp_info,    1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_AGP_ALLOC)]   = { drm_agp_alloc,   1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_AGP_FREE)]    = { drm_agp_free,    1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_AGP_BIND)]    = { drm_agp_unbind,  1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_AGP_UNBIND)]  = { drm_agp_bind,    1, 1 },
#endif
};

#define DRIVER_IOCTL_COUNT	DRM_ARRAY_SIZE( tdfx_ioctls )

#define HAVE_AGP		1

#define HAVE_MTRR		1

#define HAVE_CTX_BITMAP		1


#define TAG(x) tdfx_##x
#include "driver_tmp.h"
