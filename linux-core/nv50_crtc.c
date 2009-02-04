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

#include "nv50_crtc.h"
#include "nv50_cursor.h"
#include "nv50_lut.h"
#include "nv50_fb.h"
#include "nv50_connector.h"

static int nv50_crtc_validate_mode(struct nv50_crtc *crtc, struct drm_display_mode *mode)
{
	DRM_DEBUG("\n");

	if (mode->clock > 400000)
		return MODE_CLOCK_HIGH;

	if (mode->clock < 25000)
		return MODE_CLOCK_LOW;

	return MODE_OK;
}

static int nv50_crtc_set_mode(struct nv50_crtc *crtc, struct drm_display_mode *mode)
{
	struct drm_display_mode *hw_mode = crtc->mode;
	uint8_t rval;

	DRM_DEBUG("index %d\n", crtc->index);

	if (!mode) {
		DRM_ERROR("No mode\n");
		return MODE_NOMODE;
	}

	if ((rval = crtc->validate_mode(crtc, mode))) {
		DRM_ERROR("Mode invalid\n");
		return rval;
	}

	/* copy values to mode */
	*hw_mode = *mode;
	return 0;
}

static int nv50_crtc_execute_mode(struct nv50_crtc *crtc)
{
	struct drm_nouveau_private *dev_priv = crtc->base.dev->dev_private;
	struct drm_display_mode *mode;
	uint32_t hsync_dur,  vsync_dur, hsync_start_to_end, vsync_start_to_end;
	uint32_t hunk1, vunk1, vunk2a, vunk2b;
	uint32_t offset = crtc->index * 0x400;

	DRM_DEBUG("index %d\n", crtc->index);
	DRM_DEBUG("%s native mode\n", crtc->use_native_mode ? "using" : "not using");

	if (crtc->use_native_mode)
		mode = crtc->native_mode;
	else
		mode = crtc->mode;

	hsync_dur = mode->hsync_end - mode->hsync_start;
	vsync_dur = mode->vsync_end - mode->vsync_start;
	hsync_start_to_end = mode->htotal - mode->hsync_start;
	vsync_start_to_end = mode->vtotal - mode->vsync_start;
	/* I can't give this a proper name, anyone else can? */
	hunk1 = mode->htotal - mode->hsync_start + mode->hdisplay + 1;
	vunk1 = mode->vtotal - mode->vsync_start + mode->vdisplay + 1;
	/* Another strange value, this time only for interlaced modes. */
	vunk2a = 2*mode->vtotal - mode->vsync_start + mode->vdisplay + 1;
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
	OUT_MODE(NV50_CRTC0_INTERLACE + offset, (mode->flags & DRM_MODE_FLAG_INTERLACE) ? 2 : 0);
	OUT_MODE(NV50_CRTC0_DISPLAY_START + offset, 0);
	OUT_MODE(NV50_CRTC0_UNK82C + offset, 0);
	OUT_MODE(NV50_CRTC0_DISPLAY_TOTAL + offset, mode->vtotal << 16 | mode->htotal);
	OUT_MODE(NV50_CRTC0_SYNC_DURATION + offset, (vsync_dur - 1) << 16 | (hsync_dur - 1));
	OUT_MODE(NV50_CRTC0_SYNC_START_TO_BLANK_END + offset, (vsync_start_to_end - 1) << 16 | (hsync_start_to_end - 1));
	OUT_MODE(NV50_CRTC0_MODE_UNK1 + offset, (vunk1 - 1) << 16 | (hunk1 - 1));
	if (mode->flags & DRM_MODE_FLAG_INTERLACE) {
		OUT_MODE(NV50_CRTC0_MODE_UNK2 + offset, (vunk2b - 1) << 16 | (vunk2a - 1));
	}

	crtc->set_fb(crtc);
	crtc->set_dither(crtc);

	/* This is the actual resolution of the mode. */
	OUT_MODE(NV50_CRTC0_REAL_RES + offset, (crtc->mode->vdisplay << 16) | crtc->mode->hdisplay);
	OUT_MODE(NV50_CRTC0_SCALE_CENTER_OFFSET + offset, NV50_CRTC_SCALE_CENTER_OFFSET_VAL(0,0));

	/* Maybe move this as well? */
	crtc->blank(crtc, false);

	return 0;
}

