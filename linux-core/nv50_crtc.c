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
#include "drm_crtc_helper.h"
#include "nouveau_reg.h"
#include "nouveau_drv.h"
#include "nouveau_encoder.h"
#include "nouveau_crtc.h"
#include "nouveau_fb.h"
#include "nouveau_fbcon.h"
#include "nouveau_connector.h"
#include "nv50_cursor.h"

#define NV50_LUT_INDEX(val, w) ((val << (8 - w)) | (val >> ((w << 1) - 8)))
static int
nv50_crtc_lut_load(struct nouveau_crtc *crtc)
{
	uint32_t index = 0, i;
	void __iomem *lut = crtc->lut.mem->kmap.virtual;

	DRM_DEBUG("\n");

	/* 16 bits, red, green, blue, unused, total of 64 bits per index */
	/* 10 bits lut, with 14 bits values. */
	switch (crtc->lut.depth) {
	case 15:
		/* R5G5B5 */
		for (i = 0; i < 32; i++) {
			index = NV50_LUT_INDEX(i, 5);
			writew(crtc->lut.r[i] >> 2, lut + 8*index + 0);
			writew(crtc->lut.g[i] >> 2, lut + 8*index + 2);
			writew(crtc->lut.b[i] >> 2, lut + 8*index + 4);
		}
		break;
	case 16:
		/* R5G6B5 */
		for (i = 0; i < 32; i++) {
			index = NV50_LUT_INDEX(i, 5);
			writew(crtc->lut.r[i] >> 2, lut + 8*index + 0);
			writew(crtc->lut.b[i] >> 2, lut + 8*index + 4);
		}

		/* Green has an extra bit. */
		for (i = 0; i < 64; i++) {
			index = NV50_LUT_INDEX(i, 6);
			writew(crtc->lut.g[i] >> 2, lut + 8*index + 2);
		}
		break;
	default:
		/* R8G8B8 */
		for (i = 0; i < 256; i++) {
			writew(crtc->lut.r[i] >> 2, lut + 8*i + 0);
			writew(crtc->lut.g[i] >> 2, lut + 8*i + 2);
			writew(crtc->lut.b[i] >> 2, lut + 8*i + 4);
		}
		break;
	}

	return 0;
}

static int
nv50_crtc_blank(struct nouveau_crtc *crtc, bool blanked)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t offset = crtc->index * 0x400;

	DRM_DEBUG("index %d\n", crtc->index);
	DRM_DEBUG("%s\n", blanked ? "blanked" : "unblanked");

	if (blanked) {
		crtc->cursor->hide(crtc);

		OUT_MODE(NV50_CRTC0_CLUT_MODE + offset,
			 NV50_CRTC0_CLUT_MODE_BLANK);
		if (dev_priv->chipset != 0x50)
			OUT_MODE(NV84_CRTC0_BLANK_UNK1 + offset,
				 NV84_CRTC0_BLANK_UNK1_BLANK);
		OUT_MODE(NV50_CRTC0_BLANK_CTRL + offset,
			 NV50_CRTC0_BLANK_CTRL_BLANK);
		if (dev_priv->chipset != 0x50)
			OUT_MODE(NV84_CRTC0_BLANK_UNK2 + offset,
				 NV84_CRTC0_BLANK_UNK2_BLANK);
	} else {
		crtc->cursor->set_offset(crtc);

		if (dev_priv->chipset != 0x50)
			OUT_MODE(NV84_CRTC0_BLANK_UNK2 + offset,
				 NV84_CRTC0_BLANK_UNK2_UNBLANK);

		if (crtc->cursor->visible)
			crtc->cursor->show(crtc);
		else
			crtc->cursor->hide(crtc);

		if (dev_priv->chipset != 0x50)
			OUT_MODE(NV84_CRTC0_BLANK_UNK1 + offset,
				 NV84_CRTC0_BLANK_UNK1_UNBLANK);
		OUT_MODE(NV50_CRTC0_BLANK_CTRL + offset,
			 NV50_CRTC0_BLANK_CTRL_UNBLANK);
	}

	OUT_MODE(NV50_UPDATE_DISPLAY, 0);
	return 0;
}

