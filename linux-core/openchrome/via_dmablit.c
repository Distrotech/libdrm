/* via_dmablit.c -- PCI DMA BitBlt support for the VIA Unichrome/Pro
 *
 * Copyright (C) 2005 Thomas Hellstrom, All Rights Reserved.
 * Copyright (c) 2009 VMware, Inc., Palo Alto, CA., USA,
 * All Rights Reserved.
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
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Thomas Hellstrom.
 *    Partially based on code obtained from Digeo Inc.
 */

#include "drmP.h"
#include "ochr_drm.h"
#include "via_drv.h"
#include "via_dmablit.h"

#include <linux/pagemap.h>

#define VIA_PGDN(x)             (((unsigned long)(x)) & PAGE_MASK)
#define VIA_PGOFF(x)            (((unsigned long)(x)) & ~PAGE_MASK)
#define VIA_PFN(x)              ((unsigned long)(x) >> PAGE_SHIFT)

enum via_dmablit_sys_type {
	VIA_DMABLIT_BO,
	VIA_DMABLIT_USER
};

struct drm_via_descriptor {
	uint32_t mem_addr;
	uint32_t dev_addr;
	uint32_t size;
	uint32_t next;
};

struct via_dmablit {
	uint32_t num_lines;
	uint32_t line_length;

	uint32_t vram_offs;
	uint32_t vram_stride;

	uint32_t mem_pg_offs;
	uint32_t mem_stride;

	int to_fb;
};

struct drm_via_sg_info {
	uint32_t engine;
	uint32_t fence_seq;
	struct list_head head;
	struct page **pages;
	unsigned long num_pages;
	enum via_dmablit_sys_type sys_type;
	struct drm_via_descriptor **desc_pages;
	int num_desc_pages;
	int num_desc;
	enum dma_data_direction direction;
	unsigned char *bounce_buffer;
	dma_addr_t chain_start;
	unsigned int descriptors_per_page;
	int aborted;
	enum {
		dr_via_device_mapped,
		dr_via_desc_pages_alloc,
		dr_via_pages_locked,
		dr_via_pages_alloc,
		dr_via_sg_init
	} state;
};

/*
 * Unmap a DMA mapping.
 */

static void
via_unmap_blit_from_device(struct pci_dev *pdev, struct drm_via_sg_info *vsg)
{
	int num_desc = vsg->num_desc;
	unsigned cur_descriptor_page = num_desc / vsg->descriptors_per_page;
	unsigned descriptor_this_page = num_desc % vsg->descriptors_per_page;
	struct drm_via_descriptor *desc_ptr =
	    vsg->desc_pages[cur_descriptor_page] + descriptor_this_page;
	dma_addr_t next = vsg->chain_start;

	while (num_desc--) {
		if (descriptor_this_page-- == 0) {
			cur_descriptor_page--;
			descriptor_this_page = vsg->descriptors_per_page - 1;
			desc_ptr = vsg->desc_pages[cur_descriptor_page] +
			    descriptor_this_page;
		}
		dma_unmap_single(&pdev->dev, next, sizeof(*desc_ptr),
				 DMA_TO_DEVICE);
		dma_unmap_page(&pdev->dev, desc_ptr->mem_addr, desc_ptr->size,
			       vsg->direction);
		next = (dma_addr_t) desc_ptr->next;
		desc_ptr--;
	}
}

/*
 * If mode = 0, count how many descriptors are needed.
 * If mode = 1, Map the DMA pages for the device, put together and map also the descriptors.
 * Descriptors are run in reverse order by the hardware because we are not allowed to update the
 * 'next' field without syncing calls when the descriptor is already mapped.
 */

