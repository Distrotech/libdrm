/* radeon_drv.c -- ATI Radeon driver -*- linux-c -*-
 * Created: Wed Feb 14 17:10:04 2001 by gareth@valinux.com
 *
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
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Gareth Hughes <gareth@valinux.com>
 */


#ifdef __linux__
#include <linux/config.h>
#endif

#ifdef __FreeBSD__
#include <sys/types.h>
#include <sys/bus.h>
#include <pci/pcivar.h>
#include <opt_drm_linux.h>
#endif

#include "radeon.h"
#include "drmP.h"
#include "radeon_drv.h"
#if __REALLY_HAVE_SG
#include "ati_pcigart.h"
#endif

#define DRIVER_AUTHOR		"Gareth Hughes, VA Linux Systems Inc."

#define DRIVER_NAME		"radeon"
#define DRIVER_DESC		"ATI Radeon"
#define DRIVER_DATE		"20010405"

#define DRIVER_MAJOR		1
#define DRIVER_MINOR		1
#define DRIVER_PATCHLEVEL	1

#ifdef __FreeBSD__
static int radeon_probe(device_t dev)
{
	const char *s = 0;

	switch (pci_get_devid(dev)) {
	/* FIXME: Add radeon detection*/
		/*case 0x0525102b:
		s = "Matrox MGA G400 AGP graphics accelerator";
		break;

	case 0x0521102b:
		s = "Matrox MGA G200 AGP graphics accelerator";
		break; */
	}

	if (s) {
		device_set_desc(dev, s);
		return 0;
	}

	return ENXIO;
}
#endif

#define DRIVER_IOCTLS							     \
 [DRM_IOCTL_NR(DRM_IOCTL_DMA)]               = { radeon_cp_buffers,  1, 0 }, \
 [DRM_IOCTL_NR(DRM_IOCTL_RADEON_CP_INIT)]    = { radeon_cp_init,     1, 1 }, \
 [DRM_IOCTL_NR(DRM_IOCTL_RADEON_CP_START)]   = { radeon_cp_start,    1, 1 }, \
 [DRM_IOCTL_NR(DRM_IOCTL_RADEON_CP_STOP)]    = { radeon_cp_stop,     1, 1 }, \
 [DRM_IOCTL_NR(DRM_IOCTL_RADEON_CP_RESET)]   = { radeon_cp_reset,    1, 1 }, \
 [DRM_IOCTL_NR(DRM_IOCTL_RADEON_CP_IDLE)]    = { radeon_cp_idle,     1, 0 }, \
 [DRM_IOCTL_NR(DRM_IOCTL_RADEON_RESET)]    = { radeon_engine_reset,  1, 0 }, \
 [DRM_IOCTL_NR(DRM_IOCTL_RADEON_FULLSCREEN)] = { radeon_fullscreen,  1, 0 }, \
 [DRM_IOCTL_NR(DRM_IOCTL_RADEON_SWAP)]       = { radeon_cp_swap,     1, 0 }, \
 [DRM_IOCTL_NR(DRM_IOCTL_RADEON_CLEAR)]      = { radeon_cp_clear,    1, 0 }, \
 [DRM_IOCTL_NR(DRM_IOCTL_RADEON_VERTEX)]     = { radeon_cp_vertex,   1, 0 }, \
 [DRM_IOCTL_NR(DRM_IOCTL_RADEON_INDICES)]    = { radeon_cp_indices,  1, 0 }, \
 [DRM_IOCTL_NR(DRM_IOCTL_RADEON_TEXTURE)]    = { radeon_cp_texture,  1, 0 }, \
 [DRM_IOCTL_NR(DRM_IOCTL_RADEON_STIPPLE)]    = { radeon_cp_stipple,  1, 0 }, \
 [DRM_IOCTL_NR(DRM_IOCTL_RADEON_INDIRECT)]   = { radeon_cp_indirect, 1, 1 },


#if 0
/* GH: Count data sent to card via ring or vertex/indirect buffers.
 */
#define __HAVE_COUNTERS         3
#define __HAVE_COUNTER6         _DRM_STAT_IRQ
#define __HAVE_COUNTER7         _DRM_STAT_PRIMARY
#define __HAVE_COUNTER8         _DRM_STAT_SECONDARY
#endif


#include "drm_agpsupport.h"
#include "drm_auth.h"
#include "drm_bufs.h"
#include "drm_context.h"
#include "drm_dma.h"
#include "drm_drawable.h"
#include "drm_drv.h"
#include "drm_fops.h"
#include "drm_init.h"
#include "drm_ioctl.h"
#include "drm_lock.h"
#include "drm_memory.h"
#include "drm_vm.h"
#ifdef __linux__
#include "drm_proc.h"
#include "drm_stub.h"
#endif
#ifdef __FreeBSD__
#include "drm_sysctl.h"
#endif
#if __REALLY_HAVE_SG
#include "drm_scatter.h"
#endif

#ifdef __FreeBSD__
DRIVER_MODULE(radeon, pci, radeon_driver, radeon_devclass, 0, 0);
#endif
