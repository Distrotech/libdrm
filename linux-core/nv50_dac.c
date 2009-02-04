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

#include "nv50_output.h"

static int nv50_dac_validate_mode(struct nv50_output *output,
				  struct drm_display_mode *mode)
{
	NV50_DEBUG("\n");

	if (mode->clock > 400000) 
		return MODE_CLOCK_HIGH;

	if (mode->clock < 25000)
		return MODE_CLOCK_LOW;

	return MODE_OK;
}

static int nv50_dac_execute_mode(struct nv50_output *output, bool disconnect)
{
	struct drm_encoder *drm_encoder = &output->base;
	struct drm_nouveau_private *dev_priv = drm_encoder->dev->dev_private;
	struct nv50_crtc *crtc = to_nv50_crtc(drm_encoder->crtc);
	struct drm_display_mode *desired_mode = NULL;
	uint32_t offset = nv50_output_or_offset(output) * 0x80;
	uint32_t mode_ctl = NV50_DAC_MODE_CTRL_OFF;
	uint32_t mode_ctl2 = 0;

	NV50_DEBUG("or %d\n", nv50_output_or_offset(output));

	if (disconnect) {
		NV50_DEBUG("Disconnecting DAC\n");
		OUT_MODE(NV50_DAC0_MODE_CTRL + offset, mode_ctl);
		return 0;
	}

	desired_mode = (crtc->use_native_mode ? crtc->native_mode :
									crtc->mode);

	if (crtc->index == 1)
		mode_ctl |= NV50_DAC_MODE_CTRL_CRTC1;
	else
		mode_ctl |= NV50_DAC_MODE_CTRL_CRTC0;

	/* Lacking a working tv-out, this is not a 100% sure. */
	if (output->base.encoder_type == DRM_MODE_ENCODER_DAC) {
		mode_ctl |= 0x40;
	} else
	if (output->base.encoder_type == DRM_MODE_ENCODER_TVDAC) {
		mode_ctl |= 0x100;
	}

	if (desired_mode->flags & DRM_MODE_FLAG_NHSYNC)
		mode_ctl2 |= NV50_DAC_MODE_CTRL2_NHSYNC;

	if (desired_mode->flags & DRM_MODE_FLAG_NVSYNC)
		mode_ctl2 |= NV50_DAC_MODE_CTRL2_NVSYNC;

	OUT_MODE(NV50_DAC0_MODE_CTRL + offset, mode_ctl);
	OUT_MODE(NV50_DAC0_MODE_CTRL2 + offset, mode_ctl2);

	return 0;
}

static int nv50_dac_set_clock_mode(struct nv50_output *output)
{
	struct drm_nouveau_private *dev_priv = output->base.dev->dev_private;

	NV50_DEBUG("or %d\n", nv50_output_or_offset(output));

	NV_WRITE(NV50_PDISPLAY_DAC_CLK_CLK_CTRL2(nv50_output_or_offset(output)),  0);

	return 0;
}

static int nv50_dac_set_power_mode(struct nv50_output *output, int mode)
{
	struct drm_nouveau_private *dev_priv = output->base.dev->dev_private;
	uint32_t val;
	int or = nv50_output_or_offset(output);

	NV50_DEBUG("or %d\n", or);

	/* wait for it to be done */
	while (NV_READ(NV50_PDISPLAY_DAC_REGS_DPMS_CTRL(or)) & NV50_PDISPLAY_DAC_REGS_DPMS_CTRL_PENDING);

	val = NV_READ(NV50_PDISPLAY_DAC_REGS_DPMS_CTRL(or)) & ~0x7F;

	if (mode != DRM_MODE_DPMS_ON)
		val |= NV50_PDISPLAY_DAC_REGS_DPMS_CTRL_BLANKED;

	switch (mode) {
	case DRM_MODE_DPMS_STANDBY:
		val |= NV50_PDISPLAY_DAC_REGS_DPMS_CTRL_HSYNC_OFF;
		break;
	case DRM_MODE_DPMS_SUSPEND:
		val |= NV50_PDISPLAY_DAC_REGS_DPMS_CTRL_VSYNC_OFF;
		break;
	case DRM_MODE_DPMS_OFF:
		val |= NV50_PDISPLAY_DAC_REGS_DPMS_CTRL_OFF;
		val |= NV50_PDISPLAY_DAC_REGS_DPMS_CTRL_HSYNC_OFF;
		val |= NV50_PDISPLAY_DAC_REGS_DPMS_CTRL_VSYNC_OFF;
		break;
	default:
		break;
	}

	NV_WRITE(NV50_PDISPLAY_DAC_REGS_DPMS_CTRL(or), val | NV50_PDISPLAY_DAC_REGS_DPMS_CTRL_PENDING);

	return 0;
}