static void
via_map_blit_for_device(struct pci_dev *pdev,
			const struct via_dmablit *xfer,
			struct drm_via_sg_info *vsg, int mode)
{
	unsigned cur_descriptor_page = 0;
	unsigned num_descriptors_this_page = 0;
	unsigned long mem_addr = xfer->mem_pg_offs;
	unsigned long cur_mem;
	uint32_t fb_addr = xfer->vram_offs;
	uint32_t cur_fb;
	unsigned long line_len;
	unsigned remaining_len;
	int num_desc = 0;
	int cur_line;
	dma_addr_t next = 0 | VIA_DMA_DPR_EC;
	struct drm_via_descriptor *desc_ptr = NULL;

	if (mode == 1)
		desc_ptr = vsg->desc_pages[cur_descriptor_page];

	for (cur_line = 0; cur_line < xfer->num_lines; ++cur_line) {

		line_len = xfer->line_length;
		cur_fb = fb_addr;
		cur_mem = mem_addr;

		while (line_len > 0) {

			remaining_len =
			    min(PAGE_SIZE - VIA_PGOFF(cur_mem), line_len);
			line_len -= remaining_len;

			if (mode == 1) {
				struct page *page;

				page = vsg->pages[cur_mem >> PAGE_SHIFT];
				BUG_ON(cur_descriptor_page >=
				       vsg->num_desc_pages);
				desc_ptr->mem_addr =
				    dma_map_page(&pdev->dev,
						 page,
						 VIA_PGOFF(cur_mem),
						 remaining_len, vsg->direction);
				desc_ptr->dev_addr = cur_fb;
				desc_ptr->size = remaining_len;
				desc_ptr->next = (uint32_t) next;
				next =
				    dma_map_single(&pdev->dev, desc_ptr,
						   sizeof(*desc_ptr),
						   DMA_TO_DEVICE);
				desc_ptr++;
				if (++num_descriptors_this_page >=
				    vsg->descriptors_per_page) {
					num_descriptors_this_page = 0;
					desc_ptr =
					    vsg->
					    desc_pages[++cur_descriptor_page];
				}
			}

			num_desc++;
			cur_mem += remaining_len;
			cur_fb += remaining_len;
		}

		mem_addr += xfer->mem_stride;
		fb_addr += xfer->vram_stride;
	}

	if (mode == 1) {
		vsg->chain_start = next;
		vsg->state = dr_via_device_mapped;
	}
	vsg->num_desc = num_desc;
}

/*
 * Function that frees up all resources for a blit. It is usable even if the
 * blit info has only been partially built as long as the status enum is consistent
 * with the actual status of the used resources.
 */

static void via_free_sg_info(struct pci_dev *pdev, struct drm_via_sg_info *vsg)
{
	struct page *page;
	int i;

	switch (vsg->state) {
	case dr_via_device_mapped:
		via_unmap_blit_from_device(pdev, vsg);
	case dr_via_desc_pages_alloc:
		for (i = 0; i < vsg->num_desc_pages; ++i) {
			if (vsg->desc_pages[i] != NULL)
				free_page((unsigned long)vsg->desc_pages[i]);
		}
		kfree(vsg->desc_pages);
	case dr_via_pages_locked:
		if (vsg->sys_type == VIA_DMABLIT_USER) {
			for (i = 0; i < vsg->num_pages; ++i) {
				if (NULL != (page = vsg->pages[i])) {
					if (!PageReserved(page)
					    && (DMA_FROM_DEVICE ==
						vsg->direction))
						SetPageDirty(page);
					page_cache_release(page);
				}
			}
		}
	case dr_via_pages_alloc:
		if (vsg->sys_type == VIA_DMABLIT_USER)
			vfree(vsg->pages);
	default:
		vsg->state = dr_via_sg_init;
	}
	if (vsg->bounce_buffer) {
		vfree(vsg->bounce_buffer);
		vsg->bounce_buffer = NULL;
	}
}

/*
 * Fire a blit engine.
 */