static int nv50_crtc_set_fb(struct nv50_crtc *crtc)
{
	struct drm_nouveau_private *dev_priv = crtc->base.dev->dev_private;
	struct drm_framebuffer *drm_fb = crtc->base.fb;
	uint32_t offset = crtc->index * 0x400;

	DRM_DEBUG("\n");

	OUT_MODE(NV50_CRTC0_FB_SIZE + offset, drm_fb->height << 16 | drm_fb->width);

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

	OUT_MODE(NV50_CRTC0_COLOR_CTRL + offset, NV50_CRTC_COLOR_CTRL_MODE_COLOR);
	OUT_MODE(NV50_CRTC0_FB_POS + offset, (crtc->base.desired_y << 16) | (crtc->base.desired_x));

	return 0;
}

static int nv50_crtc_blank(struct nv50_crtc *crtc, bool blanked)
{
	struct drm_nouveau_private *dev_priv = crtc->base.dev->dev_private;
	uint32_t offset = crtc->index * 0x400;

	DRM_DEBUG("index %d\n", crtc->index);
	DRM_DEBUG("%s\n", blanked ? "blanked" : "unblanked");

	if (blanked) {
		crtc->cursor->hide(crtc);

		OUT_MODE(NV50_CRTC0_CLUT_MODE + offset, NV50_CRTC0_CLUT_MODE_BLANK);
		OUT_MODE(NV50_CRTC0_CLUT_OFFSET + offset, 0);
		if (dev_priv->chipset != 0x50)
			OUT_MODE(NV84_CRTC0_BLANK_UNK1 + offset, NV84_CRTC0_BLANK_UNK1_BLANK);
		OUT_MODE(NV50_CRTC0_BLANK_CTRL + offset, NV50_CRTC0_BLANK_CTRL_BLANK);
		if (dev_priv->chipset != 0x50)
			OUT_MODE(NV84_CRTC0_BLANK_UNK2 + offset, NV84_CRTC0_BLANK_UNK2_BLANK);
	} else {
		struct nv50_framebuffer *fb = to_nv50_framebuffer(crtc->base.fb);
		struct nouveau_gem_object *ngem = nouveau_gem_object(fb->gem);
		uint32_t v_vram = ngem->bo->offset - dev_priv->vm_vram_base;
		uint32_t v_lut = crtc->lut->bo->offset - dev_priv->vm_vram_base;

		OUT_MODE(NV50_CRTC0_FB_OFFSET + offset, v_vram >> 8);
		OUT_MODE(0x864 + offset, 0);

		crtc->cursor->set_offset(crtc);

		if (dev_priv->chipset != 0x50)
			OUT_MODE(NV84_CRTC0_BLANK_UNK2 + offset, NV84_CRTC0_BLANK_UNK2_UNBLANK);

		if (crtc->cursor->visible)
			crtc->cursor->show(crtc);
		else
			crtc->cursor->hide(crtc);

		OUT_MODE(NV50_CRTC0_CLUT_MODE + offset, 
			fb->base.depth == 8 ? NV50_CRTC0_CLUT_MODE_OFF : NV50_CRTC0_CLUT_MODE_ON);
		OUT_MODE(NV50_CRTC0_CLUT_OFFSET + offset, v_lut >> 8);
		if (dev_priv->chipset != 0x50)
			OUT_MODE(NV84_CRTC0_BLANK_UNK1 + offset, NV84_CRTC0_BLANK_UNK1_UNBLANK);
		OUT_MODE(NV50_CRTC0_BLANK_CTRL + offset, NV50_CRTC0_BLANK_CTRL_UNBLANK);
	}

	/* sometimes you need to know if a screen is already blanked. */
	crtc->blanked = blanked;

	return 0;
}

static int nv50_crtc_set_dither(struct nv50_crtc *crtc)
{
	struct drm_nouveau_private *dev_priv = crtc->base.dev->dev_private;
	uint32_t offset = crtc->index * 0x400;

	DRM_DEBUG("\n");

	OUT_MODE(NV50_CRTC0_DITHERING_CTRL + offset, crtc->use_dithering ? 
			NV50_CRTC0_DITHERING_CTRL_ON : NV50_CRTC0_DITHERING_CTRL_OFF);

	return 0;
}

