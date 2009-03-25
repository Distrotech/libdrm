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

#ifndef __NOUVEAU_DMA_H__
#define __NOUVEAU_DMA_H__

typedef enum {
	NvSubM2MF	= 0,
	NvSub2D		= 1
} nouveau_subchannel_id_t;

typedef enum {
	NvM2MF		= 0x80000001,
	NvDmaFB		= 0x80000002,
	NvDmaTT		= 0x80000003,
	NvNotify0       = 0x80000004,
	Nv2D		= 0x80000005,
} nouveau_object_handle_t;

#define NV_MEMORY_TO_MEMORY_FORMAT                                    0x00000039
#define NV_MEMORY_TO_MEMORY_FORMAT_NAME                               0x00000000
#define NV_MEMORY_TO_MEMORY_FORMAT_SET_REF                            0x00000050
#define NV_MEMORY_TO_MEMORY_FORMAT_NOP                                0x00000100
#define NV_MEMORY_TO_MEMORY_FORMAT_NOTIFY                             0x00000104
#define NV_MEMORY_TO_MEMORY_FORMAT_NOTIFY_STYLE_WRITE                 0x00000000
#define NV_MEMORY_TO_MEMORY_FORMAT_NOTIFY_STYLE_WRITE_LE_AWAKEN       0x00000001
#define NV_MEMORY_TO_MEMORY_FORMAT_DMA_NOTIFY                         0x00000180
#define NV_MEMORY_TO_MEMORY_FORMAT_DMA_SOURCE                         0x00000184
#define NV_MEMORY_TO_MEMORY_FORMAT_OFFSET_IN                          0x0000030c

#define NV50_MEMORY_TO_MEMORY_FORMAT                                  0x00005039
#define NV50_MEMORY_TO_MEMORY_FORMAT_UNK200                           0x00000200
#define NV50_MEMORY_TO_MEMORY_FORMAT_UNK21C                           0x0000021c
#define NV50_MEMORY_TO_MEMORY_FORMAT_OFFSET_IN_HIGH                   0x00000238
#define NV50_MEMORY_TO_MEMORY_FORMAT_OFFSET_OUT_HIGH                  0x0000023c

static inline int
RING_SPACE(struct nouveau_channel *chan, int size)
{
	if (chan->dma.free < size) {
		int ret;

		ret = nouveau_dma_wait(chan, size);
		if (ret)
			return ret;
	}

	chan->dma.free -= size;
	return 0;
}

static inline void
OUT_RING(struct nouveau_channel *chan, int data)
{
	chan->dma.pushbuf[chan->dma.cur++] = data;
}

static inline void
BEGIN_RING(struct nouveau_channel *chan, int subc, int mthd, int size)
{
	OUT_RING(chan, (subc << 13) | (size << 18) | mthd);
}

static inline void
FIRE_RING(struct nouveau_channel *chan)
{
	struct drm_nouveau_private *dev_priv = chan->dev->dev_private;

	if (chan->dma.cur == chan->dma.put)
		return;
	chan->accel_done = true;

	DRM_MEMORYBARRIER();
	chan->dma.put = chan->dma.cur;
	nv_wr32(chan->put, (chan->dma.put << 2) + chan->pushbuf_base);
}

/* This should allow easy switching to a real fifo in the future. */
#define OUT_MODE(mthd, val) do {				\
	nv50_display_command(dev, mthd, val);                   \
} while(0)

#endif
