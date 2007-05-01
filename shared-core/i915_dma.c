/* i915_dma.c -- DMA support for the I915 -*- linux-c -*-
 */
/*
 * Copyright 2003 Tungsten Graphics, Inc., Cedar Park, Texas.
 * All Rights Reserved.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL TUNGSTEN GRAPHICS AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 * 
 */

#include "drmP.h"
#include "drm.h"
#include "i915_drm.h"
#include "i915_drv.h"

#define IS_I965G(dev)  (dev->pci_device == 0x2972 || \
			dev->pci_device == 0x2982 || \
			dev->pci_device == 0x2992 || \
			dev->pci_device == 0x29A2 || \
			dev->pci_device == 0x2A02)


/* Really want an OS-independent resettable timer.  Would like to have
 * this loop run for (eg) 3 sec, but have the timer reset every time
 * the head pointer changes, so that EBUSY only happens if the ring
 * actually stalls for (eg) 3 seconds.
 */
int i915_wait_ring(drm_i915_private_t *dev_priv, drm_i915_ring_buffer_t *ring,
		   int n, const char *caller)
{
	u32 last_head = I915_READ(ring->reg + RING_HEAD) & HEAD_ADDR;
	int i;

	for (i = 0; i < 10000; i++) {
		ring->head = I915_READ(ring->reg + RING_HEAD) & HEAD_ADDR;
		ring->space = ring->head - (ring->tail + 8);
		if (ring->space < 0)
			ring->space += ring->Size;
		if (ring->space >= n)
			return 0;

		dev_priv->sarea_priv->perf_boxes |= I915_BOX_WAIT;

		if (ring->head != last_head)
			i = 0;

		last_head = ring->head;
		DRM_UDELAY(1);
	}

	return DRM_ERR(EBUSY);
}

void i915_kernel_lost_context(drm_i915_private_t * dev_priv,
			      drm_i915_ring_buffer_t *ring)
{
	ring->head = I915_READ(ring->reg + RING_HEAD) & HEAD_ADDR;
	ring->tail = I915_READ(ring->reg + RING_TAIL) & TAIL_ADDR;
	ring->space = ring->head - (ring->tail + 8);
	if (ring->space < 0)
		ring->space += ring->Size;

	if (ring->head == ring->tail)
		dev_priv->sarea_priv->perf_boxes |= I915_BOX_RING_EMPTY;
}

static int i915_dma_cleanup(drm_device_t * dev)
{
	/* Make sure interrupts are disabled here because the uninstall ioctl
	 * may not have been called from userspace and after dev_private
	 * is freed, it's too late.
	 */
	if (dev->irq)
		drm_irq_uninstall(dev);

	if (dev->dev_private) {
		drm_i915_private_t *dev_priv =
		    (drm_i915_private_t *) dev->dev_private;

		if (dev_priv->ring.virtual_start) {
			drm_core_ioremapfree(&dev_priv->ring.map, dev);
		}
		if (dev_priv->hwb_ring.virtual_start) {
			drm_core_ioremapfree(&dev_priv->hwb_ring.map, dev);
		}
		if (dev_priv->hwz_ring.virtual_start) {
			drm_core_ioremapfree(&dev_priv->hwz_ring.map, dev);
		}

		if (dev_priv->status_page_dmah) {
			drm_pci_free(dev, dev_priv->status_page_dmah);
			/* Need to rewrite hardware status page */
			I915_WRITE(0x02080, 0x1ffff000);
		}

		drm_free(dev->dev_private, sizeof(drm_i915_private_t),
			 DRM_MEM_DRIVER);

		dev->dev_private = NULL;
	}

	return 0;
}

static int i915_init_ring(drm_device_t * dev, drm_i915_ring_buffer_t * ring,
			  unsigned start, unsigned end, unsigned size, u32 reg)
{
	ring->Start = start;
	ring->End = end;
	ring->Size = size;
	ring->tail_mask = ring->Size - 1;

	ring->map.offset = start;
	ring->map.size = size;
	ring->map.type = 0;
	ring->map.flags = 0;
	ring->map.mtrr = 0;

	drm_core_ioremap(&ring->map, dev);

	if (ring->map.handle == NULL) {
		DRM_ERROR("can not ioremap virtual address for ring buffer\n");
		return DRM_ERR(ENOMEM);
	}

	ring->virtual_start = ring->map.handle;
	ring->reg = reg;

	return 0;
}

static int i915_initialize(drm_device_t * dev,
			   drm_i915_private_t * dev_priv,
			   drm_i915_init_t * init)
{
	memset(dev_priv, 0, sizeof(drm_i915_private_t));

	dev_priv->sarea = drm_getsarea(dev);
	if (!dev_priv->sarea) {
		DRM_ERROR("can not find sarea!\n");
		dev->dev_private = (void *)dev_priv;
		i915_dma_cleanup(dev);
		return DRM_ERR(EINVAL);
	}

	dev_priv->mmio_map = drm_core_findmap(dev, init->mmio_offset);
	if (!dev_priv->mmio_map) {
		dev->dev_private = (void *)dev_priv;
		i915_dma_cleanup(dev);
		DRM_ERROR("can not find mmio map!\n");
		return DRM_ERR(EINVAL);
	}

	dev_priv->sarea_priv = (drm_i915_sarea_t *)
	    ((u8 *) dev_priv->sarea->handle + init->sarea_priv_offset);

	if (i915_init_ring(dev, &dev_priv->ring, init->ring_start,
			   init->ring_end, init->ring_size, LP_RING)) {
		dev->dev_private = (void *)dev_priv;
		i915_dma_cleanup(dev);
		DRM_ERROR("Failed to initialize LP ring buffer\n");
		return DRM_ERR(ENOMEM);
	}

	dev_priv->cpp = init->cpp;
	dev_priv->sarea_priv->pf_current_page = 0;

	/* We are using separate values as placeholders for mechanisms for
	 * private backbuffer/depthbuffer usage.
	 */
	dev_priv->use_mi_batchbuffer_start = 0;

	/* Allow hardware batchbuffers unless told otherwise.
	 */
	dev_priv->allow_batchbuffer = 1;

	/* Program Hardware Status Page */
	dev_priv->status_page_dmah = drm_pci_alloc(dev, PAGE_SIZE, PAGE_SIZE, 
	    0xffffffff);

	if (!dev_priv->status_page_dmah) {
		dev->dev_private = (void *)dev_priv;
		i915_dma_cleanup(dev);
		DRM_ERROR("Can not allocate hardware status page\n");
		return DRM_ERR(ENOMEM);
	}
	dev_priv->hw_status_page = dev_priv->status_page_dmah->vaddr;
	dev_priv->dma_status_page = dev_priv->status_page_dmah->busaddr;
	
	memset(dev_priv->hw_status_page, 0, PAGE_SIZE);
	DRM_DEBUG("hw status page @ %p\n", dev_priv->hw_status_page);

	I915_WRITE(0x02080, dev_priv->dma_status_page);
	DRM_DEBUG("Enabled hardware status page\n");
	dev->dev_private = (void *)dev_priv;
	return 0;
}