static int nv50_crtc_set_dither(struct nouveau_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	uint32_t offset = crtc->index * 0x400;

	DRM_DEBUG("\n");

	OUT_MODE(NV50_CRTC0_DITHERING_CTRL + offset, crtc->use_dithering ?
		 NV50_CRTC0_DITHERING_CTRL_ON : NV50_CRTC0_DITHERING_CTRL_OFF);

	return 0;
}

static struct nouveau_encoder *
nouveau_crtc_encoder_get(struct nouveau_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_encoder *drm_encoder;

	list_for_each_entry(drm_encoder, &dev->mode_config.encoder_list, head) {
		if (drm_encoder->crtc == &crtc->base)
			return to_nouveau_encoder(drm_encoder);
	}

	return NULL;
}

static struct nouveau_connector *
nouveau_crtc_connector_get(struct nouveau_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_connector *drm_connector;
	struct nouveau_encoder *encoder;

	encoder = nouveau_crtc_encoder_get(crtc);
	if (!encoder)
		return NULL;

	list_for_each_entry(drm_connector, &dev->mode_config.connector_list, head) {
		if (drm_connector->encoder == &encoder->base)
			return to_nouveau_connector(drm_connector);
	}

	return NULL;
}

static int
nv50_crtc_set_scale(struct nouveau_crtc *crtc, int scaling_mode, bool update)
{
	struct nouveau_connector *connector = nouveau_crtc_connector_get(crtc);
	struct drm_device *dev = crtc->base.dev;
	struct drm_display_mode *native_mode = NULL;
	struct drm_display_mode *mode = &crtc->base.mode;
	uint32_t offset = crtc->index * 0x400;
	uint32_t outX, outY, horiz, vert;

	DRM_DEBUG("\n");

	if (!connector->digital)
		scaling_mode = DRM_MODE_SCALE_NON_GPU;

	switch (scaling_mode) {
	case DRM_MODE_SCALE_NO_SCALE:
	case DRM_MODE_SCALE_NON_GPU:
		break;
	default:
		if (!connector || !connector->native_mode) {
			DRM_ERROR("No native mode, forcing panel scaling\n");
			scaling_mode = DRM_MODE_SCALE_NON_GPU;
		} else {
			native_mode = connector->native_mode;
		}
		break;
	}

	switch (scaling_mode) {
	case DRM_MODE_SCALE_ASPECT:
		horiz = (native_mode->hdisplay << 19) / mode->hdisplay;
		vert = (native_mode->vdisplay << 19) / mode->vdisplay;

		if (vert > horiz) {
			outX = (mode->hdisplay * horiz) >> 19;
			outY = (mode->vdisplay * horiz) >> 19;
		} else {
			outX = (mode->hdisplay * vert) >> 19;
			outY = (mode->vdisplay * vert) >> 19;
		}
		break;
	case DRM_MODE_SCALE_FULLSCREEN:
		outX = native_mode->hdisplay;
		outY = native_mode->vdisplay;
		break;
	case DRM_MODE_SCALE_NO_SCALE:
	case DRM_MODE_SCALE_NON_GPU:
	default:
		outX = mode->hdisplay;
		outY = mode->vdisplay;
		break;
	}

	/* Got a better name for SCALER_ACTIVE? */
	/* One day i've got to really figure out why this is needed. */
	if ((mode->flags & DRM_MODE_FLAG_DBLSCAN) ||
	    (mode->flags & DRM_MODE_FLAG_INTERLACE) ||
	    mode->hdisplay != outX || mode->vdisplay != outY) {
		OUT_MODE(NV50_CRTC0_SCALE_CTRL + offset,
			 NV50_CRTC0_SCALE_CTRL_SCALER_ACTIVE);
	} else {
		OUT_MODE(NV50_CRTC0_SCALE_CTRL + offset,
			 NV50_CRTC0_SCALE_CTRL_SCALER_INACTIVE);
	}

	OUT_MODE(NV50_CRTC0_SCALE_RES1 + offset, outY << 16 | outX);
	OUT_MODE(NV50_CRTC0_SCALE_RES2 + offset, outY << 16 | outX);

	if (update)
		OUT_MODE(NV50_UPDATE_DISPLAY, 0);

	return 0;
}