static void
via_fire_dmablit(struct drm_device *dev, struct drm_via_sg_info *vsg,
		 int engine)
{
	struct drm_via_private *dev_priv = via_priv(dev);

	VIA_WRITE(VIA_PCI_DMA_MAR0 + engine * 0x10, 0);
	VIA_WRITE(VIA_PCI_DMA_DAR0 + engine * 0x10, 0);
	VIA_WRITE(VIA_PCI_DMA_CSR0 + engine * 0x04,
		  VIA_DMA_CSR_DD | VIA_DMA_CSR_TD | VIA_DMA_CSR_DE);
	VIA_WRITE(VIA_PCI_DMA_MR0 + engine * 0x04,
		  VIA_DMA_MR_CM | VIA_DMA_MR_TDIE);
	VIA_WRITE(VIA_PCI_DMA_BCR0 + engine * 0x10, 0);
	VIA_WRITE(VIA_PCI_DMA_DPR0 + engine * 0x10, vsg->chain_start);
	DRM_WRITEMEMORYBARRIER();
	VIA_WRITE(VIA_PCI_DMA_CSR0 + engine * 0x04,
		  VIA_DMA_CSR_DE | VIA_DMA_CSR_TS);
	VIA_READ(VIA_PCI_DMA_CSR0 + engine * 0x04);
}

#if 0
/*
 * Obtain a page pointer array and lock all pages into system memory.
 */

static int
via_lock_all_dma_pages(struct drm_via_sg_info *vsg,
		       struct drm_via_dmablit *xfer)
{
	int ret;
	unsigned long first_pfn = VIA_PFN(xfer->mem_addr);
	vsg->num_pages =
	    VIA_PFN(xfer->mem_addr + (xfer->num_lines * xfer->mem_stride - 1)) -
	    first_pfn + 1;

	if (NULL ==
	    (vsg->pages = vmalloc(sizeof(struct page *) * vsg->num_pages)))
		return -ENOMEM;
	memset(vsg->pages, 0, sizeof(struct page *) * vsg->num_pages);
	down_read(&current->mm->mmap_sem);
	ret = get_user_pages(current, current->mm,
			     (unsigned long)xfer->mem_addr,
			     vsg->num_pages,
			     (vsg->direction == DMA_FROM_DEVICE),
			     0, vsg->pages, NULL);

	up_read(&current->mm->mmap_sem);
	if (ret != vsg->num_pages) {
		if (ret < 0)
			return ret;
		vsg->state = dr_via_pages_locked;
		return -EINVAL;
	}
	vsg->state = dr_via_pages_locked;
	DRM_DEBUG("DMA pages locked\n");
	return 0;
}

#endif

/*
 * Allocate DMA capable memory for the blit descriptor chain, and an array that keeps track of the
 * pages we allocate. We don't want to use kmalloc for the descriptor chain because it may be
 * quite large for some blits, and pages don't need to be contingous.
 */

static int via_alloc_desc_pages(struct drm_via_sg_info *vsg)
{
	int i;

	vsg->descriptors_per_page =
	    PAGE_SIZE / sizeof(struct drm_via_descriptor);
	vsg->num_desc_pages =
	    (vsg->num_desc + vsg->descriptors_per_page -
	     1) / vsg->descriptors_per_page;

	if (NULL ==
	    (vsg->desc_pages =
	     kmalloc(sizeof(void *) * vsg->num_desc_pages, GFP_KERNEL)))
		return -ENOMEM;

	memset(vsg->desc_pages, 0, sizeof(void *) * vsg->num_desc_pages);
	vsg->state = dr_via_desc_pages_alloc;
	for (i = 0; i < vsg->num_desc_pages; ++i) {
		if (NULL == (vsg->desc_pages[i] = (struct drm_via_descriptor *)
			     __get_free_page(GFP_KERNEL)))
			return -ENOMEM;
	}
	DRM_DEBUG("Allocated %d pages for %d descriptors.\n",
		  vsg->num_desc_pages, vsg->num_desc);
	return 0;
}