static int i915_dma_resume(drm_device_t * dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;

	DRM_DEBUG("%s\n", __FUNCTION__);

	if (!dev_priv->sarea) {
		DRM_ERROR("can not find sarea!\n");
		return DRM_ERR(EINVAL);
	}

	if (!dev_priv->mmio_map) {
		DRM_ERROR("can not find mmio map!\n");
		return DRM_ERR(EINVAL);
	}

	if (dev_priv->ring.map.handle == NULL) {
		DRM_ERROR("can not ioremap virtual address for"
			  " ring buffer\n");
		return DRM_ERR(ENOMEM);
	}

	/* Program Hardware Status Page */
	if (!dev_priv->hw_status_page) {
		DRM_ERROR("Can not find hardware status page\n");
		return DRM_ERR(EINVAL);
	}
	DRM_DEBUG("hw status page @ %p\n", dev_priv->hw_status_page);

	I915_WRITE(0x02080, dev_priv->dma_status_page);
	DRM_DEBUG("Enabled hardware status page\n");

	return 0;
}

static int i915_dma_init(DRM_IOCTL_ARGS)
{
	DRM_DEVICE;
	drm_i915_private_t *dev_priv;
	drm_i915_init_t init;
	int retcode = 0;

	DRM_COPY_FROM_USER_IOCTL(init, (drm_i915_init_t __user *) data,
				 sizeof(init));

	switch (init.func) {
	case I915_INIT_DMA:
		dev_priv = drm_alloc(sizeof(drm_i915_private_t),
				     DRM_MEM_DRIVER);
		if (dev_priv == NULL)
			return DRM_ERR(ENOMEM);
		retcode = i915_initialize(dev, dev_priv, &init);
		break;
	case I915_CLEANUP_DMA:
		retcode = i915_dma_cleanup(dev);
		break;
	case I915_RESUME_DMA:
		retcode = i915_dma_resume(dev);
		break;
	default:
		retcode = DRM_ERR(EINVAL);
		break;
	}

	return retcode;
}

/* Implement basically the same security restrictions as hardware does
 * for MI_BATCH_NON_SECURE.  These can be made stricter at any time.
 *
 * Most of the calculations below involve calculating the size of a
 * particular instruction.  It's important to get the size right as
 * that tells us where the next instruction to check is.  Any illegal
 * instruction detected will be given a size of zero, which is a
 * signal to abort the rest of the buffer.
 */
static int do_validate_cmd(int cmd)
{
	switch (((cmd >> 29) & 0x7)) {
	case 0x0:
		switch ((cmd >> 23) & 0x3f) {
		case 0x0:
			return 1;	/* MI_NOOP */
		case 0x4:
			return 1;	/* MI_FLUSH */
		default:
			return 0;	/* disallow everything else */
		}
		break;
	case 0x1:
		return 0;	/* reserved */
	case 0x2:
		return (cmd & 0xff) + 2;	/* 2d commands */
	case 0x3:
		if (((cmd >> 24) & 0x1f) <= 0x18)
			return 1;

		switch ((cmd >> 24) & 0x1f) {
		case 0x1c:
			return 1;
		case 0x1d:
			switch ((cmd >> 16) & 0xff) {
			case 0x3:
				return (cmd & 0x1f) + 2;
			case 0x4:
				return (cmd & 0xf) + 2;
			default:
				return (cmd & 0xffff) + 2;
			}
		case 0x1e:
			if (cmd & (1 << 23))
				return (cmd & 0xffff) + 1;
			else
				return 1;
		case 0x1f:
			if ((cmd & (1 << 23)) == 0)	/* inline vertices */
				return (cmd & 0x1ffff) + 2;
			else if (cmd & (1 << 17))	/* indirect random */
				if ((cmd & 0xffff) == 0)
					return 0;	/* unknown length, too hard */
				else
					return (((cmd & 0xffff) + 1) / 2) + 1;
			else
				return 2;	/* indirect sequential */
		default:
			return 0;
		}
	default:
		return 0;
	}

	return 0;
}

static int validate_cmd(int cmd)
{
	int ret = do_validate_cmd(cmd);

/* 	printk("validate_cmd( %x ): %d\n", cmd, ret); */

	return ret;
}

static int i915_emit_cmds(drm_device_t * dev, int __user * buffer, int dwords)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	int i;
	RING_LOCALS;

	if ((dwords+1) * sizeof(int) >= dev_priv->ring.Size - 8)
		return DRM_ERR(EINVAL);

	BEGIN_RING(&dev_priv->ring, (dwords+1)&~1);

	for (i = 0; i < dwords;) {
		int cmd, sz;

		if (DRM_COPY_FROM_USER_UNCHECKED(&cmd, &buffer[i], sizeof(cmd)))
			return DRM_ERR(EINVAL);

		if ((sz = validate_cmd(cmd)) == 0 || i + sz > dwords)
			return DRM_ERR(EINVAL);

		OUT_RING(cmd);

		while (++i, --sz) {
			if (DRM_COPY_FROM_USER_UNCHECKED(&cmd, &buffer[i],
							 sizeof(cmd))) {
				return DRM_ERR(EINVAL);
			}
			OUT_RING(cmd);
		}
	}
		
	if (dwords & 1)
		OUT_RING(0);

	ADVANCE_RING();
		
	return 0;
}

static int i915_emit_box(drm_device_t * dev,
			 drm_clip_rect_t __user * boxes,
			 int i, int DR1, int DR4)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_clip_rect_t box;
	RING_LOCALS;

	if (DRM_COPY_FROM_USER_UNCHECKED(&box, &boxes[i], sizeof(box))) {
		return DRM_ERR(EFAULT);
	}

	if (box.y2 <= box.y1 || box.x2 <= box.x1 || box.y2 <= 0 || box.x2 <= 0) {
		DRM_ERROR("Bad box %d,%d..%d,%d\n",
			  box.x1, box.y1, box.x2, box.y2);
		return DRM_ERR(EINVAL);
	}

	if (IS_I965G(dev)) {
		BEGIN_RING(&dev_priv->ring, 4);
		OUT_RING(GFX_OP_DRAWRECT_INFO_I965);
		OUT_RING((box.x1 & 0xffff) | (box.y1 << 16));
		OUT_RING(((box.x2 - 1) & 0xffff) | ((box.y2 - 1) << 16));
		OUT_RING(DR4);
		ADVANCE_RING();
	} else {
		BEGIN_RING(&dev_priv->ring, 6);
		OUT_RING(GFX_OP_DRAWRECT_INFO);
		OUT_RING(DR1);
		OUT_RING((box.x1 & 0xffff) | (box.y1 << 16));
		OUT_RING(((box.x2 - 1) & 0xffff) | ((box.y2 - 1) << 16));
		OUT_RING(DR4);
		OUT_RING(0);
		ADVANCE_RING();
	}

	return 0;
}

/* XXX: Emitting the counter should really be moved to part of the IRQ
 * emit.  For now, do it in both places:
 */

void i915_emit_breadcrumb(drm_device_t *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	RING_LOCALS;

	dev_priv->sarea_priv->last_enqueue = ++dev_priv->counter;

	if (dev_priv->counter > 0x7FFFFFFFUL)
		 dev_priv->sarea_priv->last_enqueue = dev_priv->counter = 1;

	BEGIN_RING(&dev_priv->ring, 4);
	OUT_RING(CMD_STORE_DWORD_IDX);
	OUT_RING(20);
	OUT_RING(dev_priv->counter);
	OUT_RING(0);
	ADVANCE_RING();
}