static void nv50_crtc_calc_scale(struct nv50_crtc *crtc, uint32_t *outX, uint32_t *outY)
{
	uint32_t hor_scale, ver_scale;

	/* max res is 8192, which is 2^13, which leaves 19 bits */
	hor_scale = (crtc->native_mode->hdisplay << 19)/crtc->mode->hdisplay;
	ver_scale = (crtc->native_mode->vdisplay << 19)/crtc->mode->vdisplay;

	if (ver_scale > hor_scale) {
		*outX = (crtc->mode->hdisplay * hor_scale) >> 19;
		*outY = (crtc->mode->vdisplay * hor_scale) >> 19;
	} else {
		*outX = (crtc->mode->hdisplay * ver_scale) >> 19;
		*outY = (crtc->mode->vdisplay * ver_scale) >> 19;
	}
}

static int nv50_crtc_set_scale(struct nv50_crtc *crtc)
{
	struct drm_nouveau_private *dev_priv = crtc->base.dev->dev_private;
	uint32_t offset = crtc->index * 0x400;
	uint32_t outX, outY;

	DRM_DEBUG("\n");

	switch (crtc->requested_scaling_mode) {
	case DRM_MODE_SCALE_ASPECT:
		nv50_crtc_calc_scale(crtc, &outX, &outY);
		break;
	case DRM_MODE_SCALE_FULLSCREEN:
		outX = crtc->native_mode->hdisplay;
		outY = crtc->native_mode->vdisplay;
		break;
	case DRM_MODE_SCALE_NO_SCALE:
	case DRM_MODE_SCALE_NON_GPU:
	default:
		outX = crtc->mode->hdisplay;
		outY = crtc->mode->vdisplay;
		break;
	}

	/* Got a better name for SCALER_ACTIVE? */
	/* One day i've got to really figure out why this is needed. */
	if ((crtc->mode->flags & DRM_MODE_FLAG_DBLSCAN) || (crtc->mode->flags & DRM_MODE_FLAG_INTERLACE) ||
		crtc->mode->hdisplay != outX || crtc->mode->vdisplay != outY) {
		OUT_MODE(NV50_CRTC0_SCALE_CTRL + offset, NV50_CRTC0_SCALE_CTRL_SCALER_ACTIVE);
	} else {
		OUT_MODE(NV50_CRTC0_SCALE_CTRL + offset, NV50_CRTC0_SCALE_CTRL_SCALER_INACTIVE);
	}

	OUT_MODE(NV50_CRTC0_SCALE_RES1 + offset, outY << 16 | outX);
	OUT_MODE(NV50_CRTC0_SCALE_RES2 + offset, outY << 16 | outX);

	/* processed */
	crtc->scaling_mode = crtc->requested_scaling_mode;

	return 0;
}

static int nv50_crtc_calc_clock(struct nv50_crtc *crtc, 
	uint32_t *bestN1, uint32_t *bestN2, uint32_t *bestM1, uint32_t *bestM2, uint32_t *bestlog2P)
{
	struct drm_display_mode *mode;
	struct pll_lims limits;
	int clk, vco2, crystal;
	int minvco1, minvco2, minU1, maxU1, minU2, maxU2, minM1, maxM1;
	int maxvco1, maxvco2, minN1, maxN1, minM2, maxM2, minN2, maxN2;
	bool fixedgain2;
	int M1, N1, M2, N2, log2P;
	int clkP, calcclk1, calcclk2, calcclkout;
	int delta, bestdelta = INT_MAX;
	int bestclk = 0;

	DRM_DEBUG("\n");

	if (crtc->use_native_mode)
		mode = crtc->native_mode;
	else
		mode = crtc->mode;

	clk = mode->clock;

	/* These are in the g80 bios tables, at least in mine. */
	if (!get_pll_limits(crtc->base.dev, NV50_PDISPLAY_CRTC_CLK_CLK_CTRL1(crtc->index), &limits))
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
	for (log2P = 0; clk && log2P < 6 && clk <= (vco2 >> log2P); log2P++) /* log2P is maximum of 6 */
		;
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
				/* we do an exhaustive search rather than terminating
				 * on an optimality condition...
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

static int nv50_crtc_set_clock(struct nv50_crtc *crtc)
{
	struct drm_nouveau_private *dev_priv = crtc->base.dev->dev_private;

	uint32_t pll_reg = NV50_PDISPLAY_CRTC_CLK_CLK_CTRL1(crtc->index);

	uint32_t N1 = 0, N2 = 0, M1 = 0, M2 = 0, log2P = 0;

	uint32_t reg1 = NV_READ(pll_reg + 4);
	uint32_t reg2 = NV_READ(pll_reg + 8);

	DRM_DEBUG("\n");

	NV_WRITE(pll_reg, NV50_PDISPLAY_CRTC_CLK_CLK_CTRL1_CONNECTED | 0x10000011);

	/* The other bits are typically empty, but let's be on the safe side. */
	reg1 &= 0xff00ff00;
	reg2 &= 0x8000ff00;

	if (!nv50_crtc_calc_clock(crtc, &N1, &N2, &M1, &M2, &log2P))
		return -EINVAL;

	DRM_DEBUG("N1 %d N2 %d M1 %d M2 %d log2P %d\n", N1, N2, M1, M2, log2P);

	reg1 |= (M1 << 16) | N1;
	reg2 |= (log2P << 28) | (M2 << 16) | N2;

	NV_WRITE(pll_reg + 4, reg1);
	NV_WRITE(pll_reg + 8, reg2);

	return 0;
}

