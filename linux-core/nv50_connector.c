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
#include "drm_edid.h"
#include "drm_crtc_helper.h"
#include "nouveau_reg.h"
#include "nouveau_drv.h"
#include "nouveau_encoder.h"
#include "nouveau_crtc.h"
#include "nouveau_connector.h"
#include "nv50_display_commands.h"

static struct drm_display_mode *
nv50_connector_lvds_native_mode(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct drm_display_mode *mode;
	uint32_t output, tmp;

	output = nv_rd32(0x610050);
	if ((output & 0x00000003) == 0x00000002)
		output = 0x00000000;
	else
	if ((output & 0x00000300) == 0x00000200)
		output = 0x000000540;
	else {
		DRM_ERROR("Unable to determine LVDS head: 0x%08x\n", output);
		return NULL;
	}

	mode = drm_mode_create(dev);
	if (!mode)
		return NULL;

	mode->clock = nv_rd32(0x610ad4 + output) & 0x003fffff;
	tmp = nv_rd32(0x610b4c + output);
	mode->hdisplay = (tmp & 0x00003fff);
	mode->vdisplay = (tmp & 0x3fff0000) >> 16;
	tmp = nv_rd32(0x610afc + output);
	mode->htotal = tmp & 0x3fff;
	mode->vtotal = tmp >> 16;
	tmp = nv_rd32(0x610ae8 + output);
	mode->hsync_start = mode->htotal - (tmp & 0x3fff) - 1;
	mode->vsync_start = mode->vtotal - (tmp >> 16) - 1;
	tmp = nv_rd32(0x610b00 + output);
	mode->hsync_end = mode->hsync_start + (tmp & 0x3fff) + 1;
	mode->vsync_end = mode->vsync_start + (tmp >> 16) + 1;
	mode->flags = 0;
	mode->type = DRM_MODE_TYPE_PREFERRED | DRM_MODE_TYPE_DRIVER;
	mode->vrefresh = drm_mode_vrefresh(mode);

	DRM_DEBUG("Probed current LVDS mode:\n");
	drm_mode_debug_printmodeline(mode);
	return mode;
}

static struct nouveau_encoder *
nv50_connector_to_encoder(struct nouveau_connector *connector, bool digital)
{
	struct drm_device *dev = connector->base.dev;
	struct drm_encoder *drm_encoder;
	bool digital_possible = false;
	bool analog_possible = false;

	switch (connector->base.connector_type) {
	case DRM_MODE_CONNECTOR_VGA:
	case DRM_MODE_CONNECTOR_SVIDEO:
		analog_possible = true;
		break;
	case DRM_MODE_CONNECTOR_DVII:
		analog_possible = true;
		digital_possible = true;
		break;
	case DRM_MODE_CONNECTOR_DVID:
	case DRM_MODE_CONNECTOR_LVDS:
		digital_possible = true;
		break;
	default:
		return NULL;
	}

	/* Return early on bad situations. */
	if (!analog_possible && !digital)
		return NULL;

	if (!digital_possible && digital)
		return NULL;

	list_for_each_entry(drm_encoder, &dev->mode_config.encoder_list, head) {
		struct nouveau_encoder *encoder = to_nouveau_encoder(drm_encoder);

		if (connector->bus != encoder->dcb_entry->bus)
			continue;

		if (digital) {
			switch (encoder->base.encoder_type) {
			case DRM_MODE_ENCODER_TMDS:
			case DRM_MODE_ENCODER_LVDS:
				return encoder;
			default:
				break;
			}
		} else {
			switch (encoder->base.encoder_type) {
			case DRM_MODE_ENCODER_DAC:
			case DRM_MODE_ENCODER_TVDAC:
				return encoder;
			default:
				break;
			}
		}
	}

	return NULL;
}

static bool nv50_connector_hpd_detect(struct nouveau_connector *connector)
{
	struct drm_nouveau_private *dev_priv = connector->base.dev->dev_private;
	uint32_t bus = connector->bus, reg;
	bool present = false;

	reg = nv_rd32(NV50_PCONNECTOR_HOTPLUG_STATE);
	if (reg & (NV50_PCONNECTOR_HOTPLUG_STATE_PIN_CONNECTED_I2C0 << (4 * bus)))
		present = true;

	DRM_DEBUG("hpd_detect: bus=%d reg=0x%08x present=%d\n",
		  connector->bus, reg, present);
	return present;
}