int i915_emit_mi_flush(drm_device_t *dev, uint32_t flush)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	uint32_t flush_cmd = CMD_MI_FLUSH;
	RING_LOCALS;

	flush_cmd |= flush;

	i915_kernel_lost_context(dev_priv, &dev_priv->ring);

	BEGIN_RING(&dev_priv->ring, 4);
	OUT_RING(flush_cmd);
	OUT_RING(0);
	OUT_RING(0);
	OUT_RING(0);
	ADVANCE_RING();

	return 0;
}


static int i915_dispatch_cmdbuffer(drm_device_t * dev,
				   drm_i915_cmdbuffer_t * cmd)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	int nbox = cmd->num_cliprects;
	int i = 0, count, ret;

	if (cmd->sz & 0x3) {
		DRM_ERROR("alignment");
		return DRM_ERR(EINVAL);
	}

	i915_kernel_lost_context(dev_priv, &dev_priv->ring);

	count = nbox ? nbox : 1;

	for (i = 0; i < count; i++) {
		if (i < nbox) {
			ret = i915_emit_box(dev, cmd->cliprects, i,
					    cmd->DR1, cmd->DR4);
			if (ret)
				return ret;
		}

		ret = i915_emit_cmds(dev, (int __user *)cmd->buf, cmd->sz / 4);
		if (ret)
			return ret;
	}

	i915_emit_breadcrumb( dev );
#ifdef I915_HAVE_FENCE
	drm_fence_flush_old(dev, 0, dev_priv->counter);
#endif
	return 0;
}

static int i915_dispatch_batchbuffer(drm_device_t * dev,
				     drm_i915_batchbuffer_t * batch)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_clip_rect_t __user *boxes = batch->cliprects;
	int nbox = batch->num_cliprects;
	int i = 0, count;
	RING_LOCALS;

	if ((batch->start | batch->used) & 0x7) {
		DRM_ERROR("alignment");
		return DRM_ERR(EINVAL);
	}

	i915_kernel_lost_context(dev_priv, &dev_priv->ring);

	count = nbox ? nbox : 1;

	for (i = 0; i < count; i++) {
		if (i < nbox) {
			int ret = i915_emit_box(dev, boxes, i,
						batch->DR1, batch->DR4);
			if (ret)
				return ret;
		}

		if (dev_priv->use_mi_batchbuffer_start) {
			BEGIN_RING(&dev_priv->ring, 2);
			OUT_RING(MI_BATCH_BUFFER_START | (2 << 6));
			OUT_RING(batch->start | MI_BATCH_NON_SECURE);
			ADVANCE_RING();
		} else {
			BEGIN_RING(&dev_priv->ring, 4);
			OUT_RING(MI_BATCH_BUFFER);
			OUT_RING(batch->start | MI_BATCH_NON_SECURE);
			OUT_RING(batch->start + batch->used - 4);
			OUT_RING(0);
			ADVANCE_RING();
		}
	}

	i915_emit_breadcrumb( dev );
#ifdef I915_HAVE_FENCE
	drm_fence_flush_old(dev, 0, dev_priv->counter);
#endif
	return 0;
}

static void i915_do_dispatch_flip(drm_device_t * dev, int pipe, int sync)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	u32 num_pages, current_page, next_page, dspbase;
	int shift = 2 * pipe, x, y;
	RING_LOCALS;

	/* Calculate display base offset */
	num_pages = dev_priv->sarea_priv->third_handle ? 3 : 2;
	current_page = (dev_priv->sarea_priv->pf_current_page >> shift) & 0x3;
	next_page = (current_page + 1) % num_pages;

	switch (next_page) {
	default:
	case 0:
		dspbase = dev_priv->sarea_priv->front_offset;
		break;
	case 1:
		dspbase = dev_priv->sarea_priv->back_offset;
		break;
	case 2:
		dspbase = dev_priv->sarea_priv->third_offset;
		break;
	}

	if (pipe == 0) {
		x = dev_priv->sarea_priv->pipeA_x;
		y = dev_priv->sarea_priv->pipeA_y;
	} else {
		x = dev_priv->sarea_priv->pipeB_x;
		y = dev_priv->sarea_priv->pipeB_y;
	}

	dspbase += (y * dev_priv->sarea_priv->pitch + x) * dev_priv->cpp;

	DRM_DEBUG("pipe=%d current_page=%d dspbase=0x%x\n", pipe, current_page,
		  dspbase);

	BEGIN_RING(&dev_priv->ring, 4);
	OUT_RING(sync ? 0 :
		 (MI_WAIT_FOR_EVENT | (pipe ? MI_WAIT_FOR_PLANE_B_FLIP :
				       MI_WAIT_FOR_PLANE_A_FLIP)));
	OUT_RING(CMD_OP_DISPLAYBUFFER_INFO | (sync ? 0 : ASYNC_FLIP) |
		 (pipe ? DISPLAY_PLANE_B : DISPLAY_PLANE_A));
	OUT_RING(dev_priv->sarea_priv->pitch * dev_priv->cpp);
	OUT_RING(dspbase);
	ADVANCE_RING();

	dev_priv->sarea_priv->pf_current_page &= ~(0x3 << shift);
	dev_priv->sarea_priv->pf_current_page |= next_page << shift;
}

void i915_dispatch_flip(drm_device_t * dev, int pipes, int sync)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	int i;

	DRM_DEBUG("%s: pipes=0x%x pfCurrentPage=%d\n",
		  __FUNCTION__,
		  pipes, dev_priv->sarea_priv->pf_current_page);

	i915_emit_mi_flush(dev, MI_READ_FLUSH | MI_EXE_FLUSH);

	for (i = 0; i < 2; i++)
		if (pipes & (1 << i))
			i915_do_dispatch_flip(dev, i, sync);

	i915_emit_breadcrumb(dev);
#ifdef I915_HAVE_FENCE
	if (!sync)
		drm_fence_flush_old(dev, 0, dev_priv->counter);
#endif
}

static int i915_quiescent(drm_device_t * dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;

	i915_kernel_lost_context(dev_priv, &dev_priv->ring);
	return i915_wait_ring(dev_priv, &dev_priv->ring, dev_priv->ring.Size - 8,
			      __FUNCTION__);
}

static int i915_flush_ioctl(DRM_IOCTL_ARGS)
{
	DRM_DEVICE;

	LOCK_TEST_WITH_RETURN(dev, filp);

	return i915_quiescent(dev);
}

static int i915_batchbuffer(DRM_IOCTL_ARGS)
{
	DRM_DEVICE;
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	drm_i915_sarea_t *sarea_priv = (drm_i915_sarea_t *)
	    dev_priv->sarea_priv;
	drm_i915_batchbuffer_t batch;
	int ret;

	if (!dev_priv->allow_batchbuffer) {
		DRM_ERROR("Batchbuffer ioctl disabled\n");
		return DRM_ERR(EINVAL);
	}

	DRM_COPY_FROM_USER_IOCTL(batch, (drm_i915_batchbuffer_t __user *) data,
				 sizeof(batch));

	DRM_DEBUG("i915 batchbuffer, start %x used %d cliprects %d\n",
		  batch.start, batch.used, batch.num_cliprects);

	LOCK_TEST_WITH_RETURN(dev, filp);

	if (batch.num_cliprects && DRM_VERIFYAREA_READ(batch.cliprects,
						       batch.num_cliprects *
						       sizeof(drm_clip_rect_t)))
		return DRM_ERR(EFAULT);

	ret = i915_dispatch_batchbuffer(dev, &batch);

	sarea_priv->last_dispatch = READ_BREADCRUMB(dev_priv);
	return ret;
}