static int nv50_crtc_set_clock_mode(struct nv50_crtc *crtc)
{
	struct drm_nouveau_private *dev_priv = crtc->base.dev->dev_private;

	DRM_DEBUG("\n");

	/* This acknowledges a clock request. */
	NV_WRITE(NV50_PDISPLAY_CRTC_CLK_CLK_CTRL2(crtc->index), 0);

	return 0;
}

static void nv50_crtc_destroy(struct drm_crtc *drm_crtc)
{
	struct nv50_crtc *crtc = to_nv50_crtc(drm_crtc);

	DRM_DEBUG("\n");

	if (!crtc)
		return;

	drm_crtc_cleanup(&crtc->base);

	nv50_lut_destroy(crtc);
	nv50_cursor_destroy(crtc);

	kfree(crtc->mode);
	kfree(crtc->native_mode);
	kfree(crtc);
}

static int nv50_crtc_cursor_set(struct drm_crtc *drm_crtc,
				struct drm_file *file_priv,
				uint32_t buffer_handle,
				uint32_t width, uint32_t height)
{
	struct drm_device *dev = drm_crtc->dev;
	struct nv50_crtc *crtc = to_nv50_crtc(drm_crtc);
	struct nv50_display *display = nv50_get_display(crtc->base.dev);
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

	display->update(display);
	return ret;
}

static int nv50_crtc_cursor_move(struct drm_crtc *drm_crtc, int x, int y)
{
	struct nv50_crtc *crtc = to_nv50_crtc(drm_crtc);

	return crtc->cursor->set_pos(crtc, x, y);
}

void nv50_crtc_gamma_set(struct drm_crtc *drm_crtc, u16 *r, u16 *g, u16 *b,
			 uint32_t size)
{
	struct nv50_crtc *crtc = to_nv50_crtc(drm_crtc);

	if (size != 256)
		return;

	crtc->lut->set(crtc, (uint16_t *)r, (uint16_t *)g, (uint16_t *)b);
}