static int
nv50_crtc_calc_clock(struct nouveau_crtc *crtc, struct drm_display_mode *mode,
		     uint32_t *bestN1, uint32_t *bestN2, uint32_t *bestM1,
		     uint32_t *bestM2, uint32_t *bestlog2P)
{
	struct pll_lims limits;
	int clk = mode->clock, vco2, crystal;
	int minvco1, minvco2, minU1, maxU1, minU2, maxU2, minM1, maxM1;
	int maxvco1, maxvco2, minN1, maxN1, minM2, maxM2, minN2, maxN2;
	bool fixedgain2;
	int M1, N1, M2, N2, log2P;
	int clkP, calcclk1, calcclk2, calcclkout;
	int delta, bestdelta = INT_MAX;
	int bestclk = 0;

	DRM_DEBUG("\n");

	/* These are in the g80 bios tables, at least in mine. */
	if (!get_pll_limits(crtc->base.dev,
			    NV50_PDISPLAY_CRTC_CLK_CLK_CTRL1(crtc->index),
			    &limits))
		return -EINVAL;

	minvco1 = limits.vco1.minfreq, maxvco1 = limits.vco1.maxfreq;
	minvco2 = limits.vco2.minfreq, maxvco2 = limits.vco2.maxfreq;
	minU1 = limits.vco1.min_inputfreq, minU2 = limits.vco2.min_inputfreq;
	maxU1 = limits.vco1.max_inputfreq, maxU2 = limits.vco2.max_inputfreq;
	minM1 = limits.vco1.min_m, maxM1 = limits.vco1.max_m;
	minN1 = limits.vco1.min_n, maxN1 = limits.vco1.max_n;
	minM2 = limits.vco2.min_m, maxM2 = limits.vco2.max_m;
	minN2 = limits.vco2.min_n, maxN2 = limits.vco2.max_n;
	crystal = limits.refclk;
	fixedgain2 = (minM2 == maxM2 && minN2 == maxN2);

	vco2 = (maxvco2 - maxvco2/200) / 2;
	/* log2P is maximum of 6 */
	for (log2P = 0; clk && log2P < 6 && clk <= (vco2 >> log2P); log2P++);
	clkP = clk << log2P;

	if (maxvco2 < clk + clk/200)	/* +0.5% */
		maxvco2 = clk + clk/200;

	for (M1 = minM1; M1 <= maxM1; M1++) {
		if (crystal/M1 < minU1)
			return bestclk;
		if (crystal/M1 > maxU1)
			continue;

		for (N1 = minN1; N1 <= maxN1; N1++) {
			calcclk1 = crystal * N1 / M1;
			if (calcclk1 < minvco1)
				continue;
			if (calcclk1 > maxvco1)
				break;

			for (M2 = minM2; M2 <= maxM2; M2++) {
				if (calcclk1/M2 < minU2)
					break;
				if (calcclk1/M2 > maxU2)
					continue;

				/* add calcclk1/2 to round better */
				N2 = (clkP * M2 + calcclk1/2) / calcclk1;
				if (N2 < minN2)
					continue;
				if (N2 > maxN2)
					break;

				if (!fixedgain2) {
					calcclk2 = calcclk1 * N2 / M2;
					if (calcclk2 < minvco2)
						break;
					if (calcclk2 > maxvco2)
						continue;
				} else
					calcclk2 = calcclk1;

				calcclkout = calcclk2 >> log2P;
				delta = abs(calcclkout - clk);
				/* we do an exhaustive search rather than
				 * terminating on an optimality condition...
				 */
				if (delta < bestdelta) {
					bestdelta = delta;
					bestclk = calcclkout;
					*bestN1 = N1;
					*bestN2 = N2;
					*bestM1 = M1;
					*bestM2 = M2;
					*bestlog2P = log2P;
					if (delta == 0)	/* except this one */
						return bestclk;
				}
			}
		}
	}

	return bestclk;
}