static void via_abort_dmablit(struct drm_device *dev, int engine)
{
	struct drm_via_private *dev_priv = via_priv(dev);

	VIA_WRITE(VIA_PCI_DMA_CSR0 + engine * 0x04, VIA_DMA_CSR_TA);
}

static void via_dmablit_engine_off(struct drm_device *dev, int engine)
{
	struct drm_via_private *dev_priv = via_priv(dev);

	VIA_WRITE(VIA_PCI_DMA_CSR0 + engine * 0x04,
		  VIA_DMA_CSR_TD | VIA_DMA_CSR_DD);
}

/*
 * The dmablit part of the IRQ handler. Trying to do only reasonably fast things here.
 * The rest, like unmapping and freeing memory for done blits is done in a separate workqueue
 * task. Basically the task of the interrupt handler is to submit a new blit to the engine, while
 * the workqueue task takes care of processing associated with the old blit.
 */

void via_dmablit_handler(struct drm_device *dev, int engine)
{
	struct drm_via_private *dev_priv = via_priv(dev);
	struct drm_via_blitq *blitq = dev_priv->blit_queues + engine;
	struct drm_via_sg_info *cur;
	int done_transfer;
	unsigned long irqsave = 0;
	uint32_t status = 0;

	spin_lock_irqsave(&blitq->blit_lock, irqsave);

	done_transfer = blitq->is_active &&
	    ((status =
	      VIA_READ(VIA_PCI_DMA_CSR0 + engine * 0x04)) & VIA_DMA_CSR_TD);
	done_transfer = done_transfer || (blitq->aborting
					  && !(status & VIA_DMA_CSR_DE));

	cur = blitq->cur;
	if (done_transfer) {
		cur->aborted = blitq->aborting;
		blitq->completed_fence_seq = cur->fence_seq;
		list_add_tail(&cur->head, &blitq->done_blits);

		/*
		 * Clear transfer done flag.
		 */

		VIA_WRITE(VIA_PCI_DMA_CSR0 + engine * 0x04, VIA_DMA_CSR_TD);

		blitq->is_active = 0;
		blitq->aborting = 0;
		schedule_work(&blitq->wq);

	} else if (blitq->is_active && time_after_eq(jiffies, blitq->end)) {

		/*
		 * Abort transfer after one second.
		 */

		via_abort_dmablit(dev, engine);
		blitq->aborting = 1;
		blitq->end = jiffies + DRM_HZ;
	}

	if (!blitq->is_active) {
		if (!list_empty(&blitq->pending_blits)) {
			cur = list_entry(blitq->pending_blits.next,
					 struct drm_via_sg_info, head);
			list_del_init(&cur->head);
			via_fire_dmablit(dev, cur, engine);
			blitq->is_active = 1;
			blitq->cur = cur;
			blitq->end = jiffies + DRM_HZ;
			if (!timer_pending(&blitq->poll_timer)) {
				blitq->poll_timer.expires = jiffies + 1;
				add_timer(&blitq->poll_timer);
			}
		} else {
			if (timer_pending(&blitq->poll_timer)) {
				del_timer(&blitq->poll_timer);
			}
			via_dmablit_engine_off(dev, engine);
		}
	}

	spin_unlock_irqrestore(&blitq->blit_lock, irqsave);
}

/*
 * A timer that regularly polls the blit engine in cases where we don't have interrupts:
 * a) Broken hardware (typically those that don't have any video capture facility).
 * b) Blit abort. The hardware doesn't send an interrupt when a blit is aborted.
 * The timer and hardware IRQ's can and do work in parallel. If the hardware has
 * irqs, it will shorten the latency somewhat.
 */