static int i915_cmdbuffer(DRM_IOCTL_ARGS)
{
	DRM_DEVICE;
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	drm_i915_sarea_t *sarea_priv = (drm_i915_sarea_t *)
	    dev_priv->sarea_priv;
	drm_i915_cmdbuffer_t cmdbuf;
	int ret;

	DRM_COPY_FROM_USER_IOCTL(cmdbuf, (drm_i915_cmdbuffer_t __user *) data,
				 sizeof(cmdbuf));

	DRM_DEBUG("i915 cmdbuffer, buf %p sz %d cliprects %d\n",
		  cmdbuf.buf, cmdbuf.sz, cmdbuf.num_cliprects);

	LOCK_TEST_WITH_RETURN(dev, filp);

	if (cmdbuf.num_cliprects &&
	    DRM_VERIFYAREA_READ(cmdbuf.cliprects,
				cmdbuf.num_cliprects *
				sizeof(drm_clip_rect_t))) {
		DRM_ERROR("Fault accessing cliprects\n");
		return DRM_ERR(EFAULT);
	}

	ret = i915_dispatch_cmdbuffer(dev, &cmdbuf);
	if (ret) {
		DRM_ERROR("i915_dispatch_cmdbuffer failed\n");
		return ret;
	}

	sarea_priv->last_dispatch = READ_BREADCRUMB(dev_priv);
	return 0;
}

#define BIN_WIDTH 64
#define BIN_HEIGHT 32
#define BIN_HMASK ~(BIN_HEIGHT - 1)
#define BIN_WMASK ~(BIN_WIDTH - 1)

#define BMP_SIZE PAGE_SIZE
#define BMP_POOL_SIZE ((BMP_SIZE - 32) / 4)

static int i915_bmp_alloc(drm_device_t *dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	int i;
	u32 *ring;

	dev_priv->bmp = drm_pci_alloc(dev, BMP_SIZE, PAGE_SIZE, 0xffffffff);

	if (!dev_priv->bmp) {
		DRM_ERROR("Failed to allocate BMP ring buffer\n");
		return DRM_ERR(ENOMEM);
	}

	dev_priv->bmp_pool = drm_calloc(1, BMP_POOL_SIZE *
					sizeof(drm_dma_handle_t*),
					DRM_MEM_DRIVER);

	if (!dev_priv->bmp_pool) {
		DRM_ERROR("Failed to allocate BMP pool\n");
		drm_pci_free(dev, dev_priv->bmp);
		dev_priv->bmp = NULL;
		return DRM_ERR(ENOMEM);
	}

	for (i = 0, ring = dev_priv->bmp->vaddr; i < BMP_POOL_SIZE; i++) {
		dev_priv->bmp_pool[i] = drm_pci_alloc(dev, PAGE_SIZE, PAGE_SIZE,
						      0xffffffff);

		if (!dev_priv->bmp_pool[i]) {
			DRM_INFO("Failed to allocate page %d for BMP pool\n",
				 i);
			break;
		}

		ring[i] = dev_priv->bmp_pool[i]->busaddr /*>> PAGE_SHIFT*/;
	}

	I915_WRITE(BMP_PUT, (i / 8) << BMP_OFFSET_SHIFT);
	I915_WRITE(BMP_GET, 0 << BMP_OFFSET_SHIFT);

	I915_WRITE(BMP_BUFFER, dev_priv->bmp->busaddr | BMP_PAGE_SIZE_4K |
		   ((BMP_SIZE / PAGE_SIZE - 1) << BMP_BUFFER_SIZE_SHIFT) |
		   BMP_ENABLE);

	DRM_INFO("BMP allocated and initialized\n");

	return 0;
}

static void i915_bmp_free(drm_device_t *dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;

	if (dev_priv->bmp_pool) {
		int i;

		for (i = 0; i < BMP_POOL_SIZE; i++) {
			if (!dev_priv->bmp_pool[i])
				break;

			drm_pci_free(dev, dev_priv->bmp_pool[i]);
		}

		drm_free(dev_priv->bmp_pool, BMP_POOL_SIZE *
			 sizeof(drm_dma_handle_t*), DRM_MEM_DRIVER);
		dev_priv->bmp_pool = NULL;
	}

	if (dev_priv->bmp) {
		drm_pci_free(dev, dev_priv->bmp);
		dev_priv->bmp = NULL;
	}

	DRM_INFO("BMP freed\n");
}

#define VIRTUAL_BPL 0
#define BPL_ALIGN (16 * 1024)

static int i915_bpl_alloc(drm_device_t *dev, int bin_pitch, int bin_rows)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	int i, bpl_size = (8 * bin_rows * bin_pitch + PAGE_SIZE - 1) &
		PAGE_MASK;

	if (bin_pitch <= 0 || bin_rows <= 0) {
		DRM_ERROR("Invalid bin pitch=%d rows=%d\n", bin_pitch, bin_rows);
		return DRM_ERR(EINVAL);
	}

	/* drm_pci_alloc can't handle alignment > size */
	if (bpl_size < BPL_ALIGN)
		bpl_size = BPL_ALIGN;

#if VIRTUAL_BPL
	DRM_INFO("HWZ ring offset=0x%lx handle=%p\n",
		 dev_priv->hwz_ring.map.offset, dev_priv->hwz_ring.map.handle);
#endif

	for (i = 0; i < dev_priv->num_bpls; i++) {
		if (dev_priv->bpl[i])
			continue;
#if VIRTUAL_BPL
		dev_priv->bpl[i] = drm_calloc(1, sizeof(*dev_priv->bpl),
					      DRM_MEM_DRIVER);
#else
		dev_priv->bpl[i] = drm_pci_alloc(dev, bpl_size, BPL_ALIGN,
						 0xffffffff);
#endif
		if (!dev_priv->bpl[i]) {
			DRM_ERROR("Failed to allocate BPL %d\n", i);
			return DRM_ERR(ENOMEM);
		}
#if VIRTUAL_BPL
		dev_priv->bpl[i]->busaddr = (dev_priv->hwz_ring.map.offset -
					     dev->agp->agp_info.aper_base +
					     i * bpl_size + BPL_ALIGN - 1) &
			~(BPL_ALIGN - 1);

		if (dev_priv->bpl[i]->busaddr & (0x7 << 29)) {
			DRM_ERROR("BPL %d bus address 0x%x high bits not 0\n",
				  i, dev_priv->bpl[i]->busaddr);
			drm_free(dev_priv->bpl[i], sizeof(*dev_priv->bpl),
				 DRM_MEM_DRIVER);
			dev_priv->bpl[i] = NULL;
			return DRM_ERR(ENOMEM);
		}

		dev_priv->bpl[i]->vaddr =
			(void*)(((unsigned long)dev_priv->hwz_ring.map.handle +
				 i * bpl_size + BPL_ALIGN - 1) & ~(BPL_ALIGN - 1));

		DRM_INFO("BPL %d busaddr=0x%x vaddr=%p\n", i,
			 dev_priv->bpl[i]->busaddr, dev_priv->bpl[i]->vaddr);
#endif
	}

	DRM_INFO("Allocated %d BPLs of %d bytes\n", dev_priv->num_bpls,
		 bpl_size);

	return 0;
}