static bool nv50_connector_i2c_detect(struct nouveau_connector *connector)
{
	/* kindly borrrowed from the intel driver, hope it works. */
	uint8_t out_buf[] = { 0x0, 0x0};
	uint8_t buf[2];
	bool ret;
	struct i2c_msg msgs[] = {
		{
			.addr = 0x50,
			.flags = 0,
			.len = 1,
			.buf = out_buf,
		},
		{
			.addr = 0x50,
			.flags = I2C_M_RD,
			.len = 1,
			.buf = buf,
		}
	};

	if (!connector->i2c_chan)
		return false;

	ret = (i2c_transfer(&connector->i2c_chan->adapter, msgs, 2) == 2);

	DRM_DEBUG("i2c_detect: bus=%d present=%d\n", connector->bus, ret);
	return ret;
}

static void nv50_connector_destroy(struct drm_connector *drm_connector)
{
	struct nouveau_connector *connector = to_nouveau_connector(drm_connector);
	struct drm_device *dev = connector->base.dev;
	struct nv50_display *display = nv50_get_display(dev);

	DRM_DEBUG("\n");

	if (!display || !connector)
		return;

	if (connector->i2c_chan)
		nv50_i2c_channel_destroy(connector->i2c_chan);

	drm_sysfs_connector_remove(drm_connector);
	drm_connector_cleanup(drm_connector);
	kfree(drm_connector);
}

static void
nv50_connector_set_digital(struct nouveau_connector *connector, bool digital)
{
	struct drm_device *dev = connector->base.dev;

	if (connector->base.connector_type == DRM_MODE_CONNECTOR_DVII) {
		struct drm_property *prop =
			dev->mode_config.dvi_i_subconnector_property;
		uint64_t val;

		if (digital)
			val = DRM_MODE_SUBCONNECTOR_DVID;
		else
			val = DRM_MODE_SUBCONNECTOR_DVIA;

		drm_connector_property_set_value(&connector->base, prop, val);
	}

	connector->digital = digital;
}

void nv50_connector_detect_all(struct drm_device *dev)
{
	struct drm_connector *drm_connector = NULL;

	list_for_each_entry(drm_connector, &dev->mode_config.connector_list, head) {
		drm_connector->funcs->detect(drm_connector);
	}
}

static enum drm_connector_status
nv50_connector_detect(struct drm_connector *drm_connector)
{
	struct nouveau_connector *connector = to_nouveau_connector(drm_connector);
	struct nouveau_encoder *encoder = NULL;
	struct drm_encoder_helper_funcs *helper = NULL;
	bool hpd, i2c;

	if (drm_connector->connector_type == DRM_MODE_CONNECTOR_LVDS) {
		if (!connector->native_mode) {
			DRM_ERROR("No native mode for LVDS.\n");
			return connector_status_disconnected;
		}

		nv50_connector_set_digital(connector, true);
		return connector_status_connected;
	}

	encoder = connector->to_encoder(connector, false);
	if (encoder)
		helper = encoder->base.helper_private;

	if (helper && helper->detect(&encoder->base, &connector->base) ==
			connector_status_connected) {
		nv50_connector_set_digital(connector, false);
		return connector_status_connected;
	}

	/* It's not certain if we can trust the hotplug pins just yet,
	 * at least, we haven't found a reliable way of determining which
	 * pin is wired to which connector.  We'll do both hpd and single-byte
	 * i2c detect, and report if they differ for reference, and then
	 * trust i2c detect.
	 */
	hpd = nv50_connector_hpd_detect(connector);
	i2c = nv50_connector_i2c_detect(connector);
	if (hpd != i2c)
		DRM_INFO("i2c and hpd detect differ: %d vs %d\n", i2c, hpd);

	if (i2c) {
		nv50_connector_set_digital(connector, true);
		return connector_status_connected;
	}

	return connector_status_disconnected;
}

