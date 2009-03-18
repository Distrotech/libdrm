/*
 * Copyright (C) 2008 Maarten Maathuis.
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
#include "drm_mode.h"
#include "nouveau_reg.h"
#include "nouveau_drv.h"
#include "nouveau_crtc.h"
#include "nv50_display.h"

static void
nv50_cursor_show(struct nouveau_crtc *crtc, bool update)
{
	struct drm_nouveau_private *dev_priv = crtc->base.dev->dev_private;
	struct drm_device *dev = crtc->base.dev;
	uint32_t offset = crtc->index * 0x400;

	DRM_DEBUG("\n");

	if (dev_priv->chipset != 0x50)
		OUT_MODE(NV84_CRTC0_CURSOR_DMA + offset,
			 NV84_CRTC0_CURSOR_DMA_LOCAL);
	OUT_MODE(NV50_CRTC0_CURSOR_CTRL + offset, NV50_CRTC0_CURSOR_CTRL_SHOW);
			 
	if (update) {
		OUT_MODE(NV50_UPDATE_DISPLAY, 0);
		crtc->cursor.visible = true;
	}
}

static void
nv50_cursor_hide(struct nouveau_crtc *crtc, bool update)
{
	struct drm_nouveau_private *dev_priv = crtc->base.dev->dev_private;
	struct drm_device *dev = crtc->base.dev;
	uint32_t offset = crtc->index * 0x400;

	DRM_DEBUG("\n");

	OUT_MODE(NV50_CRTC0_CURSOR_CTRL + offset, NV50_CRTC0_CURSOR_CTRL_HIDE);
	if (dev_priv->chipset != 0x50)
		OUT_MODE(NV84_CRTC0_CURSOR_DMA + offset,
			 NV84_CRTC0_CURSOR_DMA_DISABLE);

	if (update) {
		OUT_MODE(NV50_UPDATE_DISPLAY, 0);
		crtc->cursor.visible = false;
	}
}

static void
nv50_cursor_set_pos(struct nouveau_crtc *crtc, int x, int y)
{
	struct drm_nouveau_private *dev_priv = crtc->base.dev->dev_private;

	nv_wr32(NV50_HW_CURSOR_POS(crtc->index),
		((y & 0xFFFF) << 16) | (x & 0xFFFF));
	/* Needed to make the cursor move. */
	nv_wr32(NV50_HW_CURSOR_POS_CTRL(crtc->index), 0);
}

static void
nv50_cursor_set_offset(struct nouveau_crtc *crtc, uint32_t offset)
{
	struct drm_device *dev = crtc->base.dev;

	DRM_DEBUG("\n");

	OUT_MODE(NV50_CRTC0_CURSOR_OFFSET + crtc->index * 0x0400, offset >> 8);
}

int
nv50_cursor_init(struct nouveau_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int idx = crtc->index;

	nv_wr32(NV50_PDISPLAY_CURSOR_CURSOR_CTRL2(idx), 0x2000);
	if (!nv_wait(NV50_PDISPLAY_CURSOR_CURSOR_CTRL2(idx),
		     NV50_PDISPLAY_CURSOR_CURSOR_CTRL2_STATUS_MASK, 0)) {
		DRM_ERROR("timeout: CURSOR_CTRL2_STATUS == 0\n");
		DRM_ERROR("CURSOR_CTRL2 = 0x%08x\n",
			  nv_rd32(NV50_PDISPLAY_CURSOR_CURSOR_CTRL2(idx)));
		return -EBUSY;
	}

	nv_wr32(NV50_PDISPLAY_CURSOR_CURSOR_CTRL2(crtc->index),
		NV50_PDISPLAY_CURSOR_CURSOR_CTRL2_ON);
	if (!nv_wait(NV50_PDISPLAY_CURSOR_CURSOR_CTRL2(idx),
		     NV50_PDISPLAY_CURSOR_CURSOR_CTRL2_STATUS_ACTIVE,
		     NV50_PDISPLAY_CURSOR_CURSOR_CTRL2_STATUS_ACTIVE)) {
		DRM_ERROR("timeout: CURSOR_CTRL2_STATUS_ACTIVE(%d)\n", idx);
		DRM_ERROR("CURSOR_CTRL2(%d) = 0x%08x\n", idx,
			  nv_rd32(NV50_PDISPLAY_CURSOR_CURSOR_CTRL2(idx)));
		return -EBUSY;
	}

	crtc->cursor.set_offset = nv50_cursor_set_offset;
	crtc->cursor.set_pos = nv50_cursor_set_pos;
	crtc->cursor.hide = nv50_cursor_hide;
	crtc->cursor.show = nv50_cursor_show;
	return 0;
}

void
nv50_cursor_fini(struct nouveau_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int idx = crtc->index;

	DRM_DEBUG("\n");

	nv_wr32(NV50_PDISPLAY_CURSOR_CURSOR_CTRL2(idx), 0);
	if (!nv_wait(NV50_PDISPLAY_CURSOR_CURSOR_CTRL2(idx),
		     NV50_PDISPLAY_CURSOR_CURSOR_CTRL2_STATUS_MASK, 0)) {
		DRM_ERROR("timeout: CURSOR_CTRL2_STATUS == 0\n");
		DRM_ERROR("CURSOR_CTRL2 = 0x%08x\n",
			  nv_rd32(NV50_PDISPLAY_CURSOR_CURSOR_CTRL2(idx)));
	}
}

