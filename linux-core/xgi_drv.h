/* xgi_drv.h -- Private header for xgi driver -*- linux-c -*-
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * All rights reserved.
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
 */

#ifndef _XGI_DRV_H_
#define _XGI_DRV_H_

#define DRIVER_AUTHOR	 "XGI"
#define DRIVER_NAME	 "xgi"
#define DRIVER_DESC	 "XGI Volari DRI Kernel Module"
#define DRIVER_DATE	 "20060417"
#define DRIVER_MAJOR	 1
#define DRIVER_MINOR	 0
#define DRIVER_PATCHLEVEL  0

#define XGI_HAVE_CORE_MM

#include "drm_sman.h"

typedef struct drm_xgi_private {
	drm_local_map_t *mmio;
	drm_sman_t sman;
	unsigned long chipset;
	int vram_initialized;
	int agp_initialized;
	unsigned long vram_offset;
	unsigned long agp_offset;
} drm_xgi_private_t;

extern void xgi_lastclose(drm_device_t *dev);

extern drm_ioctl_desc_t xgi_ioctls[];
extern int xgi_max_ioctl;
extern void xgi_reclaim_buffers_locked(drm_device_t * dev, struct file *filp);

#endif