static int nv50_dac_detect(struct nv50_output *output)
{
	struct drm_nouveau_private *dev_priv = output->base.dev->dev_private;
	int or = nv50_output_or_offset(output);
	bool present = 0;
	uint32_t dpms_state, load_pattern, load_state;

	NV_WRITE(NV50_PDISPLAY_DAC_REGS_CLK_CTRL1(or), 0x00000001);
	dpms_state = NV_READ(NV50_PDISPLAY_DAC_REGS_DPMS_CTRL(or));

	NV_WRITE(NV50_PDISPLAY_DAC_REGS_DPMS_CTRL(or), 0x00150000 | NV50_PDISPLAY_DAC_REGS_DPMS_CTRL_PENDING);
	while (NV_READ(NV50_PDISPLAY_DAC_REGS_DPMS_CTRL(or)) & NV50_PDISPLAY_DAC_REGS_DPMS_CTRL_PENDING);

	/* Use bios provided value if possible. */
	if (dev_priv->bios.dactestval) {
		load_pattern = dev_priv->bios.dactestval;
		NV50_DEBUG("Using bios provided load_pattern of %d\n", load_pattern);
	} else {
		load_pattern = 340;
		NV50_DEBUG("Using default load_pattern of %d\n", load_pattern);
	}

	NV_WRITE(NV50_PDISPLAY_DAC_REGS_LOAD_CTRL(or), NV50_PDISPLAY_DAC_REGS_LOAD_CTRL_ACTIVE | load_pattern);
	udelay(10000); /* give it some time to process */
	load_state = NV_READ(NV50_PDISPLAY_DAC_REGS_LOAD_CTRL(or));

	NV_WRITE(NV50_PDISPLAY_DAC_REGS_LOAD_CTRL(or), 0);
	NV_WRITE(NV50_PDISPLAY_DAC_REGS_DPMS_CTRL(or), dpms_state);

	if ((load_state & NV50_PDISPLAY_DAC_REGS_LOAD_CTRL_PRESENT) == NV50_PDISPLAY_DAC_REGS_LOAD_CTRL_PRESENT)
		present = 1;

	if (present)
		NV50_DEBUG("Load was detected on output with or %d\n", or);
	else
		NV50_DEBUG("Load was not detected on output with or %d\n", or);

	return present;
}

static void nv50_dac_destroy(struct drm_encoder *drm_encoder)
{
	struct nv50_output *output = to_nv50_output(drm_encoder);

	NV50_DEBUG("\n");

	if (!drm_encoder)
		return;

	drm_encoder_cleanup(&output->base);

	kfree(output->native_mode);
	kfree(output);
}

static const struct drm_encoder_funcs nv50_dac_encoder_funcs = {
	.destroy = nv50_dac_destroy,
};

int nv50_dac_create(struct drm_device *dev, int dcb_entry)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nv50_output *output = NULL;
	struct nv50_display *display = NULL;
	struct dcb_entry *entry = NULL;

	NV50_DEBUG("\n");

	display = nv50_get_display(dev);
	entry = &dev_priv->dcb_table.entry[dcb_entry];
	if (!display || dcb_entry >= dev_priv->dcb_table.entries)
		return -EINVAL;

	switch (entry->type) {
	case DCB_OUTPUT_ANALOG:
		DRM_INFO("Detected a DAC output\n");
		break;
	default:
		return -EINVAL;
	}

	output = kzalloc(sizeof(*output), GFP_KERNEL);
	if (!output)
		return -ENOMEM;

	output->native_mode = kzalloc(sizeof(*output->native_mode), GFP_KERNEL);
	if (!output->native_mode) {
		kfree(output);
		return -ENOMEM;
	}

	output->dcb_entry = dcb_entry;
	output->bus = entry->bus;

	/* Set function pointers. */
	output->validate_mode = nv50_dac_validate_mode;
	output->execute_mode = nv50_dac_execute_mode;
	output->set_clock_mode = nv50_dac_set_clock_mode;
	output->set_power_mode = nv50_dac_set_power_mode;
	output->detect = nv50_dac_detect;

	drm_encoder_init(dev, &output->base, &nv50_dac_encoder_funcs,
			 DRM_MODE_ENCODER_DAC);
	/* I've never seen possible crtc's restricted. */
	output->base.possible_crtcs = 3;
	output->base.possible_clones = 0;
	return 0;
}