int nv50_crtc_set_config(struct drm_mode_set *set)
{
	int rval = 0, i;
	uint32_t crtc_mask = 0;
	struct drm_device *dev = NULL;
	struct drm_nouveau_private *dev_priv = NULL;
	struct nv50_display *display = NULL;
	struct drm_connector *drm_connector = NULL;
	struct drm_encoder *drm_encoder = NULL;
	struct drm_crtc *drm_crtc = NULL;

	struct nv50_crtc *crtc = NULL;
	struct nv50_output *output = NULL;
	struct nv50_connector *connector = NULL;

	bool blank = false;
	bool switch_fb = false;
	bool modeset = false;

	DRM_DEBUG("\n");

	/*
	 * Supported operations:
	 * - Switch mode.
	 * - Switch framebuffer.
	 * - Blank screen.
	 */

	/* Sanity checking */
	if (!set) {
		DRM_ERROR("Sanity check failed\n");
		goto out;
	}

	if (!set->crtc) {
		DRM_ERROR("Sanity check failed\n");
		goto out;
	}

	if (set->mode) {
		if (set->fb) {
			if (!drm_mode_equal(set->mode, &set->crtc->mode))
				modeset = true;

			if (set->fb != set->crtc->fb)
				switch_fb = true;

			if (set->x != set->crtc->x || set->y != set->crtc->y)
				switch_fb = true;
		}
	} else {
		blank = true;
	}

	if (!set->connectors && !blank) {
		DRM_ERROR("Sanity check failed\n");
		goto out;
	}

	/* Basic variable setting */
	dev = set->crtc->dev;
	dev_priv = dev->dev_private;
	display = nv50_get_display(dev);
	crtc = to_nv50_crtc(set->crtc);

	/**
	 * Wiring up the encoders and connectors.
	 */

	/* for switch_fb we verify if any important changes happened */
	if (!blank) {
		/* Mode validation */

		rval = crtc->validate_mode(crtc, set->mode);
		if (rval != MODE_OK) {
			DRM_ERROR("Mode not ok\n");
			goto out;
		}

		for (i = 0; i < set->num_connectors; i++) {
			drm_connector = set->connectors[i];
			if (!drm_connector) {
				DRM_ERROR("No connector\n");
				goto out;
			}
			connector = to_nv50_connector(drm_connector);

			/* This is to ensure it knows the connector subtype. */
			drm_connector->funcs->fill_modes(drm_connector, 0, 0);

			output = connector->to_output(connector, nv50_connector_get_digital(drm_connector));
			if (!output) {
				DRM_ERROR("No output\n");
				goto out;
			}

			rval = output->validate_mode(output, set->mode);
			if (rval != MODE_OK) {
				DRM_ERROR("Mode not ok\n");
				goto out;
			}

			/* verify if any "sneaky" changes happened */
			if (&output->base != drm_connector->encoder)
				modeset = true;

			if (output->base.crtc != &crtc->base)
				modeset = true;
		}
	}

	/* Now we verified if anything changed, fail if nothing has. */
	if (!modeset && !switch_fb && !blank)
		DRM_INFO("A seemingly empty modeset encountered, this could be a bug.\n");

	/* Validation done, move on to cleaning of existing structures. */
	if (modeset) {
		/* find encoders that use this crtc. */
		list_for_each_entry(drm_encoder, &dev->mode_config.encoder_list, head) {
			if (drm_encoder->crtc == set->crtc) {
				/* find the connector that goes with it */
				list_for_each_entry(drm_connector, &dev->mode_config.connector_list, head) {
					if (drm_connector->encoder == drm_encoder) {
						drm_connector->encoder =  NULL;
						break;
					}
				}
				drm_encoder->crtc = NULL;
			}
		}

		/* now find if our desired encoders or connectors are in use already. */
		for (i = 0; i < set->num_connectors; i++) {
			drm_connector = set->connectors[i];
			if (!drm_connector) {
				DRM_ERROR("No connector\n");
				goto out;
			}

			if (!drm_connector->encoder)
				continue;

			drm_encoder = drm_connector->encoder;
			drm_connector->encoder = NULL;

			if (!drm_encoder->crtc)
				continue;

			drm_crtc = drm_encoder->crtc;
			drm_encoder->crtc = NULL;

			drm_crtc->enabled = false;
		}

		/* Time to wire up the public encoder, the private one will be handled later. */
		for (i = 0; i < set->num_connectors; i++) {
			drm_connector = set->connectors[i];
			if (!drm_connector) {
				DRM_ERROR("No connector\n");
				goto out;
			}

			output = connector->to_output(connector, nv50_connector_get_digital(drm_connector));
			if (!output) {
				DRM_ERROR("No output\n");
				goto out;
			}

			output->base.crtc = set->crtc;
			set->crtc->enabled = true;
			drm_connector->encoder = &output->base;
		}
	}

	/**
	 * Disable crtc.
	 */

	if (blank) {
		crtc = to_nv50_crtc(set->crtc);

		set->crtc->enabled = false;

		/* disconnect encoders and connectors */
		for (i = 0; i < set->num_connectors; i++) {
			drm_connector = set->connectors[i];

			if (!drm_connector->encoder)
				continue;

			drm_connector->encoder->crtc = NULL;
			drm_connector->encoder = NULL;
		}
	}

	/**
	 * All state should now be updated, now onto the real work.
	 */

	/**
	 * Bind framebuffer.
	 */

	if (switch_fb) {
		struct nv50_crtc *crtc = to_nv50_crtc(set->crtc);
		int r_size = 0, g_size = 0, b_size = 0;
		uint16_t *r_val, *g_val, *b_val;
		int i;

		/* set framebuffer */
		set->crtc->fb = set->fb;
		set->crtc->desired_x = set->x;
		set->crtc->desired_y = set->y;

		switch (set->crtc->fb->depth) {
		case 15:
			r_size = 32;
			g_size = 32;
			b_size = 32;
			break;
		case 16:
			r_size = 32;
			g_size = 64;
			b_size = 32;
			break;
		case 24:
		default:
			r_size = 256;
			g_size = 256;
			b_size = 256;
			break;
		}

		r_val = kmalloc(r_size * sizeof(uint16_t), GFP_KERNEL);
		g_val = kmalloc(g_size * sizeof(uint16_t), GFP_KERNEL);
		b_val = kmalloc(b_size * sizeof(uint16_t), GFP_KERNEL);

		if (!r_val || !g_val || !b_val)
			return -ENOMEM;

		/* Set the color indices. */
		for (i = 0; i < r_size; i++) {
			r_val[i] = i << 8;
		}
		for (i = 0; i < g_size; i++) {
			g_val[i] = i << 8;
		}
		for (i = 0; i < b_size; i++) {
			b_val[i] = i << 8;
		}

		rval = crtc->lut->set(crtc, r_val, g_val, b_val);

		/* free before returning */
		kfree(r_val);
		kfree(g_val);
		kfree(b_val);

		if (rval != 0)
			return rval;
	}

	/* this is !cursor_show */
	if (!crtc->cursor->enabled) {
		rval = crtc->cursor->enable(crtc);
		if (rval != 0) {
			DRM_ERROR("cursor_enable failed\n");
			goto out;
		}
	}

	/**
	 * Blanking.
	 */

	if (blank) {
		crtc = to_nv50_crtc(set->crtc);

		rval = crtc->blank(crtc, true);
		if (rval != 0) {
			DRM_ERROR("blanking failed\n");
			goto out;
		}

		/* detach any outputs that are currently unused */
		list_for_each_entry(drm_encoder, &dev->mode_config.encoder_list, head) {
			if (!drm_encoder->crtc) {
				output = to_nv50_output(drm_encoder);

				rval = output->execute_mode(output, true);
				if (rval != 0) {
					DRM_ERROR("detaching output failed\n");
					goto out;
				}
			}
		}
	}

	/**
	 * Change framebuffer, without changing mode.
	 */

	if (switch_fb && !modeset && !blank) {
		crtc = to_nv50_crtc(set->crtc);

		rval = crtc->set_fb(crtc);
		if (rval != 0) {
			DRM_ERROR("set_fb failed\n");
			goto out;
		}

		/* this also sets the fb offset */
		rval = crtc->blank(crtc, false);
		if (rval != 0) {
			DRM_ERROR("unblanking failed\n");
			goto out;
		}
	}

	/**
	 * Normal modesetting.
	 */

	if (modeset) {
		crtc = to_nv50_crtc(set->crtc);

		/* disconnect unused outputs */
		list_for_each_entry(drm_encoder, &dev->mode_config.encoder_list, head) {
			output = to_nv50_output(drm_encoder);

			if (drm_encoder->crtc) {
				crtc = to_nv50_crtc(drm_encoder->crtc);
				crtc_mask |= 1 << crtc->index;
			} else {
				rval = output->execute_mode(output, true);
				if (rval != 0) {
					DRM_ERROR("detaching output failed\n");
					goto out;
				}
			}
		}

		/* blank any unused crtcs */
		list_for_each_entry(drm_crtc, &dev->mode_config.crtc_list, head) {
			crtc = to_nv50_crtc(drm_crtc);
			if (!(crtc_mask & (1 << crtc->index)))
				crtc->blank(crtc, true);
		}

		crtc = to_nv50_crtc(set->crtc);

		rval = crtc->set_mode(crtc, set->mode);
		if (rval != 0) {
			DRM_ERROR("crtc mode set failed\n");
			goto out;
		}

		/* find native mode. */
		list_for_each_entry(drm_encoder, &dev->mode_config.encoder_list, head) {
			output = to_nv50_output(drm_encoder);
			if (drm_encoder->crtc != &crtc->base)
				continue;

			*crtc->native_mode = *output->native_mode;
			list_for_each_entry(drm_connector, &dev->mode_config.connector_list, head) {
				connector = to_nv50_connector(drm_connector);
				if (drm_connector->encoder != drm_encoder)
					continue;

				crtc->requested_scaling_mode = connector->requested_scaling_mode;
				crtc->use_dithering = connector->use_dithering;
				break;
			}

			if (crtc->requested_scaling_mode == DRM_MODE_SCALE_NON_GPU)
				crtc->use_native_mode = false;
			else
				crtc->use_native_mode = true;

			break; /* no use in finding more than one mode */
		}

		rval = crtc->execute_mode(crtc);
		if (rval != 0) {
			DRM_ERROR("crtc execute mode failed\n");
			goto out;
		}


		list_for_each_entry(drm_encoder, &dev->mode_config.encoder_list, head) {
			output = to_nv50_output(drm_encoder);
			if (drm_encoder->crtc != &crtc->base)
				continue;

			rval = output->execute_mode(output, false);
			if (rval != 0) {
				DRM_ERROR("output execute mode failed\n");
				goto out;
			}
		}

		rval = crtc->set_scale(crtc);
		if (rval != 0) {
			DRM_ERROR("crtc set scale failed\n");
			goto out;
		}

		/* next line changes crtc, so putting it here is important */
		display->last_crtc = crtc->index;
	}

	/* always reset dpms, regardless if any other modesetting is done. */
	if (!blank) {
		/* this is executed immediately */
		list_for_each_entry(drm_encoder, &dev->mode_config.encoder_list, head) {
			output = to_nv50_output(drm_encoder);
			if (drm_encoder->crtc != &crtc->base)
				continue;

			rval = output->set_power_mode(output, DRM_MODE_DPMS_ON);
			if (rval != 0) {
				DRM_ERROR("output set power mode failed\n");
				goto out;
			}
		}

		/* update dpms state to DPMSModeOn */
		for (i = 0; i < set->num_connectors; i++) {
			drm_connector = set->connectors[i];
			if (!drm_connector) {
				DRM_ERROR("No connector\n");
				goto out;
			}

			rval = drm_connector_property_set_value(drm_connector,
					dev->mode_config.dpms_property,
					DRM_MODE_DPMS_ON);
			if (rval != 0) {
				DRM_ERROR("failed to update dpms state\n");
				goto out;
			}
		}
	}

	display->update(display);

	/* Update the current mode, now that all has gone well. */
	if (modeset) {
		set->crtc->mode = *(set->mode);
		set->crtc->x = set->x;
		set->crtc->y = set->y;
	}

	return 0;

out:
	if (rval != 0)
		return rval;
	else
		return -EINVAL;
}

