/* savage_drm.h -- Public header for the savage driver -*- linux-c -*-
 *
 * Copyright 2000 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Fremont, California.
 * Copyright 2002 Tungsten Graphics, Inc., Cedar Park, Texas.
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
 * Authors:
 *    Kevin E. Martin <martin@valinux.com>
 *    Gareth Hughes <gareth@valinux.com>
 *    Keith Whitwell <keith@tungstengraphics.com>
 */

#ifndef __SAVAGE_DRM_H__
#define __SAVAGE_DRM_H__

/* WARNING: If you change any of these defines, make sure to change the
 * defines in the X server file (savage_sarea.h)
 */
#ifndef __SAVAGE_SAREA_DEFINES__
#define __SAVAGE_SAREA_DEFINES__

/* Keep these small for testing */
#define SAVAGE_NR_SAREA_CLIPRECTS	12

/* There are 2 heaps (local/AGP).  Each region within a heap is a
 * minimum of 64k, and there are at most 64 of them per heap.
 */
#define SAVAGE_CARD_HEAP		0
#define SAVAGE_AGP_HEAP			1
#define SAVAGE_NR_TEX_HEAPS		2
#define SAVAGE_NR_TEX_REGIONS		64
#define SAVAGE_LOG_TEX_GRANULARITY	16

#define SAVAGE_MAX_TEXTURE_LEVELS	10
#define SAVAGE_MAX_TEXTURE_UNITS	2

#endif

typedef struct {
    unsigned int blue;
    unsigned int green;
    unsigned int red;
    unsigned int alpha;
} savage_color_regs_t;

typedef struct {
    /* Context state */

    /* Vertex format state */

    /* Line state */

    /* Bumpmap state */

    /* Mask state */

    /* Viewport state */

    /* Setup state */

    /* Misc state */
} savage_context_regs_t;

/* Setup registers for each texture unit */
typedef struct {
    unsigned int tex_cntl;
    unsigned int tex_addr;
    unsigned int tex_blend_cntl;
} savage_texture_regs_t;

typedef struct {
    unsigned char next, prev;	/* indices to form a circular LRU  */
    unsigned char in_use;	/* owned by a client, or free? */
    int age;			/* tracked by clients to update local LRU's */
} savage_tex_region_t;

/* WARNING: Do not change the SAREA structure without changing the kernel
 * as well.
 */
typedef struct {
    /* The channel for communication of state information to the kernel
     * on firing a vertex buffer.
     */
    savage_context_regs_t	ContextState;
    savage_texture_regs_t	TexState[SAVAGE_MAX_TEXTURE_UNITS];
    unsigned int dirty;
    unsigned int vertsize;
    unsigned int vc_format;

    /* The current cliprects, or a subset thereof.
     */
    drm_clip_rect_t boxes[SAVAGE_NR_SAREA_CLIPRECTS];
    unsigned int nbox;

    /* Counters for throttling of rendering clients.
     */
    unsigned int last_frame;
    unsigned int last_dispatch;

    /* Maintain an LRU of contiguous regions of texture space.  If you
     * think you own a region of texture memory, and it has an age
     * different to the one you set, then you are mistaken and it has
     * been stolen by another client.  If global texAge hasn't changed,
     * there is no need to walk the list.
     *
     * These regions can be used as a proxy for the fine-grained texture
     * information of other clients - by maintaining them in the same
     * lru which is used to age their own textures, clients have an
     * approximate lru for the whole of global texture space, and can
     * make informed decisions as to which areas to kick out.  There is
     * no need to choose whether to kick out your own texture or someone
     * else's - simply eject them all in LRU order.
     */
				/* Last elt is sentinal */
    savage_tex_region_t texList[SAVAGE_NR_TEX_HEAPS][SAVAGE_NR_TEX_REGIONS+1];
				/* last time texture was uploaded */
    int texAge[SAVAGE_NR_TEX_HEAPS];

    int ctxOwner;		/* last context to upload state */
} drm_savage_sarea_t;

/* WARNING: If you change any of these defines, make sure to change the
 * defines in the Xserver file (xf86drmSavage.h)
 *
 * KW: actually it's illegal to change any of this (backwards compatibility).
 */

/* Savage specific ioctls
 * The device specific ioctl range is 0x40 to 0x79.
 */
#define DRM_IOCTL_SAVAGE_BCI_INIT	DRM_IOW( 0x40, drm_savage_init_t)
#define DRM_IOCTL_SAVAGE_VERTEX		DRM_IOW( 0x49, drm_buf_t)

typedef struct drm_savage_init {
	enum {
		SAVAGE_INIT_BCI    = 0x01,
		SAVAGE_CLEANUP_BCI = 0x02,
	} func;
	unsigned long sarea_priv_offset;
	int is_pci;
	int agp_size;
	int usec_timeout;

	unsigned int fb_bpp;
	unsigned int front_offset, front_pitch;
	unsigned int back_offset, back_pitch;
	unsigned int depth_bpp;
	unsigned int depth_offset, depth_pitch;

	unsigned long fb_offset;
	unsigned long mmio_offset;
	unsigned long bci_offset;
	unsigned long buffers_offset;
	unsigned long agp_textures_offset;
} drm_savage_init_t;

#endif