static void i915_bpl_free(drm_device_t *dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	int i;

	for (i = 0; i < 3; i++) {
		if (!dev_priv->bpl[i])
			return;

#if VIRTUAL_BPL
		drm_free(dev_priv->bpl[i], sizeof(*dev_priv->bpl), DRM_MEM_DRIVER);
#else
		drm_pci_free(dev, dev_priv->bpl[i]);
#endif

		dev_priv->bpl[i] = NULL;
	}
}

static int i915_hwb_idle(drm_i915_private_t *dev_priv, unsigned bpl_num)
{
	int i, ret = 0;

	if (i915_wait_ring(dev_priv, &dev_priv->hwb_ring,
			   dev_priv->hwb_ring.Size - 8,  __FUNCTION__)) {
		DRM_INFO("Timeout waiting for HWB ring to go idle"
			 ", PRB head: %x tail: %x/%x HWB head: %x tail: %x/%x\n",
			 I915_READ(LP_RING + RING_HEAD) & HEAD_ADDR,
			 I915_READ(LP_RING + RING_TAIL) & HEAD_ADDR,
			 dev_priv->ring.tail,
			 I915_READ(HWB_RING + RING_HEAD) & HEAD_ADDR,
			 I915_READ(HWB_RING + RING_TAIL) & HEAD_ADDR,
			 dev_priv->hwb_ring.tail);
		DRM_INFO("ESR: 0x%x DMA_FADD_S: 0x%x IPEIR: 0x%x SCPD0: 0x%x "
			 "IIR: 0x%x\n", I915_READ(ESR), I915_READ(DMA_FADD_S),
			 I915_READ(IPEIR), I915_READ(SCPD0),
			 I915_READ(I915REG_INT_IDENTITY_R));
		DRM_INFO("BCPD: 0x%x BMCD: 0x%x BDCD: 0x%x BPCD: 0x%x\n"
			 "BINSCENE: 0x%x BINSKPD: 0x%x HWBSKPD: 0x%x\n", I915_READ(BCPD),
			 I915_READ(BMCD), I915_READ(BDCD), I915_READ(BPCD),
			 I915_READ(BINSCENE), I915_READ(BINSKPD), I915_READ(HWBSKPD));

		ret = DRM_ERR(EBUSY);
	}

	if (!dev_priv->preamble_inited[bpl_num])
		return ret;

	for (i = 0; i < dev_priv->num_bins; i++) {
		u32 *bin;
		int k;

		if (!dev_priv->bins[bpl_num] || !dev_priv->bins[bpl_num][i])
			continue;

		bin = dev_priv->bins[bpl_num][i]->vaddr;

		for (k = 6; k < 1024; k++) {
			if (bin[k]) {
				int j;

				DRM_INFO("BPL %d bin %d busaddr=0x%x contents:\n",
					 bpl_num, i,
					 dev_priv->bins[bpl_num][i]->busaddr);

				for (j = 0; j < 128; j++) {
					u32 *data = dev_priv->bins[bpl_num][i]->vaddr +
						j * 8 * 4;

					if (data[0] || data[1] || data[2] || data[3] ||
					    data[4] || data[5] || data[6] || data[7])
						DRM_INFO("%p: %8x %8x %8x %8x %8x %8x %8x %8x\n",
							 data, data[0], data[1], data[2], data[3],
							 data[4], data[5], data[6], data[7]);
				}

				break;
			}
		}
	}

	return ret;
}

static void i915_bin_free(drm_device_t *dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	int i, j;

	i915_hwb_idle(dev_priv, 0);

	for (i = 0; i < 3; i++) {
		if (!dev_priv->bins[i])
			return;

		for (j = 0; j < dev_priv->num_bins; j++)
			drm_pci_free(dev, dev_priv->bins[i][j]);

		drm_free(dev_priv->bins[i], dev_priv->num_bins *
			 sizeof(drm_dma_handle_t*), DRM_MEM_DRIVER);
		dev_priv->bins[i] = NULL;
	}
}

static int i915_bin_alloc(drm_device_t *dev, int bins)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	int i, j;

	i915_bin_free(dev);

	for (i = 0; i < dev_priv->num_bpls; i++) {
		dev_priv->bins[i] = drm_calloc(1, bins *
					       sizeof(drm_dma_handle_t*),
					       DRM_MEM_DRIVER);

		if (!dev_priv->bins[i]) {
			DRM_ERROR("Failed to allocate bin pool %d\n", i);
			return DRM_ERR(ENOMEM);
		}

		for (j = 0; j < bins; j++) {
			dev_priv->bins[i][j] = drm_pci_alloc(dev, PAGE_SIZE,
							     PAGE_SIZE,
							     0xffffffff);

			if (!dev_priv->bins[i][j]) {
				DRM_ERROR("Failed to allocate page for bin %d "
					  "of buffer %d\n", j, i);
				return DRM_ERR(ENOMEM);
			}
		}

		dev_priv->preamble_inited[i] = FALSE;
	}

	dev_priv->num_bins = bins;

	DRM_INFO("Allocated %d times %d bins\n", dev_priv->num_bpls, bins);

	return 0;
}

static int i915_hwz_alloc(drm_device_t *dev, struct drm_i915_hwz_alloc *alloc)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	int bin_rows = ((((alloc->y2 + BIN_HEIGHT - 1) & BIN_HMASK) -
			 (alloc->y1 & BIN_HMASK)) / BIN_HEIGHT + 3) & ~3;
	int bin_cols = (((alloc->x2 + BIN_WIDTH - 1) & BIN_WMASK) -
			(alloc->x1 & BIN_WMASK)) / BIN_WIDTH;
	int ret;

	if (!dev_priv->bmp) {
		DRM_DEBUG("HWZ not initialized\n");
		return DRM_ERR(EINVAL);
	}

	if (alloc->num_buffers > 3) {
		DRM_ERROR("Only up to 3 buffers allowed\n");
		return DRM_ERR(EINVAL);
	}

	dev_priv->num_bpls = alloc->num_buffers;

	ret = i915_bpl_alloc(dev, bin_cols, bin_rows);

	if (ret) {
		DRM_ERROR("Failed to allocate BPLs\n");
		return ret;
	}

	ret = i915_bin_alloc(dev, bin_cols * bin_rows);

	if (ret) {
		DRM_ERROR("Failed to allocate bins\n");
		return ret;
	}

	dev_priv->bin_x1 = alloc->x1;
	dev_priv->bin_x2 = alloc->x2;
	dev_priv->bin_cols = bin_cols;
	dev_priv->bin_pitch = alloc->pitch;
	dev_priv->bin_y1 = alloc->y1;
	dev_priv->bin_y2 = alloc->y2;
	dev_priv->bin_rows = bin_rows;

	return 0;
}