static const struct drm_crtc_funcs nv50_crtc_funcs = {
	.save = NULL,
	.restore = NULL,
	.cursor_set = nv50_crtc_cursor_set,
	.cursor_move = nv50_crtc_cursor_move,
	.gamma_set = nv50_crtc_gamma_set,
	.set_config = nv50_crtc_set_config,
	.destroy = nv50_crtc_destroy,
};

int nv50_crtc_create(struct drm_device *dev, int index)
{
	struct nv50_crtc *crtc = NULL;
	struct nv50_display *display = NULL;

	DRM_DEBUG("\n");

	display = nv50_get_display(dev);
	if (!display)
		return -EINVAL;

	crtc = kzalloc(sizeof(*crtc), GFP_KERNEL);
	if (!crtc)
		return -ENOMEM;

	crtc->mode = kzalloc(sizeof(*crtc->mode), GFP_KERNEL);
	if (!crtc->mode) {
		kfree(crtc);
		return -ENOMEM;
	}

	crtc->native_mode = kzalloc(sizeof(*crtc->native_mode), GFP_KERNEL);
	if (!crtc->native_mode) {
		kfree(crtc->mode);
		kfree(crtc);
		return -ENOMEM;
	}

	crtc->index = index;
	crtc->requested_scaling_mode = DRM_MODE_SCALE_NO_SCALE;
	crtc->scaling_mode = DRM_MODE_SCALE_NO_SCALE;

	/* set function pointers */
	crtc->validate_mode = nv50_crtc_validate_mode;
	crtc->set_mode = nv50_crtc_set_mode;
	crtc->execute_mode = nv50_crtc_execute_mode;
	crtc->set_fb = nv50_crtc_set_fb;
	crtc->blank = nv50_crtc_blank;
	crtc->set_dither = nv50_crtc_set_dither;
	crtc->set_scale = nv50_crtc_set_scale;
	crtc->set_clock = nv50_crtc_set_clock;
	crtc->set_clock_mode = nv50_crtc_set_clock_mode;

	drm_crtc_init(dev, &crtc->base, &nv50_crtc_funcs);
	drm_mode_crtc_set_gamma_size(&crtc->base, 256);

	nv50_lut_create(crtc);
	nv50_cursor_create(crtc);
	return 0;
}