static int nv50_connector_set_property(struct drm_connector *drm_connector,
				       struct drm_property *property,
				       uint64_t value)
{
	struct drm_device *dev = drm_connector->dev;
	struct nouveau_connector *connector = to_nouveau_connector(drm_connector);
	int rval;

	/* DPMS */
	if (property == dev->mode_config.dpms_property) {
		if (value > 3)
			return -EINVAL;

		drm_helper_set_connector_dpms(drm_connector, value);
	}

	/* Scaling mode */
	if (property == dev->mode_config.scaling_mode_property) {
		struct nouveau_crtc *crtc = NULL;
		bool modeset = false;

		switch (value) {
		case DRM_MODE_SCALE_NON_GPU:
		case DRM_MODE_SCALE_FULLSCREEN:
		case DRM_MODE_SCALE_NO_SCALE:
		case DRM_MODE_SCALE_ASPECT:
			break;
		default:
			return -EINVAL;
		}

		/* LVDS always needs gpu scaling */
		if (connector->base.connector_type == DRM_MODE_CONNECTOR_LVDS &&
		    value == DRM_MODE_SCALE_NON_GPU)
			return -EINVAL;

		/* Changing between GPU and panel scaling requires a full
		 * modeset
		 */
		if ((connector->scaling_mode == DRM_MODE_SCALE_NON_GPU) ||
		    (value == DRM_MODE_SCALE_NON_GPU))
			modeset = true;
		connector->scaling_mode = value;

		if (drm_connector->encoder && drm_connector->encoder->crtc)
			crtc = to_nouveau_crtc(drm_connector->encoder->crtc);

		if (!crtc)
			return 0;

		if (modeset) {
			rval = drm_crtc_helper_set_mode(&crtc->base,
							&crtc->base.mode,
							crtc->base.x,
							crtc->base.y);
			if (rval)
				return rval;
		} else {
			rval = crtc->set_scale(crtc, value, true);
			if (rval)
				return rval;
		}

		return 0;
	}

	/* Dithering */
	if (property == dev->mode_config.dithering_mode_property) {
		struct nouveau_crtc *crtc = NULL;

		if (value == DRM_MODE_DITHERING_ON)
			connector->use_dithering = true;
		else
			connector->use_dithering = false;

		if (drm_connector->encoder && drm_connector->encoder->crtc)
			crtc = to_nouveau_crtc(drm_connector->encoder->crtc);

		if (!crtc)
			return 0;

		/* update hw state */
		crtc->use_dithering = connector->use_dithering;
		rval = crtc->set_dither(crtc, true);
		if (rval)
			return rval;

		return 0;
	}

	return -EINVAL;
}

static struct drm_display_mode *
nv50_connector_native_mode(struct nouveau_connector *connector)
{
	struct drm_device *dev = connector->base.dev;
	struct drm_display_mode *mode;

	if (!connector->digital)
		return NULL;

	list_for_each_entry(mode, &connector->base.probed_modes, head) {
		if (mode->type & DRM_MODE_TYPE_PREFERRED)
			return drm_mode_duplicate(dev, mode);
	}

	return NULL;
}

static int nv50_connector_get_modes(struct drm_connector *drm_connector)
{
	struct drm_device *dev = drm_connector->dev;
	struct nouveau_connector *connector = to_nouveau_connector(drm_connector);
	struct edid *edid = NULL;
	int ret = 0;

	/* If we're not LVDS, destroy the previous native mode, the attached
	 * monitor could have changed.
	 */
	if (drm_connector->connector_type != DRM_MODE_CONNECTOR_LVDS &&
	    connector->native_mode) {
		drm_mode_destroy(dev, connector->native_mode);
		connector->native_mode = NULL;
	}

	if (connector->i2c_chan)
		edid = (struct edid *)drm_do_probe_ddc_edid(&connector->i2c_chan->adapter);
	drm_mode_connector_update_edid_property(drm_connector, edid);

	if (edid) {
		ret = drm_add_edid_modes(drm_connector, edid);
		kfree(edid);
	}

	/* Find the native mode if this is a digital panel, if we didn't
	 * find any modes through DDC previously add the native mode to
	 * the list of modes.
	 */
	if (!connector->native_mode)
		connector->native_mode = nv50_connector_native_mode(connector);
	if (ret == 0 && connector->native_mode) {
		struct drm_display_mode *mode;

		mode = drm_mode_duplicate(dev, connector->native_mode);
		drm_mode_probed_add(drm_connector, mode);
		ret = 1;
	}

	return ret;
}

static int nv50_connector_mode_valid(struct drm_connector *drm_connector,
				     struct drm_display_mode *mode)
{
	struct nouveau_connector *connector = to_nouveau_connector(drm_connector);
	struct nouveau_encoder *encoder =
		connector->to_encoder(connector, connector->digital);
	unsigned min_clock, max_clock;

	min_clock = 25000;

	switch (encoder->base.encoder_type) {
	case DRM_MODE_ENCODER_LVDS:
		if (!connector->native_mode) {
			DRM_ERROR("AIIII no native mode\n");
			return MODE_PANEL;
		}

		if (mode->hdisplay > connector->native_mode->hdisplay ||
		    mode->vdisplay > connector->native_mode->vdisplay)
			return MODE_PANEL;
		/* fall-through */
	case DRM_MODE_ENCODER_TMDS:
		max_clock = 165000;
		break;
	default:
		max_clock = 400000;
		break;
	}