static int i915_hwz_free(drm_device_t *dev)
{
	i915_bin_free(dev);
	i915_bpl_free(dev);
	i915_bmp_free(dev);

	return 0;
}

static int i915_bin_init(drm_device_t *dev, int i, u32 DR1, u32 DR4)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	int bpl_row;
	drm_dma_handle_t **bins = dev_priv->bins[i];
	unsigned preamble_inited = dev_priv->preamble_inited[i];

	if (!bins) {
		DRM_ERROR("Bins not allocated\n");
		return DRM_ERR(EINVAL);
	}

	for (bpl_row = 0; bpl_row < dev_priv->bin_rows; bpl_row += 4) {
		int bpl_col;

		for (bpl_col = 0; bpl_col < dev_priv->bin_cols; bpl_col++) {
			u32 *bpl = (u32*)dev_priv->bpl[i]->vaddr +
				2 * (bpl_row * dev_priv->bin_cols / BIN_WIDTH +
				     4 * bpl_col);
			int j, num_bpls = dev_priv->bin_rows - bpl_row;

			if (num_bpls > 4)
				num_bpls = 4;

			DRM_DEBUG("bpl_row=%d bpl_col=%d vaddr=%p => bpl=%p num_bpls = %d\n",
				  bpl_row, bpl_col, dev_priv->bpl[i]->vaddr, bpl, num_bpls);

			for (j = 0; j < num_bpls; j++) {
				unsigned idx = (bpl_row + j) *
					dev_priv->bin_cols + bpl_col;
				drm_dma_handle_t *bin = bins[idx];

				DRM_DEBUG("j=%d => idx=%u bpl=%p busaddr=0x%x\n",
					  j, idx, bpl, bin->busaddr);

				*bpl++ = bin->busaddr + 20;
				*bpl++ = 1 << 2 | 1 << 0;

				if (!preamble_inited) {
					u32 *pre = bin->vaddr;
					u32 ymin = max(dev_priv->bin_y1,
						       (dev_priv->bin_y1 +
						       (bpl_row + j) *
							BIN_HEIGHT) & BIN_HMASK);
					u32 xmin = max(dev_priv->bin_x1,
						       (dev_priv->bin_x1 +
							bpl_col * BIN_WIDTH) &
						       BIN_WMASK);
					u32 ymax = min(dev_priv->bin_y2 - 1,
						       ((ymin + BIN_HEIGHT) &
							BIN_HMASK) - 1);
					u32 xmax = min(dev_priv->bin_x2 - 1,
						       ((xmin + BIN_WIDTH) &
							BIN_WMASK) - 1);

					*pre++ = GFX_OP_DRAWRECT_INFO;
					*pre++ = DR1;
					*pre++ = ymin << 16 | xmin;
					*pre++ = ymax << 16 | xmax;
					*pre++ = DR4;
					*pre++ = MI_BATCH_BUFFER_END;
				}
			}
		}
	}

	dev_priv->preamble_inited[i] = TRUE;

	flush_agp_cache();

	I915_WRITE(GFX_FLSH_CNTL, 1);

	DRM_INFO("BPL %d initialized for %d bins\n", i, dev_priv->bin_rows *
		 dev_priv->bin_cols);

	return 0;
}

static int i915_hwz_render(drm_device_t *dev, struct drm_i915_hwz_render *render)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	int ret, i;
	u32 cache_mode_0 = I915_READ(Cache_Mode_0);
	RING_LOCALS;

	DRM_INFO("i915 hwz render, bpl_num = %d, batch_start = 0x%x\n",
		 render->bpl_num, render->batch_start);

	if (dev_priv->hwb_ring.tail != (I915_READ(HWB_RING + RING_TAIL)
					& TAIL_ADDR)) {
		DRM_INFO("Refreshing contexts of HWZ ring buffers\n");
		i915_kernel_lost_context(dev_priv, &dev_priv->hwb_ring);
		i915_kernel_lost_context(dev_priv, &dev_priv->hwz_ring);
	}

	if (i915_hwb_idle(dev_priv, render->bpl_num)) {
		return DRM_ERR(EBUSY);
	}

	ret = i915_bin_init(dev, render->bpl_num, render->DR1, render->DR4);

	if (ret) {
		DRM_ERROR("Failed to initialize  BPL %d\n", render->bpl_num);
		return ret;
	}

	/* Write the HWB command stream */
	I915_WRITE(BINSCENE, (dev_priv->bin_rows - 1) << 16 |
		   (dev_priv->bin_cols - 1) << 10 | BS_MASK);
#if VIRTUAL_BPL
	I915_WRITE(BINSKPD, (1<<7) | (1<<(7+16)));
#endif
	I915_WRITE(BINCTL, dev_priv->bpl[render->bpl_num]->busaddr | BC_MASK);

	BEGIN_RING(&dev_priv->hwb_ring, 16);

	OUT_RING(CMD_OP_BIN_CONTROL);
	OUT_RING(0x5 << 4 | 0x6);
	OUT_RING((dev_priv->bin_y1 & BIN_HMASK) << 16 |
		 (dev_priv->bin_x1 & BIN_WMASK));
	OUT_RING((((dev_priv->bin_y2 + BIN_HEIGHT - 1) & BIN_HMASK) - 1) << 16 |
		 (((dev_priv->bin_x2 + BIN_WIDTH - 1) & BIN_WMASK) - 1));
	OUT_RING(dev_priv->bin_y1 << 16 | dev_priv->bin_x1);
	OUT_RING((dev_priv->bin_y2 - 1) << 16 | (dev_priv->bin_x2 - 1));

	OUT_RING(GFX_OP_DRAWRECT_INFO);
	OUT_RING(render->DR1);
	OUT_RING((dev_priv->bin_y1 & BIN_HMASK) << 16 |
		 (dev_priv->bin_x1 & BIN_WMASK));
	OUT_RING((((dev_priv->bin_y2 + BIN_HEIGHT - 1) & BIN_HMASK) - 1) << 16 |
		 (((dev_priv->bin_x2 + BIN_WIDTH - 1) & BIN_WMASK) - 1));
	OUT_RING(render->DR4);

	OUT_RING(GFX_OP_DESTBUFFER_VARS);
	OUT_RING((0x8<<20) | (0x8<<16));

	OUT_RING(GFX_OP_RASTER_RULES | (1<<5) | (2<<3));

	OUT_RING(MI_BATCH_BUFFER_START | (2 << 6));
	OUT_RING(render->batch_start | MI_BATCH_NON_SECURE);

	ADVANCE_RING();

	/* Prepare the Scene Render List */
	if (render->static_state_size) {
		DRM_INFO("Emitting %d DWORDs of static indirect state\n",
			 render->static_state_size);

		BEGIN_RING(&dev_priv->ring, 4);
		OUT_RING(GFX_OP_LOAD_INDIRECT | (1<<8) | (1<<14) | 1);
		OUT_RING((render->batch_start + render->static_state_offset) |
			 (1<<1) | (1<<0));
		OUT_RING(render->static_state_size - 1);
		OUT_RING(0);
		ADVANCE_RING();
	}

	BEGIN_RING(&dev_priv->ring, 2 * dev_priv->num_bins + 8);

	OUT_RING(CMD_MI_FLUSH /*| MI_NO_WRITE_FLUSH*/);
	OUT_RING(CMD_MI_LOAD_REGISTER_IMM);
	OUT_RING(Cache_Mode_0);
	OUT_RING((cache_mode_0 & ~0x20) | 0x201);

	DRM_INFO("Setting Cache_Mode_0 to 0x%x for zone rendering\n",
		 (cache_mode_0 & ~0x20) | 0x201);

	for (i = 0; i < dev_priv->num_bins; i++) {
		OUT_RING(MI_BATCH_BUFFER_START);
		OUT_RING(dev_priv->bins[render->bpl_num][i]->busaddr);
	}

	OUT_RING(CMD_MI_FLUSH | MI_END_SCENE | MI_SCENE_COUNT);
	OUT_RING(CMD_MI_LOAD_REGISTER_IMM);
	OUT_RING(Cache_Mode_0);
	OUT_RING(cache_mode_0);

	DRM_INFO("Restoring Cache_Mode_0 to 0x%x\n", cache_mode_0);

	dev_priv->ring.tail = outring;
	dev_priv->ring.space -= outcount * 4;

	i915_hwb_idle(dev_priv, render->bpl_num);

	BEGIN_RING(&dev_priv->hwb_ring, 2);
	OUT_RING(CMD_MI_FLUSH | MI_END_SCENE | MI_SCENE_COUNT |
		 MI_NO_WRITE_FLUSH);
	OUT_RING(0);
	ADVANCE_RING();

	i915_hwb_idle(dev_priv, render->bpl_num);

	I915_WRITE(LP_RING + RING_TAIL, dev_priv->ring.tail);

	return ret;
}

