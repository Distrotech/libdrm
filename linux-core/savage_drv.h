/* savage_drv.h -- Private header for savage driver -*- linux-c -*-
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Fremont, California.
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
 */

#ifndef __SAVAGE_DRV_H__
#define __SAVAGE_DRV_H__

typedef struct drm_savage_freelist {
   	unsigned int age;
   	drm_buf_t *buf;
   	struct drm_radeon_freelist *next;
   	struct drm_radeon_freelist *prev;
} drm_savage_freelist_t;

typedef struct drm_savage_ring_buffer {
	u32 *start;
	u32 *end;
	int size;
	int size_l2qw;

	volatile u32 *head;
	u32 tail;
	u32 tail_mask;
	int space;

	int high_mark;
} drm_savage_ring_buffer_t;

typedef struct drm_savage_depth_clear_t {
	u32 rb3d_cntl;
	u32 rb3d_zstencilcntl;
	u32 se_cntl;
} drm_savage_depth_clear_t;


struct mem_block {
	struct mem_block *next;
	struct mem_block *prev;
	int start;
	int size;
	int pid;		/* 0: free, -1: heap, other: real pids */
};

typedef struct drm_savage_private {
	drm_savage_ring_buffer_t ring;
	drm_savage_sarea_t *sarea_priv;

	int agp_size;
	u32 agp_vm_start;
	unsigned long agp_buffers_offset;

	int bci_mode;
	int bci_running;

   	drm_savage_freelist_t *head;
   	drm_savage_freelist_t *tail;
	int last_buf;
	volatile u32 *scratch;
	int writeback_works;

	int usec_timeout;

	int is_r200;

	int is_pci;
	unsigned long phys_pci_gart;
	dma_addr_t bus_pci_gart;

	struct {
		u32 boxes;
		int freelist_timeouts;
		int freelist_loops;
		int requested_bufs;
		int last_frame_reads;
		int last_clear_reads;
		int clears;
		int texture_uploads;
	} stats;

	int do_boxes;
	int page_flipping;
	int current_page;

	u32 color_fmt;
	unsigned int front_offset;
	unsigned int front_pitch;
	unsigned int back_offset;
	unsigned int back_pitch;

	u32 depth_fmt;
	unsigned int depth_offset;
	unsigned int depth_pitch;

	u32 front_pitch_offset;
	u32 back_pitch_offset;
	u32 depth_pitch_offset;

	drm_savage_depth_clear_t depth_clear;

	drm_map_t *sarea;
	drm_map_t *fb;
	drm_map_t *mmio;
	drm_map_t *bci_ring;
	drm_map_t *ring_rptr;
	drm_map_t *buffers;
	drm_map_t *agp_textures;

	struct mem_block *agp_heap;
	struct mem_block *fb_heap;

	/* SW interrupt */
   	wait_queue_head_t swi_queue;
   	atomic_t swi_emitted;

} drm_savage_private_t;

typedef struct drm_savage_buf_priv {
	u32 age;
} drm_savage_buf_priv_t;

/* Constants */
#define SAVAGE_MAX_USEC_TIMEOUT		100000	/* 100 ms */

				/* savage_bci.c */
extern int savage_bci_init( DRM_IOCTL_ARGS );
extern int savage_bci_buffers( DRM_IOCTL_ARGS );

extern int savage_do_cleanup_bci( drm_device_t *dev );

#define SAVAGE_RING_HIGH_MARK		128

#define SAVAGE_BASE(reg)	((unsigned long)(dev_priv->mmio->handle))
#define SAVAGE_ADDR(reg)	(SAVAGE_BASE( reg ) + reg)

#define SAVAGE_READ(reg)	DRM_READ32(  (volatile u32 *) SAVAGE_ADDR(reg) )
#define SAVAGE_WRITE(reg,val)	DRM_WRITE32( (volatile u32 *) SAVAGE_ADDR(reg), (val) )

#define SAVAGE_READ8(reg)	DRM_READ8(  (volatile u8 *) SAVAGE_ADDR(reg) )
#define SAVAGE_WRITE8(reg,val)	DRM_WRITE8( (volatile u8 *) SAVAGE_ADDR(reg), (val) )

#endif /* __SAVAGE_DRV_H__ */
