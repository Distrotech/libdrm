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

#include "nv50_connector.h"
#include "nv50_kms_wrapper.h"

static struct nv50_output *nv50_connector_to_output(struct nv50_connector *connector, bool digital)
{
	struct nv50_display *display = nv50_get_display(connector->base.dev);
	struct nv50_output *output = NULL;
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

	list_for_each_entry(output, &display->outputs, item) {
		if (connector->bus != output->bus)
			continue;

		if (digital) {
			switch (output->base.encoder_type) {
			case DRM_MODE_ENCODER_TMDS:
			case DRM_MODE_ENCODER_LVDS:
				return output;
			default:
				break;
			}
		} else {
			switch (output->base.encoder_type) {
			case DRM_MODE_ENCODER_DAC:
			case DRM_MODE_ENCODER_TVDAC:
				return output;
			default:
				break;
			}
		}
	}

	return NULL;
}

static int nv50_connector_hpd_detect(struct nv50_connector *connector)
{
	struct drm_nouveau_private *dev_priv = connector->base.dev->dev_private;
	bool present = 0;
	uint32_t reg = 0;

	/* Assume connected for the moment. */
	if (connector->base.connector_type == DRM_MODE_CONNECTOR_LVDS) {
		NV50_DEBUG("LVDS is defaulting to connected for the moment.\n");
		return 1;
	}

	/* No i2c port, no idea what to do for hotplug. */
	if (connector->i2c_chan->index == 15) {
		DRM_ERROR("You have a non-LVDS SOR with no i2c port, please report\n");
		return -EINVAL;
	}

	if (connector->i2c_chan->index > 3) {
		DRM_ERROR("You have an unusual configuration, index is %d\n", connector->i2c_chan->index);
		DRM_ERROR("Please report.\n");
		return -EINVAL;
	}

	/* Check hotplug pins. */
	reg = NV_READ(NV50_PCONNECTOR_HOTPLUG_STATE);
	if (reg & (NV50_PCONNECTOR_HOTPLUG_STATE_PIN_CONNECTED_I2C0 << (4 * connector->i2c_chan->index)))
		present = 1;

	if (present)
		NV50_DEBUG("Hotplug detect returned positive for bus %d\n", connector->bus);
	else
		NV50_DEBUG("Hotplug detect returned negative for bus %d\n", connector->bus);

	return present;
}

static int nv50_connector_i2c_detect(struct nv50_connector *connector)
{
	/* kindly borrrowed from the intel driver, hope it works. */
	uint8_t out_buf[] = { 0x0, 0x0};
	uint8_t buf[2];
	int ret;
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
		return -EINVAL;

	ret = i2c_transfer(&connector->i2c_chan->adapter, msgs, 2);
	NV50_DEBUG("I2C detect returned %d\n", ret);

	if (ret == 2)
		return true;

	return false;
}

static void nv50_connector_destroy(struct drm_connector *drm_connector)
{
	struct nv50_connector *connector = to_nv50_connector(drm_connector);
	struct drm_device *dev = connector->base.dev;
	struct nv50_display *display = nv50_get_display(dev);

	NV50_DEBUG("\n");

	if (!display || !connector)
		return;

	list_del(&connector->item);

	if (connector->i2c_chan)
		nv50_i2c_channel_destroy(connector->i2c_chan);

	drm_sysfs_connector_remove(drm_connector);
	drm_connector_cleanup(drm_connector);
	kfree(drm_connector);
}

/* These 2 functions wrap the connector properties that deal with multiple encoders per connector. */
bool nv50_kms_connector_get_digital(struct drm_connector *drm_connector)
{
	struct drm_device *dev = drm_connector->dev;

	switch (drm_connector->connector_type) {
	case DRM_MODE_CONNECTOR_VGA:
	case DRM_MODE_CONNECTOR_SVIDEO:
		return false;
	case DRM_MODE_CONNECTOR_DVID:
	case DRM_MODE_CONNECTOR_LVDS:
		return true;
	default:
		break;
	}

	if (drm_connector->connector_type == DRM_MODE_CONNECTOR_DVII) {
		int rval;
		uint64_t prop_val;

		rval = drm_connector_property_get_value(drm_connector, dev->mode_config.dvi_i_select_subconnector_property, &prop_val);
		if (rval) {
			DRM_ERROR("Unable to find select subconnector property, defaulting to DVI-D\n");
			return true;
		}

		/* Is a subconnector explicitly selected? */
		switch (prop_val) {
		case DRM_MODE_SUBCONNECTOR_DVID:
			return true;
		case DRM_MODE_SUBCONNECTOR_DVIA:
			return false;
		default:
			break;
		}

		rval = drm_connector_property_get_value(drm_connector, dev->mode_config.dvi_i_subconnector_property, &prop_val);
		if (rval) {
			DRM_ERROR("Unable to find subconnector property, defaulting to DVI-D\n");
			return true;
		}

		/* Do we know what subconnector we currently have connected? */
		switch (prop_val) {
		case DRM_MODE_SUBCONNECTOR_DVID:
			return true;
		case DRM_MODE_SUBCONNECTOR_DVIA:
			return false;
		default:
			DRM_ERROR("Unknown subconnector value, defaulting to DVI-D\n");
			return true;
		}
	}

	DRM_ERROR("Unknown connector type, defaulting to analog\n");
	return false;
}