static int
nv50_crtc_set_clock(struct nouveau_crtc *crtc, struct drm_display_mode *mode)
{
	struct drm_nouveau_private *dev_priv = crtc->base.dev->dev_private;

	uint32_t pll_reg = NV50_PDISPLAY_CRTC_CLK_CLK_CTRL1(crtc->index);

	uint32_t N1 = 0, N2 = 0, M1 = 0, M2 = 0, log2P = 0;

	uint32_t reg1 = nv_rd32(pll_reg + 4);
	uint32_t reg2 = nv_rd32(pll_reg + 8);

	DRM_DEBUG("\n");

	nv_wr32(pll_reg, NV50_PDISPLAY_CRTC_CLK_CLK_CTRL1_CONNECTED | 0x10000011);

	/* The other bits are typically empty, but let's be on the safe side. */
	reg1 &= 0xff00ff00;
	reg2 &= 0x8000ff00;

	if (!nv50_crtc_calc_clock(crtc, mode, &N1, &N2, &M1, &M2, &log2P))
		return -EINVAL;

	DRM_DEBUG("N1 %d N2 %d M1 %d M2 %d log2P %d\n", N1, N2, M1, M2, log2P);

	reg1 |= (M1 << 16) | N1;
	reg2 |= (log2P << 28) | (M2 << 16) | N2;

	nv_wr32(pll_reg + 4, reg1);
	nv_wr32(pll_reg + 8, reg2);

	return 0;
}

static int nv50_crtc_set_clock_mode(struct nouveau_crtc *crtc)
{
	struct drm_nouveau_private *dev_priv = crtc->base.dev->dev_private;

	DRM_DEBUG("\n");

	/* This acknowledges a clock request. */
	nv_wr32(NV50_PDISPLAY_CRTC_CLK_CLK_CTRL2(crtc->index), 0);

	return 0;
}

static void nv50_crtc_destroy(struct drm_crtc *drm_crtc)
{
	struct nouveau_crtc *crtc = to_nouveau_crtc(drm_crtc);

	DRM_DEBUG("\n");

	if (!crtc)
		return;

	drm_crtc_cleanup(&crtc->base);

	nv50_cursor_destroy(crtc);

	if (crtc->lut.mem)
		nouveau_mem_free(drm_crtc->dev, crtc->lut.mem);
	kfree(crtc->mode);
	kfree(crtc);
}

static int nv50_crtc_cursor_set(struct drm_crtc *drm_crtc,
				struct drm_file *file_priv,
				uint32_t buffer_handle,
				uint32_t width, uint32_t height)
{
	struct drm_device *dev = drm_crtc->dev;
	struct nouveau_crtc *crtc = to_nouveau_crtc(drm_crtc);
	struct drm_gem_object *gem = NULL;
	int ret = 0;

	if (width != 64 || height != 64)
		return -EINVAL;

	if (buffer_handle) {
		gem = drm_gem_object_lookup(dev, file_priv, buffer_handle);
		if (!gem)
			return -EINVAL;

		ret = nouveau_gem_pin(gem, NOUVEAU_GEM_DOMAIN_VRAM);
		if (ret) {
			mutex_lock(&dev->struct_mutex);
			drm_gem_object_unreference(gem);
			mutex_unlock(&dev->struct_mutex);
			return ret;
		}

		crtc->cursor->set_bo(crtc, gem);
		crtc->cursor->set_offset(crtc);
		ret = crtc->cursor->show(crtc);
	} else {
		crtc->cursor->set_bo(crtc, NULL);
		crtc->cursor->hide(crtc);
	}

	OUT_MODE(NV50_UPDATE_DISPLAY, 0);
	return ret;
}

static int nv50_crtc_cursor_move(struct drm_crtc *drm_crtc, int x, int y)
{
	struct nouveau_crtc *crtc = to_nouveau_crtc(drm_crtc);

	return crtc->cursor->set_pos(crtc, x, y);
}

