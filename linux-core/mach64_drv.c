/* mach64_drv.c -- mach64 (Rage Pro) driver -*- linux-c -*-
 * Created: Fri Nov 24 18:34:32 2000 by gareth@valinux.com
 *
 * Copyright 2000 Gareth Hughes
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
 * GARETH HUGHES BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Gareth Hughes <gareth@valinux.com>
 */

#include <linux/config.h>
#include "drmP.h"
#include "mach64_drv.h"


#define DRIVER_AUTHOR		"Gareth Hughes"

#define DRIVER_NAME		"mach64"
#define DRIVER_DESC		"DRM module for the ATI Rage Pro"
#define DRIVER_DATE		"20010107"

#define DRIVER_MAJOR		1
#define DRIVER_MINOR		0
#define DRIVER_PATCHLEVEL	0


static drm_ioctl_desc_t		mach64_ioctls[] = {
	[DRM_IOCTL_NR(DRM_IOCTL_VERSION)]       = { mach64_version,    0, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_GET_UNIQUE)]    = { drm_getunique,     0, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_GET_MAGIC)]     = { drm_getmagic,      0, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_IRQ_BUSID)]     = { drm_irq_busid,     0, 1 },

	[DRM_IOCTL_NR(DRM_IOCTL_SET_UNIQUE)]    = { drm_setunique,     1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_BLOCK)]	        = { drm_block,         1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_UNBLOCK)]       = { drm_unblock,       1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_AUTH_MAGIC)]    = { drm_authmagic,     1, 1 },

	[DRM_IOCTL_NR(DRM_IOCTL_ADD_MAP)]       = { drm_addmap,        1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_ADD_BUFS)]      = { mach64_addbufs,    1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_MARK_BUFS)]     = { drm_markbufs,      1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_INFO_BUFS)]     = { drm_infobufs,      1, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_MAP_BUFS)]      = { mach64_mapbufs,    1, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_FREE_BUFS)]     = { drm_freebufs,      1, 0 },

	[DRM_IOCTL_NR(DRM_IOCTL_ADD_CTX)]       = { mach64_addctx,     1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_RM_CTX)]        = { mach64_rmctx,      1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_MOD_CTX)]       = { mach64_modctx,     1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_GET_CTX)]       = { mach64_getctx,     1, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_SWITCH_CTX)]    = { mach64_switchctx,  1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_NEW_CTX)]       = { mach64_newctx,     1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_RES_CTX)]       = { mach64_resctx,     1, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_ADD_DRAW)]      = { drm_adddraw,       1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_RM_DRAW)]       = { drm_rmdraw,        1, 1 },
#if 0
	[DRM_IOCTL_NR(DRM_IOCTL_DMA)]	        = { mach64_dma,        1, 0 },
#endif
	[DRM_IOCTL_NR(DRM_IOCTL_LOCK)]	        = { mach64_lock,       1, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_UNLOCK)]        = { mach64_unlock,     1, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_FINISH)]        = { drm_finish,        1, 0 },

#if defined(CONFIG_AGP) || defined(CONFIG_AGP_MODULE)
	[DRM_IOCTL_NR(DRM_IOCTL_AGP_ACQUIRE)]   = { drm_agp_acquire,   1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_AGP_RELEASE)]   = { drm_agp_release,   1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_AGP_ENABLE)]    = { drm_agp_enable,    1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_AGP_INFO)]      = { drm_agp_info,      1, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_AGP_ALLOC)]     = { drm_agp_alloc,     1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_AGP_FREE)]      = { drm_agp_free,      1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_AGP_BIND)]      = { drm_agp_bind,      1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_AGP_UNBIND)]    = { drm_agp_unbind,    1, 1 },
#endif

   	[DRM_IOCTL_NR(DRM_IOCTL_MACH64_INIT)]   = { mach64_dma_init,   1, 1 },
   	[DRM_IOCTL_NR(DRM_IOCTL_MACH64_CLEAR)]  = { mach64_dma_clear,  1, 0 },
   	[DRM_IOCTL_NR(DRM_IOCTL_MACH64_SWAP)]   = { mach64_dma_swap,   1, 0 },
   	[DRM_IOCTL_NR(DRM_IOCTL_MACH64_IDLE)]   = { mach64_dma_idle,   1, 0 },
#if 0
   	[DRM_IOCTL_NR(DRM_IOCTL_MACH64_VERTEX)] = { mach64_vertex,     1, 0 },
   	[DRM_IOCTL_NR(DRM_IOCTL_MACH64_BLIT)]   = { mach64_blit,       1, 0 },
   	[DRM_IOCTL_NR(DRM_IOCTL_MACH64_FLUSH)]  = { mach64_flush,      1, 0 },
#endif
};

#define DRIVER_IOCTL_COUNT	DRM_ARRAY_SIZE( mach64_ioctls )

#define HAVE_AGP		1
#define MUST_HAVE_AGP		1

#define HAVE_MTRR		1

#define HAVE_CTX_BITMAP		1

#define HAVE_DMA		1
#define HAVE_DMA_QUEUE		1


#define TAG(x) mach64_##x
#include "driver_tmp.h"