static void nv50_kms_connector_set_digital(struct drm_connector *drm_connector, int digital, bool force)
{
	struct drm_device *dev = drm_connector->dev;

	if (drm_connector->connector_type == DRM_MODE_CONNECTOR_DVII) {
		uint64_t cur_value, new_value;

		int rval = drm_connector_property_get_value(drm_connector, dev->mode_config.dvi_i_subconnector_property, &cur_value);
		if (rval) {
			DRM_ERROR("Unable to find subconnector property\n");
			return;
		}

		/* Only set when unknown or when forced to do so. */
		if (cur_value != DRM_MODE_SUBCONNECTOR_Unknown && !force)
			return;

		if (digital == 1)
			new_value = DRM_MODE_SUBCONNECTOR_DVID;
		else if (digital == 0)
			new_value = DRM_MODE_SUBCONNECTOR_DVIA;
		else
			new_value = DRM_MODE_SUBCONNECTOR_Unknown;
		drm_connector_property_set_value(drm_connector, dev->mode_config.dvi_i_subconnector_property, new_value);
	}
}

void nv50_kms_connector_detect_all(struct drm_device *dev)
{
	struct drm_connector *drm_connector = NULL;

	list_for_each_entry(drm_connector, &dev->mode_config.connector_list, head) {
		drm_connector->funcs->detect(drm_connector);
	}
}

static enum drm_connector_status nv50_kms_connector_detect(struct drm_connector *drm_connector)
{
	struct drm_device *dev = drm_connector->dev;
	struct nv50_connector *connector = to_nv50_connector(drm_connector);
	struct nv50_output *output = NULL;
	int hpd_detect = 0, load_detect = 0, i2c_detect = 0;
	int old_status = drm_connector->status;

	/* hotplug detect */
	hpd_detect = connector->hpd_detect(connector);

	/* load detect */
	output = connector->to_output(connector, false); /* analog */
	if (output && output->detect)
		load_detect = output->detect(output);

	if (hpd_detect < 0 || load_detect < 0) /* did an error occur? */
		i2c_detect = connector->i2c_detect(connector);

	if (load_detect == 1) {
		nv50_kms_connector_set_digital(drm_connector, 0, true); /* analog, forced */
	} else if (hpd_detect == 1 && load_detect == 0) {
		nv50_kms_connector_set_digital(drm_connector, 1, true); /* digital, forced */
	} else {
		nv50_kms_connector_set_digital(drm_connector, -1, true); /* unknown, forced */
	}

	if (hpd_detect == 1 || load_detect == 1 || i2c_detect == 1)
		drm_connector->status = connector_status_connected;
	else
		drm_connector->status = connector_status_disconnected;

	/* update our modes whenever there is reason to */
	if (old_status != drm_connector->status) {
		drm_connector->funcs->fill_modes(drm_connector, 0, 0);

		/* notify fb of changes */
		dev->mode_config.funcs->fb_changed(dev);

		/* sent a hotplug event when appropriate. */
		drm_sysfs_hotplug_event(dev);
	}

	return drm_connector->status;
}

/*
 * Detailed mode info for a standard 640x480@60Hz monitor
 */
static struct drm_display_mode std_mode[] = {
	/*{ DRM_MODE("640x480", DRM_MODE_TYPE_DEFAULT, 25200, 640, 656,
		 752, 800, 0, 480, 490, 492, 525, 0,
		 DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC) },*/ /* 640x480@60Hz */
	{ DRM_MODE("1280x1024", DRM_MODE_TYPE_DEFAULT, 135000, 1280, 1296,
		   1440, 1688, 0, 1024, 1025, 1028, 1066, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) }, /* 1280x1024@75Hz */
};