void
nv50_crtc_gamma_set(struct drm_crtc *drm_crtc, u16 *r, u16 *g, u16 *b,
		    uint32_t size)
{
	struct nouveau_crtc *crtc = to_nouveau_crtc(drm_crtc);
	int i;

	if (size != 256)
		return;

	for (i = 0; i < 256; i++) {
		crtc->lut.r[i] = r[i];
		crtc->lut.g[i] = g[i];
		crtc->lut.b[i] = b[i];
	}

	/* We need to know the depth before we upload, but it's possible to
	 * get called before a framebuffer is bound.  If this is the case,
	 * mark the lut values as dirty by setting depth==0, and it'll be
	 * uploaded on the first mode_set_base()
	 */
	if (!crtc->base.fb) {
		crtc->lut.depth = 0;
		return;
	}

	nv50_crtc_lut_load(crtc);
}

static const struct drm_crtc_funcs nv50_crtc_funcs = {
	.save = NULL,
	.restore = NULL,
	.cursor_set = nv50_crtc_cursor_set,
	.cursor_move = nv50_crtc_cursor_move,
	.gamma_set = nv50_crtc_gamma_set,
	.set_config = drm_crtc_helper_set_config,
	.destroy = nv50_crtc_destroy,
};

static void nv50_crtc_dpms(struct drm_crtc *drm_crtc, int mode)
{
	struct nouveau_crtc *crtc = to_nouveau_crtc(drm_crtc);

	switch (mode) {
	case DRM_MODE_DPMS_ON:
	case DRM_MODE_DPMS_STANDBY:
	case DRM_MODE_DPMS_SUSPEND:
		nv50_crtc_blank(crtc, false);
		break;
	case DRM_MODE_DPMS_OFF:
		nv50_crtc_blank(crtc, true);
		break;
	}
}

static void nv50_crtc_prepare(struct drm_crtc *drm_crtc)
{
	struct nouveau_crtc *crtc = to_nouveau_crtc(drm_crtc);

	nv50_crtc_blank(crtc, true);
}

static void nv50_crtc_commit(struct drm_crtc *drm_crtc)
{
}

static bool nv50_crtc_mode_fixup(struct drm_crtc *drm_crtc,
				 struct drm_display_mode *mode,
				 struct drm_display_mode *adjusted_mode)
{
	return true;
}

static int
nv50_crtc_execute_mode(struct nouveau_crtc *crtc, struct drm_display_mode *mode)
{
	struct drm_device *dev = crtc->base.dev;
	uint32_t hsync_dur,  vsync_dur, hsync_start_to_end, vsync_start_to_end;
	uint32_t hunk1, vunk1, vunk2a, vunk2b;
	uint32_t offset = crtc->index * 0x400;

	DRM_DEBUG("index %d\n", crtc->index);

	hsync_dur = mode->hsync_end - mode->hsync_start;
	vsync_dur = mode->vsync_end - mode->vsync_start;
	hsync_start_to_end = mode->htotal - mode->hsync_start;
	vsync_start_to_end = mode->vtotal - mode->vsync_start;
	/* I can't give this a proper name, anyone else can? */
	hunk1 = mode->htotal - mode->hsync_start + mode->hdisplay;
	vunk1 = mode->vtotal - mode->vsync_start + mode->vdisplay;
	/* Another strange value, this time only for interlaced modes. */
	vunk2a = 2*mode->vtotal - mode->vsync_start + mode->vdisplay;
	vunk2b = mode->vtotal - mode->vsync_start + mode->vtotal;

	if (mode->flags & DRM_MODE_FLAG_INTERLACE) {
		vsync_dur /= 2;
		vsync_start_to_end  /= 2;
		vunk1 /= 2;
		vunk2a /= 2;
		vunk2b /= 2;
		/* magic */
		if (mode->flags & DRM_MODE_FLAG_DBLSCAN) {
			vsync_start_to_end -= 1;
			vunk1 -= 1;
			vunk2a -= 1;
			vunk2b -= 1;
		}
	}

	OUT_MODE(NV50_CRTC0_CLOCK + offset, mode->clock | 0x800000);
	OUT_MODE(NV50_CRTC0_INTERLACE + offset,
		 (mode->flags & DRM_MODE_FLAG_INTERLACE) ? 2 : 0);
	OUT_MODE(NV50_CRTC0_DISPLAY_START + offset, 0);
	OUT_MODE(NV50_CRTC0_UNK82C + offset, 0);
	OUT_MODE(NV50_CRTC0_DISPLAY_TOTAL + offset,
		 mode->vtotal << 16 | mode->htotal);
	OUT_MODE(NV50_CRTC0_SYNC_DURATION + offset,
		 (vsync_dur - 1) << 16 | (hsync_dur - 1));
	OUT_MODE(NV50_CRTC0_SYNC_START_TO_BLANK_END + offset,
		 (vsync_start_to_end - 1) << 16 | (hsync_start_to_end - 1));
	OUT_MODE(NV50_CRTC0_MODE_UNK1 + offset,
		 (vunk1 - 1) << 16 | (hunk1 - 1));
	if (mode->flags & DRM_MODE_FLAG_INTERLACE) {
		OUT_MODE(NV50_CRTC0_MODE_UNK2 + offset,
			 (vunk2b - 1) << 16 | (vunk2a - 1));
	}

	crtc->set_dither(crtc);

	/* This is the actual resolution of the mode. */
	OUT_MODE(NV50_CRTC0_REAL_RES + offset,
		 (crtc->base.mode.vdisplay << 16) | crtc->base.mode.hdisplay);
	OUT_MODE(NV50_CRTC0_SCALE_CENTER_OFFSET + offset,
		 NV50_CRTC_SCALE_CENTER_OFFSET_VAL(0,0));

	nv50_crtc_blank(crtc, false);
	return 0;
}