static int i915_hwz_init(drm_device_t *dev, struct drm_i915_hwz_init *init)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	int ret;
	RING_LOCALS;

	if (!dev_priv) {
		DRM_ERROR("called without initialization\n");
		return DRM_ERR(EINVAL);
	}

	if (dev_priv->bmp) {
		DRM_DEBUG("Already initialized\n");
		return DRM_ERR(EBUSY);
	}

	ret = i915_init_priv1(dev);

	if (ret) {
		DRM_ERROR("Failed to initialize PRIV1 memory type\n");
		return ret;
	}

	if (i915_bmp_alloc(dev)) {
		DRM_ERROR("Failed to allocate BMP\n");
		return DRM_ERR(ENOMEM);
	}

	if (i915_init_ring(dev, &dev_priv->hwb_ring, init->hwb_start,
			   init->hwb_end, init->hwb_size, HWB_RING)) {
		DRM_ERROR("Failed to initialize HWB ring buffer\n");
		return DRM_ERR(ENOMEM);
	}

	if (i915_init_ring(dev, &dev_priv->hwz_ring, init->hwz_start,
			   init->hwz_end, init->hwz_size, HP_RING)) {
		DRM_ERROR("Failed to initialize HWZ ring buffer\n");
		return DRM_ERR(ENOMEM);
	}

	DRM_INFO("Refreshing contexts of HWZ ring buffers\n");
	i915_kernel_lost_context(dev_priv, &dev_priv->hwb_ring);
	i915_kernel_lost_context(dev_priv, &dev_priv->hwz_ring);

	I915_WRITE(BINSCENE, BS_MASK | BS_OP_LOAD);

	BEGIN_RING(&dev_priv->hwb_ring, 2);
	OUT_RING(CMD_MI_FLUSH);
	OUT_RING(0);
	ADVANCE_RING();

	DRM_INFO("HWZ initialized\n");

	return 0;
}

static int i915_hwz(DRM_IOCTL_ARGS)
{
	DRM_DEVICE;
	drm_i915_hwz_t hwz;

	if (!dev->dev_private) {
		DRM_ERROR("called with no initialization\n");
		return DRM_ERR(EINVAL);
	}

	DRM_COPY_FROM_USER_IOCTL(hwz, (drm_i915_hwz_t __user *) data,
				 sizeof(hwz));

	switch (hwz.op) {
	case DRM_I915_HWZ_INIT:
		if (!priv->master) {
			DRM_ERROR("Only master may initialize HWZ\n");
			return DRM_ERR(EINVAL);
		}
		return i915_hwz_init(dev, &hwz.init);
	case DRM_I915_HWZ_RENDER:
		LOCK_TEST_WITH_RETURN(dev, filp);
		return i915_hwz_render(dev, &hwz.render);
	case DRM_I915_HWZ_ALLOC:
		return i915_hwz_alloc(dev, &hwz.alloc);
	case DRM_I915_HWZ_FREE:
		return i915_hwz_free(dev);
	default:
		DRM_ERROR("Invalid op 0x%x\n", hwz.op);
		return DRM_ERR(EINVAL);
	}
}

static int i915_do_cleanup_pageflip(drm_device_t * dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	int i, pipes, num_pages = dev_priv->sarea_priv->third_handle ? 3 : 2;

	DRM_DEBUG("%s\n", __FUNCTION__);

	for (i = 0, pipes = 0; i < 2; i++)
		if (dev_priv->sarea_priv->pf_current_page & (0x3 << (2 * i))) {
			dev_priv->sarea_priv->pf_current_page =
				(dev_priv->sarea_priv->pf_current_page &
				 ~(0x3 << (2 * i))) | (num_pages - 1) << (2 * i);

			pipes |= 1 << i;
		}

	if (pipes)
		i915_dispatch_flip(dev, pipes, 0);

	return 0;
}

static int i915_flip_bufs(DRM_IOCTL_ARGS)
{
	DRM_DEVICE;
	drm_i915_flip_t param;

	DRM_DEBUG("%s\n", __FUNCTION__);

	LOCK_TEST_WITH_RETURN(dev, filp);

	DRM_COPY_FROM_USER_IOCTL(param, (drm_i915_flip_t __user *) data,
				 sizeof(param));

	if (param.pipes & ~0x3) {
		DRM_ERROR("Invalid pipes 0x%x, only <= 0x3 is valid\n",
			  param.pipes);
		return DRM_ERR(EINVAL);
	}

	i915_dispatch_flip(dev, param.pipes, 0);

	return 0;
}


static int i915_getparam(DRM_IOCTL_ARGS)
{
	DRM_DEVICE;
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_i915_getparam_t param;
	int value;

	if (!dev_priv) {
		DRM_ERROR("%s called with no initialization\n", __FUNCTION__);
		return DRM_ERR(EINVAL);
	}

	DRM_COPY_FROM_USER_IOCTL(param, (drm_i915_getparam_t __user *) data,
				 sizeof(param));

	switch (param.param) {
	case I915_PARAM_IRQ_ACTIVE:
		value = dev->irq ? 1 : 0;
		break;
	case I915_PARAM_ALLOW_BATCHBUFFER:
		value = dev_priv->allow_batchbuffer ? 1 : 0;
		break;
	case I915_PARAM_LAST_DISPATCH:
		value = READ_BREADCRUMB(dev_priv);
		break;
	default:
		DRM_ERROR("Unknown parameter %d\n", param.param);
		return DRM_ERR(EINVAL);
	}

	if (DRM_COPY_TO_USER(param.value, &value, sizeof(int))) {
		DRM_ERROR("DRM_COPY_TO_USER failed\n");
		return DRM_ERR(EFAULT);
	}

	return 0;
}