static void via_dmablit_timer(unsigned long data)
{
	struct drm_via_blitq *blitq = (struct drm_via_blitq *)data;
	struct drm_device *dev = blitq->dev;
	int engine = (int)
	    (blitq - ((struct drm_via_private *)dev->dev_private)->blit_queues);

	DRM_DEBUG("Polling timer called for engine %d, jiffies %lu\n", engine,
		  (unsigned long)jiffies);

	via_dmablit_handler(dev, engine);

	if (!timer_pending(&blitq->poll_timer)) {
		blitq->poll_timer.expires = jiffies + 1;
		add_timer(&blitq->poll_timer);

		/*
		 * Rerun handler to delete timer if engines are off, and
		 * to shorten abort latency. This is a little nasty.
		 */

		via_dmablit_handler(dev, engine);
	}
}

/*
 * Workqueue task that frees data and mappings associated with a blit.
 * Also wakes up waiting processes. Each of these tasks handles one
 * blit engine only and may not be called on each interrupt.
 */

static void via_dmablit_workqueue(struct work_struct *work)
{
	struct drm_via_blitq *blitq =
	    container_of(work, struct drm_via_blitq, wq);
	struct drm_device *dev = blitq->dev;
	unsigned long irqsave;
	struct drm_via_sg_info *cur_sg;

	DRM_DEBUG("Workqueue task called for blit engine %ld\n", (unsigned long)
		  (blitq -
		   ((struct drm_via_private *)dev->dev_private)->blit_queues));

	spin_lock_irqsave(&blitq->blit_lock, irqsave);
	while (!list_empty(&blitq->done_blits)) {

		cur_sg = list_entry(blitq->done_blits.next,
				    struct drm_via_sg_info, head);

		list_del_init(&cur_sg->head);

		spin_unlock_irqrestore(&blitq->blit_lock, irqsave);
		DRM_WAKEUP(&blitq->busy_queue);

		via_free_sg_info(dev->pdev, cur_sg);
		kfree(cur_sg);

		spin_lock_irqsave(&blitq->blit_lock, irqsave);
	}

	spin_unlock_irqrestore(&blitq->blit_lock, irqsave);
}

/*
 * Init all blit engines. Currently we use two, but some hardware have 4.
 */

void via_init_dmablit(struct drm_device *dev)
{
	int i;
	struct drm_via_private *dev_priv = via_priv(dev);
	struct drm_via_blitq *blitq;

	pci_set_master(dev->pdev);

	for (i = 0; i < VIA_NUM_BLIT_ENGINES; ++i) {
		blitq = dev_priv->blit_queues + i;
		blitq->dev = dev;
		blitq->cur = NULL;
		blitq->num_pending = 0;
		blitq->is_active = 0;
		blitq->aborting = 0;
		blitq->num_pending = 0;
		INIT_LIST_HEAD(&blitq->pending_blits);
		INIT_LIST_HEAD(&blitq->done_blits);
		spin_lock_init(&blitq->blit_lock);
		DRM_INIT_WAITQUEUE(&blitq->busy_queue);
		INIT_WORK(&blitq->wq, via_dmablit_workqueue);
		init_timer(&blitq->poll_timer);
		blitq->poll_timer.function = &via_dmablit_timer;
		blitq->poll_timer.data = (unsigned long)blitq;
	}
}

int
via_dmablit_bo(struct ttm_buffer_object *bo,
	       struct ttm_mem_reg *new_mem,
	       struct page **pages, int *fence_class)
{
	struct ttm_bo_device *bdev = bo->bdev;
	struct drm_via_private *dev_priv =
	    container_of(bdev, struct drm_via_private, bdev);
	struct drm_device *dev = dev_priv->dev;
	int draw = (new_mem->mem_type == TTM_PL_VRAM);
	int ret = 0;
	struct ttm_tt *ttm = bo->ttm;
	struct via_dmablit xfer;
	int engine;
	struct drm_via_sg_info *vsg;
	struct drm_via_blitq *blitq;
	unsigned long irq_flags;
	int i;
	struct page *d;

	BUG_ON(!draw && (bo->mem.mem_type != TTM_PL_VRAM));
	BUG_ON(!ttm);