static void
nv50_crtc_mode_set(struct drm_crtc *drm_crtc, struct drm_display_mode *mode,
		   struct drm_display_mode *adjusted_mode, int x, int y)
{
	struct drm_device *dev = drm_crtc->dev;
	struct nouveau_crtc *crtc = to_nouveau_crtc(drm_crtc);
	struct drm_encoder *drm_encoder;
	struct nouveau_encoder *encoder;
	struct drm_crtc_helper_funcs *crtc_helper = drm_crtc->helper_private;
	struct nouveau_connector *connector = NULL;

	/* Find the connector attached to this CRTC */
	list_for_each_entry(drm_encoder, &dev->mode_config.encoder_list, head) {
		struct drm_connector *drm_connector;

		encoder = to_nouveau_encoder(drm_encoder);
		if (drm_encoder->crtc != &crtc->base)
			continue;

		list_for_each_entry(drm_connector, &dev->mode_config.connector_list, head) {
			connector = to_nouveau_connector(drm_connector);
			if (drm_connector->encoder != drm_encoder)
				continue;

			break;
		}

		break; /* no use in finding more than one mode */
	}

	*crtc->mode = *adjusted_mode;
	crtc->use_dithering = connector->use_dithering;

	nv50_crtc_execute_mode(crtc, adjusted_mode);
	crtc->set_scale(crtc, connector->scaling_mode, false);
	crtc_helper->mode_set_base(drm_crtc, x, y);
}

static void
nv50_crtc_mode_set_base(struct drm_crtc *drm_crtc, int x, int y)
{
	struct nouveau_crtc *crtc = to_nouveau_crtc(drm_crtc);
	struct drm_device *dev = crtc->base.dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct drm_framebuffer *drm_fb = crtc->base.fb;
	struct nouveau_framebuffer *fb = to_nouveau_framebuffer(drm_fb);
	struct nouveau_gem_object *ngem = nouveau_gem_object(fb->gem);
	uint32_t v_vram = ngem->bo->offset - dev_priv->vm_vram_base;
	uint32_t offset = crtc->index * 0x400;

	OUT_MODE(NV50_CRTC0_FB_OFFSET + offset, v_vram >> 8);
	OUT_MODE(0x864 + offset, 0);

	OUT_MODE(NV50_CRTC0_FB_SIZE + offset,
		 drm_fb->height << 16 | drm_fb->width);

	/* I suspect this flag indicates a linear fb. */
	OUT_MODE(NV50_CRTC0_FB_PITCH + offset, drm_fb->pitch | 0x100000);

	switch (drm_fb->depth) {
	case 8:
		OUT_MODE(NV50_CRTC0_DEPTH + offset, NV50_CRTC0_DEPTH_8BPP); 
		break;
	case 15:
		OUT_MODE(NV50_CRTC0_DEPTH + offset, NV50_CRTC0_DEPTH_15BPP);
		break;
	case 16:
		OUT_MODE(NV50_CRTC0_DEPTH + offset, NV50_CRTC0_DEPTH_16BPP);
		break;
	case 24:
		OUT_MODE(NV50_CRTC0_DEPTH + offset, NV50_CRTC0_DEPTH_24BPP); 
		break;
	}

	OUT_MODE(NV50_CRTC0_COLOR_CTRL + offset,
		 NV50_CRTC_COLOR_CTRL_MODE_COLOR);
	OUT_MODE(NV50_CRTC0_FB_POS + offset, (y << 16) | x);

	if (crtc->lut.depth != fb->base.depth) {
		crtc->lut.depth = fb->base.depth;
		nv50_crtc_lut_load(crtc);
	}

	OUT_MODE(NV50_CRTC0_CLUT_MODE + offset, fb->base.depth == 8 ?
		 NV50_CRTC0_CLUT_MODE_OFF : NV50_CRTC0_CLUT_MODE_ON);
	OUT_MODE(NV50_CRTC0_CLUT_OFFSET + offset, crtc->lut.mem->start >> 8);

	OUT_MODE(NV50_UPDATE_DISPLAY, 0);
}