static int i915_setparam(DRM_IOCTL_ARGS)
{
	DRM_DEVICE;
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_i915_setparam_t param;

	if (!dev_priv) {
		DRM_ERROR("%s called with no initialization\n", __FUNCTION__);
		return DRM_ERR(EINVAL);
	}

	DRM_COPY_FROM_USER_IOCTL(param, (drm_i915_setparam_t __user *) data,
				 sizeof(param));

	switch (param.param) {
	case I915_SETPARAM_USE_MI_BATCHBUFFER_START:
		dev_priv->use_mi_batchbuffer_start = param.value;
		break;
	case I915_SETPARAM_TEX_LRU_LOG_GRANULARITY:
		dev_priv->tex_lru_log_granularity = param.value;
		break;
	case I915_SETPARAM_ALLOW_BATCHBUFFER:
		dev_priv->allow_batchbuffer = param.value;
		break;
	default:
		DRM_ERROR("unknown parameter %d\n", param.param);
		return DRM_ERR(EINVAL);
	}

	return 0;
}

drm_i915_mmio_entry_t mmio_table[] = {
	[MMIO_REGS_PS_DEPTH_COUNT] = {
		I915_MMIO_MAY_READ|I915_MMIO_MAY_WRITE,
		0x2350,
		8
	}	
};

static int mmio_table_size = sizeof(mmio_table)/sizeof(drm_i915_mmio_entry_t);

static int i915_mmio(DRM_IOCTL_ARGS)
{
	char buf[32];
	DRM_DEVICE;
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_i915_mmio_entry_t *e;	 
	drm_i915_mmio_t mmio;
	void __iomem *base;
	if (!dev_priv) {
		DRM_ERROR("%s called with no initialization\n", __FUNCTION__);
		return DRM_ERR(EINVAL);
	}
	DRM_COPY_FROM_USER_IOCTL(mmio, (drm_i915_mmio_t __user *) data,
				 sizeof(mmio));

	if (mmio.reg >= mmio_table_size)
		return DRM_ERR(EINVAL);

	e = &mmio_table[mmio.reg];
	base = dev_priv->mmio_map->handle + e->offset;

        switch (mmio.read_write) {
		case I915_MMIO_READ:
			if (!(e->flag & I915_MMIO_MAY_READ))
				return DRM_ERR(EINVAL);
			memcpy_fromio(buf, base, e->size);
			if (DRM_COPY_TO_USER(mmio.data, buf, e->size)) {
				DRM_ERROR("DRM_COPY_TO_USER failed\n");
				return DRM_ERR(EFAULT);
			}
			break;

		case I915_MMIO_WRITE:
			if (!(e->flag & I915_MMIO_MAY_WRITE))
				return DRM_ERR(EINVAL);
			if(DRM_COPY_FROM_USER(buf, mmio.data, e->size)) {
				DRM_ERROR("DRM_COPY_TO_USER failed\n");
				return DRM_ERR(EFAULT);
			}
			memcpy_toio(base, buf, e->size);
			break;
	}
	return 0;
}

int i915_driver_load(drm_device_t *dev, unsigned long flags)
{
	/* i915 has 4 more counters */
	dev->counters += 4;
	dev->types[6] = _DRM_STAT_IRQ;
	dev->types[7] = _DRM_STAT_PRIMARY;
	dev->types[8] = _DRM_STAT_SECONDARY;
	dev->types[9] = _DRM_STAT_DMA;

	return 0;
}

void i915_driver_lastclose(drm_device_t * dev)
{
	if (dev->dev_private) {
		drm_i915_private_t *dev_priv = dev->dev_private;
		i915_do_cleanup_pageflip(dev);
		i915_hwz_free(dev);
		i915_mem_takedown(&(dev_priv->agp_heap));

		if (dev_priv->priv1_addr) {
			free_pages(dev_priv->priv1_addr, dev_priv->priv1_order);
			dev_priv->priv1_addr = 0;
		}
	}
	i915_dma_cleanup(dev);
}

void i915_driver_preclose(drm_device_t * dev, DRMFILE filp)
{
	if (dev->dev_private) {
		drm_i915_private_t *dev_priv = dev->dev_private;
		i915_mem_release(dev, filp, dev_priv->agp_heap);
	}
}

drm_ioctl_desc_t i915_ioctls[] = {
	[DRM_IOCTL_NR(DRM_I915_INIT)] = {i915_dma_init, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY},
	[DRM_IOCTL_NR(DRM_I915_FLUSH)] = {i915_flush_ioctl, DRM_AUTH},
	[DRM_IOCTL_NR(DRM_I915_FLIP)] = {i915_flip_bufs, DRM_AUTH},
	[DRM_IOCTL_NR(DRM_I915_BATCHBUFFER)] = {i915_batchbuffer, DRM_AUTH},
	[DRM_IOCTL_NR(DRM_I915_IRQ_EMIT)] = {i915_irq_emit, DRM_AUTH},
	[DRM_IOCTL_NR(DRM_I915_IRQ_WAIT)] = {i915_irq_wait, DRM_AUTH},
	[DRM_IOCTL_NR(DRM_I915_GETPARAM)] = {i915_getparam, DRM_AUTH},
	[DRM_IOCTL_NR(DRM_I915_SETPARAM)] = {i915_setparam, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY},
	[DRM_IOCTL_NR(DRM_I915_ALLOC)] = {i915_mem_alloc, DRM_AUTH},
	[DRM_IOCTL_NR(DRM_I915_FREE)] = {i915_mem_free, DRM_AUTH},
	[DRM_IOCTL_NR(DRM_I915_INIT_HEAP)] = {i915_mem_init_heap, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY},
	[DRM_IOCTL_NR(DRM_I915_CMDBUFFER)] = {i915_cmdbuffer, DRM_AUTH},
	[DRM_IOCTL_NR(DRM_I915_DESTROY_HEAP)] = { i915_mem_destroy_heap, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY },
	[DRM_IOCTL_NR(DRM_I915_SET_VBLANK_PIPE)] = { i915_vblank_pipe_set, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY },
	[DRM_IOCTL_NR(DRM_I915_GET_VBLANK_PIPE)] = { i915_vblank_pipe_get, DRM_AUTH },
	[DRM_IOCTL_NR(DRM_I915_VBLANK_SWAP)] = {i915_vblank_swap, DRM_AUTH},
	[DRM_IOCTL_NR(DRM_I915_MMIO)] = {i915_mmio, DRM_AUTH},
	[DRM_IOCTL_NR(DRM_I915_HWZ)] = {i915_hwz, DRM_AUTH},
};

int i915_max_ioctl = DRM_ARRAY_SIZE(i915_ioctls);

/**
 * Determine if the device really is AGP or not.
 *
 * All Intel graphics chipsets are treated as AGP, even if they are really
 * PCI-e.
 *
 * \param dev   The device to be tested.
 *
 * \returns
 * A value of 1 is always retured to indictate every i9x5 is AGP.
 */
int i915_driver_device_is_agp(drm_device_t * dev)
{
	return 1;
}

int i915_driver_firstopen(struct drm_device *dev)
{
#ifdef I915_HAVE_BUFFER
	drm_bo_driver_init(dev);
#endif
	return 0;
}