	for (i = 0; i < ttm->num_pages; ++i) {
		d = ttm_tt_get_page(ttm, i);
		if (unlikely(d == NULL))
			return -ENOMEM;
	}

	vsg = kmalloc(sizeof(*vsg), GFP_KERNEL);
	if (unlikely(vsg == NULL))
		return -ENOMEM;

	if (draw) {
		struct ttm_mem_type_manager *man =
		    &bdev->man[new_mem->mem_type];
		vsg->direction = DMA_TO_DEVICE;
		xfer.vram_offs = (new_mem->mm_node->start << PAGE_SHIFT) +
		    man->gpu_offset;
		engine = 0;
	} else {
		vsg->direction = DMA_FROM_DEVICE;
		xfer.vram_offs = bo->offset;
		engine = 1;
	}

	blitq = dev_priv->blit_queues + engine;
	vsg->bounce_buffer = NULL;
	vsg->state = dr_via_sg_init;

	xfer.mem_stride = PAGE_SIZE;
	xfer.line_length = xfer.mem_stride;
	xfer.vram_stride = xfer.mem_stride;
	xfer.num_lines = bo->num_pages;
	xfer.to_fb = draw;
	xfer.mem_pg_offs = 0;

	vsg->sys_type = VIA_DMABLIT_BO;
	vsg->pages = (pages == NULL) ? ttm->pages : pages;
	vsg->num_pages = bo->num_pages;

	vsg->state = dr_via_pages_alloc;
	vsg->state = dr_via_pages_locked;

	via_map_blit_for_device(dev->pdev, &xfer, vsg, 0);
	ret = via_alloc_desc_pages(vsg);

	if (unlikely(ret != 0))
		goto out_err;

	via_map_blit_for_device(dev->pdev, &xfer, vsg, 1);
	spin_lock_irqsave(&blitq->blit_lock, irq_flags);
	list_add_tail(&vsg->head, &blitq->pending_blits);
	++blitq->num_pending;
	*fence_class = engine + VIA_ENGINE_DMA0;
	vsg->fence_seq =
	    atomic_add_return(1, &dev_priv->fence_seq[*fence_class]);
	spin_unlock_irqrestore(&blitq->blit_lock, irq_flags);

	via_dmablit_handler(dev, engine);

	return 0;
      out_err:
	via_free_sg_info(dev->pdev, vsg);
	kfree(vsg);
	return ret;
}

#if 0
/*
 * Build all info and do all mappings required for a blit.
 */

static int
via_build_sg_info(struct drm_device *dev, struct drm_via_sg_info *vsg,
		  struct drm_via_dmablit *xfer)
{
	int draw = xfer->to_fb;
	int ret = 0;

	vsg->direction = (draw) ? DMA_TO_DEVICE : DMA_FROM_DEVICE;
	vsg->bounce_buffer = NULL;

	vsg->state = dr_via_sg_init;

	if (xfer->num_lines <= 0 || xfer->line_length <= 0) {
		DRM_ERROR("Zero size bitblt.\n");
		return -EINVAL;
	}

	/*
	 * Below check is a driver limitation, not a hardware one. We
	 * don't want to lock unused pages, and don't want to incoporate the
	 * extra logic of avoiding them. Make sure there are no.
	 * (Not a big limitation anyway.)
	 */

	if ((xfer->mem_stride - xfer->line_length) > 2 * PAGE_SIZE) {
		DRM_ERROR("Too large system memory stride. Stride: %d, "
			  "Length: %d\n", xfer->mem_stride, xfer->line_length);
		return -EINVAL;
	}

	if ((xfer->mem_stride == xfer->line_length) &&
	    (xfer->fb_stride == xfer->line_length)) {
		xfer->mem_stride *= xfer->num_lines;
		xfer->line_length = xfer->mem_stride;
		xfer->fb_stride = xfer->mem_stride;
		xfer->num_lines = 1;
	}

	/*
	 * Don't lock an arbitrary large number of pages, since that causes a
	 * DOS security hole.
	 */