static void nv50_kms_connector_fill_modes(struct drm_connector *drm_connector, uint32_t maxX, uint32_t maxY)
{
	struct nv50_connector *connector = to_nv50_connector(drm_connector);
	struct drm_device *dev = drm_connector->dev;
	int rval = 0;
	bool connected = false;
	struct drm_display_mode *mode, *t;
	struct edid *edid = NULL;

	NV50_DEBUG("%s\n", drm_get_connector_name(drm_connector));
	/* set all modes to the unverified state */
	list_for_each_entry_safe(mode, t, &drm_connector->modes, head)
		mode->status = MODE_UNVERIFIED;

	if (nv50_kms_connector_detect(drm_connector) == connector_status_connected)
		connected = true;

	if (connected)
		NV50_DEBUG("%s is connected\n", drm_get_connector_name(drm_connector));
	else
		NV50_DEBUG("%s is disconnected\n", drm_get_connector_name(drm_connector));

	/* Not all connnectors have an i2c channel. */
	if (connected && connector->i2c_chan)
		edid = (struct edid *) drm_do_probe_ddc_edid(&connector->i2c_chan->adapter);

	/* This will remove edid if needed. */
	drm_mode_connector_update_edid_property(drm_connector, edid);

	if (edid) {
		rval = drm_add_edid_modes(drm_connector, edid);

		/* Only update when relevant and when detect couldn't determine type. */
		nv50_kms_connector_set_digital(drm_connector, edid->digital ? 1 : 0, false);

		kfree(edid);
	}

	if (rval) /* number of modes  > 1 */
		drm_mode_connector_list_update(drm_connector);

	if (maxX && maxY)
		drm_mode_validate_size(dev, &drm_connector->modes, maxX, maxY, 0);

	list_for_each_entry_safe(mode, t, &drm_connector->modes, head) {
		if (mode->status == MODE_OK) {
			struct nv50_output *output = connector->to_output(connector, nv50_kms_connector_get_digital(drm_connector));

			mode->status = output->validate_mode(output, mode);
			/* find native mode, TODO: also check if we actually found one */
			if (mode->status == MODE_OK) {
				if (mode->type & DRM_MODE_TYPE_PREFERRED)
					*output->native_mode = *mode;
			}
		}
	}

	/* revalidate now that we have native mode */
	list_for_each_entry_safe(mode, t, &drm_connector->modes, head) {
		if (mode->status == MODE_OK) {
			struct nv50_output *output = connector->to_output(connector, nv50_kms_connector_get_digital(drm_connector));

			mode->status = output->validate_mode(output, mode);
		}
	}

	drm_mode_prune_invalid(dev, &drm_connector->modes, true);

	/* pruning is done, so bail out. */
	if (!connected)
		return;

	if (list_empty(&drm_connector->modes)) {
		struct drm_display_mode *stdmode;
		struct nv50_output *output;

		NV50_DEBUG("No valid modes on %s\n", drm_get_connector_name(drm_connector));

		/* Making up native modes for LVDS is a bad idea. */
		if (drm_connector->connector_type == DRM_MODE_CONNECTOR_LVDS)
			return;

		/* Should we do this here ???
		 * When no valid EDID modes are available we end up
		 * here and bailed in the past, now we add a standard
		 * 640x480@60Hz mode and carry on.
		 */
		stdmode = drm_mode_duplicate(dev, &std_mode[0]);
		drm_mode_probed_add(drm_connector, stdmode);
		drm_mode_list_concat(&drm_connector->probed_modes,
				     &drm_connector->modes);

		/* also add it as native mode */
		output = connector->to_output(connector, nv50_kms_connector_get_digital(drm_connector));

		if (mode)
			*output->native_mode = *mode;

		DRM_DEBUG("Adding standard 640x480 @ 60Hz to %s\n",
			  drm_get_connector_name(drm_connector));
	}

	drm_mode_sort(&drm_connector->modes);

	NV50_DEBUG("Probed modes for %s\n", drm_get_connector_name(drm_connector));

	list_for_each_entry_safe(mode, t, &drm_connector->modes, head) {
		mode->vrefresh = drm_mode_vrefresh(mode);

		/* is this needed, as it's unused by the driver? */
		drm_mode_set_crtcinfo(mode, CRTC_INTERLACE_HALVE_V);
		drm_mode_debug_printmodeline(mode);
	}
}

