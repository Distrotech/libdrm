/* mach64_drm.h -- Public header for the mach64 driver -*- linux-c -*-
 * Created: Thu Nov 30 20:04:32 2000 by gareth@valinux.com
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

#ifndef __MACH64_DRM_H__
#define __MACH64_DRM_H__


/* WARNING: If you change any of these defines, make sure to change the
 * defines in the Xserver file (xf86drmMach64.h)
 */
#ifndef __MACH64_DEFINES__
#define __MACH64_DEFINES__

/* FIXME: fill this in...
 */

/* Keep these small for testing.
 */
#define MACH64_NR_SAREA_CLIPRECTS	8

/* WARNING: If you change any of these defines, make sure to change the
 * defines in the Xserver file (mach64_sarea.h)
 */
#define MACH64_CARD_HEAP		0
#define MACH64_AGP_HEAP			1
#define MACH64_NR_TEX_HEAPS		2
#define MACH64_NR_TEX_REGIONS		16
#define MACH64_LOG_TEX_GRANULARITY	16

#endif

typedef struct drm_mach64_init {
	enum {
		MACH64_INIT_DMA = 0x01,
		MACH64_CLEANUP_DMA = 0x02
	} func;
	int sarea_priv_offset;
	int is_pci;
	int cpp;

	unsigned int pitch;
	unsigned int front_offset;
	unsigned int back_offset;
	unsigned int depth_offset;

	unsigned int texture_offset;
	unsigned int texture_size;

	unsigned int agp_texture_offset;
	unsigned int agp_texture_size;

	unsigned int mmio_offset;
} drm_r128_init_t;


typedef struct drm_tex_region {
	unsigned char next, prev;
	unsigned char in_use;
	int age;
} drm_tex_region_t;

typedef struct drm_mach64_sarea {
	/* The channel for communication of state information to the kernel
	 * on firing a vertex dma buffer.
	 */

	/* FIXME: fill this in... */

	/* The current cliprects, or a subset thereof.
	 */
	drm_clip_rect_t boxes[R128_NR_SAREA_CLIPRECTS];
	unsigned int nbox;

	/* Counters for client-side throttling of rendering clients.
	 */
	unsigned int last_frame;
	unsigned int last_dispatch;

	/* Texture memory LRU.
	 */
	drm_tex_region_t tex_list[MACH64_NR_TEX_HEAPS][MACH64_NR_TEX_REGIONS+1];
	int tex_age[MACH64_NR_TEX_HEAPS];
	int ctx_owner;
} drm_mach64_sarea_t;

#endif