static const struct drm_crtc_helper_funcs nv50_crtc_helper_funcs = {
	.dpms = nv50_crtc_dpms,
	.prepare = nv50_crtc_prepare,
	.commit = nv50_crtc_commit,
	.mode_fixup = nv50_crtc_mode_fixup,
	.mode_set = nv50_crtc_mode_set,
	.mode_set_base = nv50_crtc_mode_set_base,
};

int nv50_crtc_create(struct drm_device *dev, int index)
{
	struct nouveau_crtc *crtc = NULL;
	int ret, i;

	DRM_DEBUG("\n");

	crtc = kzalloc(sizeof(*crtc) +
		       NOUVEAUFB_CONN_LIMIT * sizeof(struct drm_connector *),
		       GFP_KERNEL);
	if (!crtc)
		return -ENOMEM;

	crtc->mode = kzalloc(sizeof(*crtc->mode), GFP_KERNEL);
	if (!crtc->mode) {
		kfree(crtc);
		return -ENOMEM;
	}

	/* Default CLUT parameters, will be activated on the hw upon
	 * first mode set.
	 */
	for (i = 0; i < 256; i++) {
		crtc->lut.r[i] = i << 8;
		crtc->lut.g[i] = i << 8;
		crtc->lut.b[i] = i << 8;
	}
	crtc->lut.depth = 0;

	crtc->lut.mem = nouveau_mem_alloc(dev, 0x100, 4096, NOUVEAU_MEM_FB |
					  NOUVEAU_MEM_NOVM | NOUVEAU_MEM_MAPPED,
					  (struct drm_file *)-2);
	if (crtc->lut.mem) {
		ret = drm_bo_kmap(crtc->lut.mem->bo, 0,
				  crtc->lut.mem->bo->mem.num_pages,
				  &crtc->lut.mem->kmap);
		if (ret) {
			nouveau_mem_free(dev, crtc->lut.mem);
			crtc->lut.mem = NULL;
		}
	}

	if (!crtc->lut.mem) {
		kfree(crtc->mode);
		kfree(crtc);
		return -ENOMEM;
	}

	crtc->index = index;

	/* set function pointers */
	crtc->set_dither = nv50_crtc_set_dither;
	crtc->set_scale = nv50_crtc_set_scale;
	crtc->set_clock = nv50_crtc_set_clock;
	crtc->set_clock_mode = nv50_crtc_set_clock_mode;

	crtc->mode_set.crtc = &crtc->base;
	crtc->mode_set.connectors = (struct drm_connector **)(crtc + 1);
	crtc->mode_set.num_connectors = 0;

	drm_crtc_init(dev, &crtc->base, &nv50_crtc_funcs);
	drm_crtc_helper_add(&crtc->base, &nv50_crtc_helper_funcs);
	drm_mode_crtc_set_gamma_size(&crtc->base, 256);

	nv50_cursor_create(crtc);
	return 0;
}
