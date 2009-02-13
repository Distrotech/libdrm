/**************************************************************************
 *
 * Copyright (c) 2008 Tungsten Graphics, Inc., Cedar Park, TX., USA,
 * All Rights Reserved.
 * Copyright (c) 2009 VMware, Inc., Palo Alto, CA., USA,
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:

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
 **************************************************************************/
/*
 * Authors: Thomas Hellström <thomas-at-tungstengraphics-dot-com>
 */

#include "drmP.h"
#include "ochr_drm.h"
#include "via_drv.h"
#include "ttm/ttm_fence_driver.h"

#define VIA_POLL_DELAY_NS 100000

static void via_ttm_fence_poll(struct ttm_fence_device *fdev,
			       uint32_t engine, uint32_t waiting_types)
{
	struct drm_via_private *dev_priv =
	    container_of(fdev, struct drm_via_private, fdev);
	uint32_t seq;

	if (unlikely(!waiting_types))
		return;

	switch (engine) {
	case VIA_ENGINE_CMD:
		{
			struct ttm_fence_class_manager *fc =
			    &fdev->fence_class[engine];
			uint32_t offset = VIA_FENCE_OFFSET_CMD;
			uint32_t type = TTM_FENCE_TYPE_EXE;
			uint32_t error = 0;

			offset >>= 2;

			seq = ioread32(dev_priv->fence_map + offset);

			if (unlikely(waiting_types & VIA_FENCE_TYPE_HQV0)) {
				if (!(VIA_READ(0x3D0) & (1 << 4)))
					type |= VIA_FENCE_TYPE_HQV0;
			}

			if (unlikely(waiting_types & VIA_FENCE_TYPE_HQV1)) {
				if (!(VIA_READ(0x13D0) & (1 << 4)))
					type |= VIA_FENCE_TYPE_HQV1;
			}

			if (unlikely(waiting_types & VIA_FENCE_TYPE_MPEG0)) {
				uint32_t mpeg_status = VIA_READ(0xC54);
				if ((mpeg_status & 0x207) == 0x204)
					type |= VIA_FENCE_TYPE_MPEG0;
				if (unlikely((mpeg_status & 0x70) != 0)) {
					type |= VIA_FENCE_TYPE_MPEG0;
					error = mpeg_status & 0x70;
				}
			}

			/*
			 * FIXME: Info on MPEG1 engine?
			 */

			ttm_fence_handler(fdev, engine, seq, type, error);
			if (fc->waiting_types) {
				hrtimer_start(&dev_priv->fence_timer,
					      ktime_set(0, VIA_POLL_DELAY_NS),
					      HRTIMER_MODE_REL);
			}
			break;
		}
	case VIA_ENGINE_DMA0:
	case VIA_ENGINE_DMA1:
	case VIA_ENGINE_DMA2:
	case VIA_ENGINE_DMA3:
		{
			uint32_t dma_engine = engine - VIA_ENGINE_DMA0;
			struct drm_via_blitq *blitq =
			    &dev_priv->blit_queues[dma_engine];
			unsigned long irq_flags;

			via_dmablit_handler(dev_priv->dev, dma_engine);

			spin_lock_irqsave(&blitq->blit_lock, irq_flags);
			seq = blitq->completed_fence_seq;
			spin_unlock_irqrestore(&blitq->blit_lock, irq_flags);

			ttm_fence_handler(fdev, engine, seq, TTM_FENCE_TYPE_EXE,
					  0);
			break;
		}
	default:
		break;
	}
}

/**
 * Emit a fence sequence.
 */

static int via_ttm_fence_emit_sequence(struct ttm_fence_device *fdev,
				       uint32_t class,
				       uint32_t flags,
				       uint32_t * sequence,
				       unsigned long *timeout_jiffies)
{
	struct drm_via_private *dev_priv =
	    container_of(fdev, struct drm_via_private, fdev);

	*sequence = atomic_read(&dev_priv->fence_seq[class]);
	*timeout_jiffies = jiffies + 3 * HZ;

	return 0;
}

void via_ttm_fence_cmd_handler(struct drm_via_private *dev_priv,
			       uint32_t signal_types)
{
	struct ttm_fence_device *fdev = &dev_priv->fdev;
	struct ttm_fence_class_manager *fc = &fdev->fence_class[VIA_ENGINE_CMD];

	write_lock(&fc->lock);
	via_ttm_fence_poll(fdev, VIA_ENGINE_CMD,
			   fc->waiting_types | signal_types);
	write_unlock(&fc->lock);
}

void via_ttm_fence_dmablit_handler(struct drm_via_private *dev_priv, int engine)
{
	struct ttm_fence_device *fdev = &dev_priv->fdev;
	struct ttm_fence_class_manager *fc = &fdev->fence_class[engine];

	write_lock(&fc->lock);
	via_ttm_fence_poll(fdev, engine, fc->waiting_types);
	write_unlock(&fc->lock);
}

static bool via_ttm_fence_has_irq(struct ttm_fence_device *fdev,
				      uint32_t engine, uint32_t flags)
{
	struct drm_via_private *dev_priv =
	    container_of(fdev, struct drm_via_private, fdev);

	if (engine >= VIA_ENGINE_DMA0)
		return dev_priv->has_irq;

	return hrtimer_is_hres_active(&dev_priv->fence_timer);
}

void via_ttm_signal_fences(struct drm_via_private *dev_priv)
{
	struct ttm_fence_class_manager *fc;
	int i;
	unsigned long irq_flags;

	if (via_driver_dma_quiescent(dev_priv->dev)) {
		msleep(1000);
	}

	for (i = 0; i < dev_priv->fdev.num_classes; ++i) {
		fc = &dev_priv->fdev.fence_class[i];

		write_lock_irqsave(&fc->lock, irq_flags);
		ttm_fence_handler(&dev_priv->fdev, i,
				  atomic_read(&dev_priv->fence_seq[i]),
				  0xFFFFFFFF, 0);
		write_unlock_irqrestore(&fc->lock, irq_flags);
	}
}

enum hrtimer_restart via_ttm_fence_timer_func(struct hrtimer *timer)
{
	struct drm_via_private *dev_priv =
	    container_of(timer, struct drm_via_private, fence_timer);
	struct ttm_fence_device *fdev = &dev_priv->fdev;
	struct ttm_fence_class_manager *fc = &fdev->fence_class[VIA_ENGINE_CMD];
	unsigned long irq_flags;

	write_lock_irqsave(&fc->lock, irq_flags);
	via_ttm_fence_poll(fdev, 0, fc->waiting_types);
	write_unlock_irqrestore(&fc->lock, irq_flags);

	return HRTIMER_NORESTART;
}

static struct ttm_fence_driver via_ttm_fence_driver = {
	.has_irq = via_ttm_fence_has_irq,
	.emit = via_ttm_fence_emit_sequence,
	.flush = NULL,
	.poll = via_ttm_fence_poll,
	.needed_flush = NULL,
	.wait = NULL,
	.signaled = NULL,
};

int via_ttm_fence_device_init(struct drm_via_private *dev_priv)
{
	struct ttm_fence_class_init fci = {.wrap_diff = (1 << 30),
		.flush_diff = (1 << 29),
		.sequence_mask = 0xFFFFFFFF
	};

	return ttm_fence_device_init(5,
				     dev_priv->mem_global_ref.object,
				     &dev_priv->fdev,
				     &fci, true, &via_ttm_fence_driver);
}