	if (xfer->num_lines > 2048
	    || (xfer->num_lines * xfer->mem_stride > (2048 * 2048 * 4))) {
		DRM_ERROR("Too large PCI DMA bitblt.\n");
		return -EINVAL;
	}

	/*
	 * we allow a negative fb stride to allow flipping of images in
	 * transfer.
	 */

	if (xfer->mem_stride < xfer->line_length ||
	    abs(xfer->fb_stride) < xfer->line_length) {
		DRM_ERROR("Invalid frame-buffer / memory stride.\n");
		return -EINVAL;
	}

	/*
	 * A hardware bug seems to be worked around if system memory addresses start on
	 * 16 byte boundaries. This seems a bit restrictive however. VIA is contacted
	 * about this. Meanwhile, impose the following restrictions:
	 */

#ifdef VIA_BUGFREE
	if ((((unsigned long)xfer->mem_addr & 3) !=
	     ((unsigned long)xfer->fb_addr & 3)) || ((xfer->num_lines > 1)
						     && ((xfer->mem_stride & 3)
							 !=
							 (xfer->
							  fb_stride & 3)))) {
		DRM_ERROR("Invalid DRM bitblt alignment.\n");
		return -EINVAL;
	}
#else
	if ((((unsigned long)xfer->mem_addr & 15)
	     || ((unsigned long)xfer->fb_addr & 3)) || ((xfer->num_lines > 1)
							&&
							((xfer->mem_stride & 15)
							 || (xfer->
							     fb_stride & 3)))) {
		DRM_ERROR("Invalid DRM bitblt alignment.\n");
		return -EINVAL;
	}
#endif

	if (0 != (ret = via_lock_all_dma_pages(vsg, xfer))) {
		DRM_ERROR("Could not lock DMA pages.\n");
		via_free_sg_info(dev->pdev, vsg);
		return ret;
	}

	via_map_blit_for_device(dev->pdev, xfer, vsg, 0);
	if (0 != (ret = via_alloc_desc_pages(vsg))) {
		DRM_ERROR("Could not allocate DMA descriptor pages.\n");
		via_free_sg_info(dev->pdev, vsg);
		return ret;
	}
	via_map_blit_for_device(dev->pdev, xfer, vsg, 1);

	return 0;
}

/*
 * Grab a free slot. Build blit info and queue a blit.
 */

static int via_dmablit(struct drm_device *dev, struct drm_via_dmablit *xfer,
		       int interruptible)
{
	struct drm_via_private *dev_priv = via_priv(dev);
	struct drm_via_sg_info *vsg;
	struct drm_via_blitq *blitq;
	int ret;
	int engine;
	unsigned long irqsave;

	if (dev_priv == NULL) {
		DRM_ERROR("Called without initialization.\n");
		return -EINVAL;
	}

	engine = (xfer->to_fb) ? 0 : 1;
	blitq = dev_priv->blit_queues + engine;
	vsg = kmalloc(sizeof(*vsg), GFP_KERNEL);

	if (unlikely(vsg == NULL))
		return -ENOMEM;

	ret = via_build_sg_info(dev, vsg, xfer);
	if (unlikely(ret != 0))
		goto out_err0;

	spin_lock_irqsave(&blitq->blit_lock, irqsave);
	list_add_tail(&vsg->head, &blitq->pending_blits);
	++blitq->num_pending;
	vsg->fence_seq =
	    atomic_add_return(1,
			      &dev_priv->fence_seq[engine + VIA_ENGINE_DMA0]);
	xfer->sync.sync_handle = vsg->fence_seq;
	xfer->sync.engine = engine + VIA_ENGINE_DMA0;
	spin_unlock_irqrestore(&blitq->blit_lock, irqsave);

	via_dmablit_handler(dev, engine, 0);

	return 0;
      out_err0:
	kfree(vsg);
	return ret;
}

#endif