static int nv50_kms_connector_set_property(struct drm_connector *drm_connector,
					struct drm_property *property,
					uint64_t value)
{
	struct drm_device *dev = drm_connector->dev;
	struct nv50_connector *connector = to_nv50_connector(drm_connector);
	int rval = 0;
	bool delay_change = false;

	/* DPMS */
	if (property == dev->mode_config.dpms_property && drm_connector->encoder) {
		struct nv50_output *output = to_nv50_output(drm_connector->encoder);

		rval = output->set_power_mode(output, (int) value);

		return rval;
	}

	/* Scaling mode */
	if (property == dev->mode_config.scaling_mode_property) {
		struct nv50_crtc *crtc = NULL;
		struct nv50_display *display = nv50_get_display(dev);

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

		connector->requested_scaling_mode = value;

		if (drm_connector->encoder && drm_connector->encoder->crtc)
			crtc = to_nv50_crtc(drm_connector->encoder->crtc);

		if (!crtc)
			return 0;

		crtc->requested_scaling_mode = connector->requested_scaling_mode;

		/* going from and to a gpu scaled regime requires a
		 * modesetting, so wait until next modeset
		 */
		if (crtc->scaling_mode == DRM_MODE_SCALE_NON_GPU ||
		    value == DRM_MODE_SCALE_NON_GPU) {
			DRM_INFO("Moving from or to a non-gpu scaled mode, "
				 "this will be processed upon next modeset.");
			delay_change = true;
		}

		if (delay_change)
			return 0;

		rval = crtc->set_scale(crtc);
		if (rval)
			return rval;

		/* process command buffer */
		display->update(display);

		return 0;
	}

	/* Dithering */
	if (property == dev->mode_config.dithering_mode_property) {
		struct nv50_crtc *crtc = NULL;
		struct nv50_display *display = nv50_get_display(dev);

		if (value == DRM_MODE_DITHERING_ON)
			connector->use_dithering = true;
		else
			connector->use_dithering = false;

		if (drm_connector->encoder && drm_connector->encoder->crtc)
			crtc = to_nv50_crtc(drm_connector->encoder->crtc);

		if (!crtc)
			return 0;

		/* update hw state */
		crtc->use_dithering = connector->use_dithering;
		rval = crtc->set_dither(crtc);
		if (rval)
			return rval;

		/* process command buffer */
		display->update(display);

		return 0;
	}

	return -EINVAL;
}

static const struct drm_connector_funcs nv50_connector_funcs = {
	.save = NULL,
	.restore = NULL,
	.detect = nv50_kms_connector_detect,
	.destroy = nv50_connector_destroy,
	.fill_modes = nv50_kms_connector_fill_modes,
	.set_property = nv50_kms_connector_set_property
};

static int nv50_kms_get_scaling_mode(struct drm_connector *drm_connector)
{
	struct nv50_connector *connector = NULL;

	if (!drm_connector) {
		DRM_ERROR("drm_connector is NULL\n");
		return 0;
	}

	connector = to_nv50_connector(drm_connector);
	return connector->requested_scaling_mode;
}

int nv50_connector_create(struct drm_device *dev, int bus, int i2c_index, int type)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nv50_connector *connector = NULL;
	struct nv50_display *display = NULL;
	int i;

	NV50_DEBUG("\n");

	display = nv50_get_display(dev);
	if (!display || type == DRM_MODE_CONNECTOR_Unknown)
		return -EINVAL;

	connector = kzalloc(sizeof(*connector), GFP_KERNEL);
	if (!connector)
		return -ENOMEM;

	list_add_tail(&connector->item, &display->connectors);

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
		connector->requested_scaling_mode = DRM_MODE_SCALE_FULLSCREEN;
		break;
	default:
		connector->requested_scaling_mode = DRM_MODE_SCALE_NON_GPU;
		break;
	}

	connector->use_dithering = false;

	if (i2c_index < 0xf) {
		i2c_index = dev_priv->dcb_table.i2c_read[i2c_index];
		connector->i2c_chan = nv50_i2c_channel_create(dev, i2c_index);
	}

	/* set function pointers */
	connector->hpd_detect = nv50_connector_hpd_detect;
	connector->i2c_detect = nv50_connector_i2c_detect;
	connector->to_output = nv50_connector_to_output;

	/* It should be allowed sometimes, but let's be safe for the moment. */
	connector->base.interlace_allowed = false;
	connector->base.doublescan_allowed = false;

	drm_connector_init(dev, &connector->base, &nv50_connector_funcs, type);

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
		drm_connector_attach_property(&connector->base, dev->mode_config.scaling_mode_property, nv50_kms_get_scaling_mode(&connector->base));
	}

	drm_connector_attach_property(&connector->base, dev->mode_config.dithering_mode_property, connector->use_dithering ? DRM_MODE_DITHERING_ON : DRM_MODE_DITHERING_OFF);

	/* attach encoders, possibilities are analog + digital */
	for (i = 0; i < 2; i++) {
		struct nv50_output *output = connector->to_output(connector, i);
		if (!output)
			continue;

		drm_mode_connector_attach_encoder(&connector->base, &output->base);
	}

	drm_sysfs_connector_add(&connector->base);
	return 0;
}