	if (mode->clock < min_clock)
		return MODE_CLOCK_LOW;

	if (mode->clock > max_clock)
		return MODE_CLOCK_HIGH;

	return MODE_OK;
}

static struct drm_encoder *
nv50_connector_best_encoder(struct drm_connector *drm_connector)
{
	struct nouveau_connector *connector = to_nouveau_connector(drm_connector);

	return &connector->to_encoder(connector, connector->digital)->base;
}

static const struct drm_connector_helper_funcs nv50_connector_helper_funcs = {
	.get_modes = nv50_connector_get_modes,
	.mode_valid = nv50_connector_mode_valid,
	.best_encoder = nv50_connector_best_encoder,
};

static const struct drm_connector_funcs nv50_connector_funcs = {
	.save = NULL,
	.restore = NULL,
	.detect = nv50_connector_detect,
	.destroy = nv50_connector_destroy,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.set_property = nv50_connector_set_property
};

int nv50_connector_create(struct drm_device *dev, int bus, int i2c_index, int type)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_connector *connector = NULL;
	int i;

	DRM_DEBUG("\n");

	connector = kzalloc(sizeof(*connector), GFP_KERNEL);
	if (!connector)
		return -ENOMEM;

	connector->bus = bus;

	switch (type) {
	case DRM_MODE_CONNECTOR_VGA:
		DRM_INFO("Detected a VGA connector\n");
		break;
	case DRM_MODE_CONNECTOR_DVID:
		DRM_INFO("Detected a DVI-D connector\n");
		break;
	case DRM_MODE_CONNECTOR_DVII:
		DRM_INFO("Detected a DVI-I connector\n");
		break;
	case DRM_MODE_CONNECTOR_LVDS:
		connector->native_mode = nv50_connector_lvds_native_mode(dev);
		DRM_INFO("Detected a LVDS connector\n");
		break;
	case DRM_MODE_CONNECTOR_SVIDEO:
		DRM_INFO("Detected a TV connector\n");
		break;
	default:
		DRM_ERROR("Unknown connector, this is not good.\n");
		break;
	}

	/* some reasonable defaults */
	switch (type) {
	case DRM_MODE_CONNECTOR_DVII:
	case DRM_MODE_CONNECTOR_DVID:
	case DRM_MODE_CONNECTOR_LVDS:
		connector->scaling_mode = DRM_MODE_SCALE_FULLSCREEN;
		break;
	default:
		connector->scaling_mode = DRM_MODE_SCALE_NON_GPU;
		break;
	}

	if (type == DRM_MODE_CONNECTOR_LVDS)
		connector->use_dithering = true;
	else
		connector->use_dithering = false;

	if (i2c_index < 0xf) {
		i2c_index = dev_priv->dcb_table.i2c_read[i2c_index];
		connector->i2c_chan = nv50_i2c_channel_create(dev, i2c_index);
	}

	/* set function pointers */
	connector->to_encoder = nv50_connector_to_encoder;

	/* It should be allowed sometimes, but let's be safe for the moment. */
	connector->base.interlace_allowed = false;
	connector->base.doublescan_allowed = false;

	drm_connector_init(dev, &connector->base, &nv50_connector_funcs, type);
	drm_connector_helper_add(&connector->base, &nv50_connector_helper_funcs);

	/* Init DVI-I specific properties */
	if (type == DRM_MODE_CONNECTOR_DVII) {
		drm_mode_create_dvi_i_properties(dev);
		drm_connector_attach_property(&connector->base, dev->mode_config.dvi_i_subconnector_property, 0);
		drm_connector_attach_property(&connector->base, dev->mode_config.dvi_i_select_subconnector_property, 0);
	}

	/* If supported in the future, it will have to use the scalers
	 * internally and not expose them.
	 */
	if (type != DRM_MODE_CONNECTOR_SVIDEO) {
		drm_connector_attach_property(&connector->base, dev->mode_config.scaling_mode_property, connector->scaling_mode);
	}

	drm_connector_attach_property(&connector->base, dev->mode_config.dithering_mode_property, connector->use_dithering ? DRM_MODE_DITHERING_ON : DRM_MODE_DITHERING_OFF);

	/* attach encoders, possibilities are analog + digital */
	for (i = 0; i < 2; i++) {
		struct nouveau_encoder *encoder = connector->to_encoder(connector, i);
		if (!encoder)
			continue;

		drm_mode_connector_attach_encoder(&connector->base, &encoder->base);
	}

	drm_sysfs_connector_add(&connector->base);

	return 0;
}
