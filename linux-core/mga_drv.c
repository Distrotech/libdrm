/* mga_drv.c -- Matrox g200/g400 driver -*- linux-c -*-
 * Created: Mon Dec 13 01:56:22 1999 by jhartmann@precisioninsight.com
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
 *	Jeff Hartmann <jhartmann@valinux.com>
 *	Gareth Hughes <gareth@valinux.com>
 *
 */

#include <linux/config.h>
#include "drmP.h"
#include "mga_drv.h"

#define DRIVER_AUTHOR		"VA Linux Systems, Inc."

#define DRIVER_NAME		"mga"
#define DRIVER_DESC		"Matrox G200/G400"
#define DRIVER_DATE		"20001127"

#define DRIVER_MAJOR		2
#define DRIVER_MINOR		0
#define DRIVER_PATCHLEVEL	2

static drm_ioctl_desc_t		mga_ioctls[] = {
	[DRM_IOCTL_NR(DRM_IOCTL_VERSION)]     = { mga_version,     0, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_GET_UNIQUE)]  = { drm_getunique,   0, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_GET_MAGIC)]   = { drm_getmagic,    0, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_IRQ_BUSID)]   = { drm_irq_busid,   0, 1 },

	[DRM_IOCTL_NR(DRM_IOCTL_SET_UNIQUE)]  = { drm_setunique,   1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_BLOCK)]       = { drm_block,       1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_UNBLOCK)]     = { drm_unblock,     1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_CONTROL)]     = { mga_control,     1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_AUTH_MAGIC)]  = { drm_authmagic,   1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_ADD_MAP)]     = { drm_addmap,      1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_ADD_BUFS)]    = { mga_addbufs,     1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_MARK_BUFS)]   = { mga_markbufs,    1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_INFO_BUFS)]   = { mga_infobufs,    1, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_MAP_BUFS)]    = { mga_mapbufs,     1, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_FREE_BUFS)]   = { mga_freebufs,    1, 0 },

	[DRM_IOCTL_NR(DRM_IOCTL_ADD_CTX)]     = { mga_addctx,      1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_RM_CTX)]      = { mga_rmctx,       1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_MOD_CTX)]     = { mga_modctx,      1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_GET_CTX)]     = { mga_getctx,      1, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_SWITCH_CTX)]  = { mga_switchctx,   1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_NEW_CTX)]     = { mga_newctx,      1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_RES_CTX)]     = { mga_resctx,      1, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_ADD_DRAW)]    = { drm_adddraw,     1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_RM_DRAW)]     = { drm_rmdraw,      1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_DMA)]	      = { mga_dma,         1, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_LOCK)]	      = { mga_lock,        1, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_UNLOCK)]      = { mga_unlock,      1, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_FINISH)]      = { drm_finish,      1, 0 },

	[DRM_IOCTL_NR(DRM_IOCTL_AGP_ACQUIRE)] = { drm_agp_acquire, 1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_AGP_RELEASE)] = { drm_agp_release, 1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_AGP_ENABLE)]  = { drm_agp_enable,  1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_AGP_INFO)]    = { drm_agp_info,    1, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_AGP_ALLOC)]   = { drm_agp_alloc,   1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_AGP_FREE)]    = { drm_agp_free,    1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_AGP_BIND)]    = { drm_agp_bind,    1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_AGP_UNBIND)]  = { drm_agp_unbind,  1, 1 },

	[DRM_IOCTL_NR(DRM_IOCTL_MGA_INIT)]    = { mga_dma_init,    1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_MGA_SWAP)]    = { mga_swap_bufs,   1, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_MGA_CLEAR)]   = { mga_clear_bufs,  1, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_MGA_ILOAD)]   = { mga_iload,       1, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_MGA_VERTEX)]  = { mga_vertex,      1, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_MGA_FLUSH)]   = { mga_flush_ioctl, 1, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_MGA_INDICES)] = { mga_indices,     1, 0 },
};

#define DRIVER_IOCTL_COUNT	DRM_ARRAY_SIZE( mga_ioctls )

#define HAVE_AGP		1
#define MUST_HAVE_AGP		1

#define HAVE_MTRR		1

#define HAVE_CTX_BITMAP		1

#define HAVE_DMA		1
#define HAVE_DMA_IRQ		1
#define HAVE_DMA_QUEUE		1
#define HAVE_DMA_SCHEDULE	1

#define HAVE_DMA_QUIESCENT	1
#define DRIVER_DMA_QUIESCENT() do {					\
	DRM_DEBUG( "_DRM_LOCK_QUIESCENT\n" );				\
	mga_flush_queue( dev );						\
	mga_dma_quiescent( dev );					\
} while (0)

#define HAVE_DRIVER_RELEASE	1
#define DRIVER_RELEASE() do {						\
	mga_reclaim_buffers( dev, priv->pid );				\
	if ( dev->dev_private ) {					\
		drm_mga_private_t *dev_priv =				\
			(drm_mga_private_t *)dev->dev_private;		\
		dev_priv->dispatch_status &= MGA_IN_DISPATCH;		\
	}								\
} while (0)

#define DRIVER_PRETAKEDOWN() do {					\
	if ( dev->dev_private ) mga_dma_cleanup( dev );			\
} while (0)


#define TAG(x) mga_##x
#include "driver_tmp.h"


#if 0

int mga_unlock(struct inode *inode, struct file *filp, unsigned int cmd,
		 unsigned long arg)
{
	drm_file_t	  *priv	  = filp->private_data;
	drm_device_t	  *dev	  = priv->dev;
	drm_lock_t	  lock;

	if (copy_from_user(&lock, (drm_lock_t *)arg, sizeof(lock)))
		return -EFAULT;

	if (lock.context == DRM_KERNEL_CONTEXT) {
		DRM_ERROR("Process %d using kernel context %d\n",
			  current->pid, lock.context);
		return -EINVAL;
	}

	atomic_inc(&dev->total_unlocks);
	if (_DRM_LOCK_IS_CONT(dev->lock.hw_lock->lock))
		atomic_inc(&dev->total_contends);
	drm_lock_transfer(dev, &dev->lock.hw_lock->lock, DRM_KERNEL_CONTEXT);
	mga_dma_schedule(dev, 1);

	if (drm_lock_free(dev, &dev->lock.hw_lock->lock,
			  DRM_KERNEL_CONTEXT)) DRM_ERROR("\n");

	unblock_all_signals();
	return 0;
}

#endif
