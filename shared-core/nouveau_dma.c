/*
 * Copyright (C) 2007 Ben Skeggs.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "drmP.h"
#include "drm.h"
#include "nouveau_drv.h"
#include "nouveau_dma.h"

int
nouveau_dma_channel_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct mem_block *pushbuf = NULL;
	int ret;

	DRM_DEBUG("\n");

	/* On !mm_enabled, we can't map a GART push buffer into kernel
	 * space easily - so we'll just use VRAM.
	 */
	pushbuf = nouveau_mem_alloc(dev, 0, 0x8000,
				    NOUVEAU_MEM_FB | NOUVEAU_MEM_MAPPED,
				    (struct drm_file *)-2);
	if (!pushbuf) {
		DRM_ERROR("Failed to allocate DMA push buffer\n");
		return -ENOMEM;
	}

	ret = nouveau_fifo_alloc(dev, &dev_priv->channel, (struct drm_file *)-2,
				 pushbuf, NvDmaFB, NvDmaTT);
	if (ret) {
		DRM_ERROR("Error allocating GPU channel: %d\n", ret);
		return ret;
	}

	if (!dev_priv->mm_enabled) {
		ret = nouveau_dma_channel_setup(dev_priv->channel);
		if (ret) {
			nouveau_fifo_free(dev_priv->channel);
			dev_priv->channel = NULL;
			return ret;
		}
	}
	
	DRM_DEBUG("Using FIFO channel %d\n", dev_priv->channel->id);
	return 0;
}

void
nouveau_dma_channel_takedown(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	DRM_DEBUG("\n");

	if (dev_priv->channel) {
		nouveau_fifo_free(dev_priv->channel);
		dev_priv->channel = NULL;
	}
}

int
nouveau_dma_channel_setup(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpuobj *m2mf = NULL;
	int ret, i;

	/* Create NV_MEMORY_TO_MEMORY_FORMAT for buffer moves */
	ret = nouveau_gpuobj_gr_new(chan, dev_priv->card_type < NV_50 ?
				    0x0039 : 0x5039, &m2mf);
	if (ret)
		return ret;

	ret = nouveau_gpuobj_ref_add(dev, chan, NvM2MF, m2mf, NULL);
	if (ret)
		return ret;

	/* NV_MEMORY_TO_MEMORY_FORMAT requires a notifier object */
	ret = nouveau_notifier_alloc(chan, NvNotify0, 1, &chan->m2mf_ntfy);
	if (ret)
		return ret;

	/* Map push buffer */
	if (dev_priv->mm_enabled) {
		ret = drm_bo_kmap(chan->pushbuf_mem->bo, 0,
				  chan->pushbuf_mem->bo->mem.num_pages,
				  &chan->pushbuf_mem->kmap);
		if (ret)
			return ret;
		chan->dma.pushbuf = chan->pushbuf_mem->kmap.virtual;
	} else {
		drm_core_ioremap(chan->pushbuf_mem->map, dev);
		if (!chan->pushbuf_mem->map->handle) {
			DRM_ERROR("Failed to ioremap push buffer\n");
			return -EINVAL;
		}
		chan->dma.pushbuf = (void *)chan->pushbuf_mem->map->handle;
	}

	/* Map M2MF notifier object - fbcon. */
	if (drm_core_check_feature(dev, DRIVER_MODESET)) {
		ret = drm_bo_kmap(chan->notifier_block->bo, 0,
				  chan->notifier_block->bo->mem.num_pages,
				  &chan->notifier_block->kmap);
		if (ret)
			return ret;
		chan->m2mf_ntfy_map  = chan->notifier_block->kmap.virtual;
		chan->m2mf_ntfy_map += chan->m2mf_ntfy;
	}

	/* Initialise DMA vars */
	chan->dma.max  = (chan->pushbuf_mem->size >> 2) - 2;
	chan->dma.put  = 0;
	chan->dma.cur  = chan->dma.put;
	chan->dma.free = chan->dma.max - chan->dma.cur;

	/* Insert NOPS for NOUVEAU_DMA_SKIPS */
	RING_SPACE(chan, NOUVEAU_DMA_SKIPS);
	for (i = 0; i < NOUVEAU_DMA_SKIPS; i++)
		OUT_RING (chan, 0);

	/* Initialise NV_MEMORY_TO_MEMORY_FORMAT */
	RING_SPACE(chan, 4);
	BEGIN_RING(chan, NvSubM2MF, NV_MEMORY_TO_MEMORY_FORMAT_NAME, 1);
	OUT_RING  (chan, NvM2MF);
	BEGIN_RING(chan, NvSubM2MF, NV_MEMORY_TO_MEMORY_FORMAT_DMA_NOTIFY, 1);
	OUT_RING  (chan, NvNotify0);

	/* Sit back and pray the channel works.. */
	FIRE_RING (chan);

	return 0;
}

#define READ_GET() ((nv_rd32(chan->get) - chan->pushbuf_base) >> 2)
#define WRITE_PUT(val) nv_wr32(chan->put, ((val) << 2) + chan->pushbuf_base)

int
nouveau_dma_wait(struct nouveau_channel *chan, int size)
{
	struct drm_nouveau_private *dev_priv = chan->dev->dev_private;
	const int us_timeout = 100000;
	int ret = -EBUSY, i;

	for (i = 0; i < us_timeout; i++) {
		uint32_t get = READ_GET();

		if (chan->dma.put >= get) {
			chan->dma.free = chan->dma.max - chan->dma.cur;

			if (chan->dma.free < size) {
				OUT_RING(chan, 0x20000000|chan->pushbuf_base);
				if (get <= NOUVEAU_DMA_SKIPS) {
					/*corner case - will be idle*/
					if (chan->dma.put <= NOUVEAU_DMA_SKIPS)
						WRITE_PUT(NOUVEAU_DMA_SKIPS + 1);

					for (; i < us_timeout; i++) {
						get = READ_GET();
						if (get > NOUVEAU_DMA_SKIPS)
							break;

						DRM_UDELAY(1);
					}

					if (i >= us_timeout)
						break;
				}

				WRITE_PUT(NOUVEAU_DMA_SKIPS);
				chan->dma.cur  =
				chan->dma.put  = NOUVEAU_DMA_SKIPS;
				chan->dma.free = get - (NOUVEAU_DMA_SKIPS + 1);
			}
		} else {
			chan->dma.free = get - chan->dma.cur - 1;
		}

		if (chan->dma.free >= size) {
			ret = 0;
			break;
		}

		DRM_UDELAY(1);
	}

	return ret;
}
