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

#include "nv50_kms_wrapper.h"
#include "drm_crtc_helper.h" /* be careful what you use from this */

/* This file serves as the interface between the common kernel modesetting code and the device dependent implementation. */

/*
 * Get private functions.
 */

struct nv50_kms_priv *nv50_get_kms_priv(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	return dev_priv->kms_priv;
}

/*
 * Allocation functions.
 */

static void *nv50_kms_alloc_crtc(struct drm_device *dev)
{
	struct nv50_kms_priv *kms_priv = nv50_get_kms_priv(dev);
	struct nv50_kms_crtc *crtc = kzalloc(sizeof(struct nv50_kms_crtc), GFP_KERNEL);

	if (!crtc)
		return NULL;

	list_add_tail(&crtc->item, &kms_priv->crtcs);

	return &(crtc->priv);
}

static void *nv50_kms_alloc_output(struct drm_device *dev)
{
	struct nv50_kms_priv *kms_priv = nv50_get_kms_priv(dev);
	struct nv50_kms_encoder *encoder = kzalloc(sizeof(struct nv50_kms_encoder), GFP_KERNEL);

	if (!encoder)
		return NULL;

	list_add_tail(&encoder->item, &kms_priv->encoders);

	return &(encoder->priv);
}

static void nv50_kms_free_crtc(void *crtc)
{
	struct nv50_kms_crtc *kms_crtc = from_nv50_crtc(crtc);

	list_del(&kms_crtc->item);

	kfree(kms_crtc);
}

static void nv50_kms_free_output(void *output)
{
	struct nv50_kms_encoder *kms_encoder = from_nv50_output(output);

	list_del(&kms_encoder->item);

	kfree(kms_encoder);
}

/*
 * Mode conversion functions.
 */

struct nouveau_hw_mode *nv50_kms_to_hw_mode(struct drm_display_mode *mode)
{
	struct nouveau_hw_mode *hw_mode = kzalloc(sizeof(struct nouveau_hw_mode), GFP_KERNEL);
	if (!hw_mode)
		return NULL;

	/* create hw values. */
	hw_mode->clock = mode->clock;
	hw_mode->flags = hw_mode->flags;

	hw_mode->hdisplay = mode->hdisplay;
	hw_mode->hsync_start = mode->hsync_start;
	hw_mode->hsync_end = mode->hsync_end;
	hw_mode->htotal = mode->htotal;

	hw_mode->hblank_start = mode->hdisplay + 1;
	hw_mode->hblank_end = mode->htotal;

	hw_mode->vdisplay = mode->vdisplay;
	hw_mode->vsync_start = mode->vsync_start;
	hw_mode->vsync_end = mode->vsync_end;
	hw_mode->vtotal = mode->vtotal;

	hw_mode->vblank_start = mode->vdisplay + 1;
	hw_mode->vblank_end = mode->vtotal;

	return hw_mode;
}

/*
 * State mirroring functions.
 */

static void nv50_kms_mirror_routing(struct drm_device *dev)
{
	struct nv50_display *display = nv50_get_display(dev);
	struct nv50_crtc *crtc = NULL;
	struct nv50_output *output = NULL;
	struct nv50_connector *connector = NULL;
	struct drm_connector *drm_connector = NULL;
	struct drm_crtc *drm_crtc = NULL;

	/* Wipe all previous connections. */
	list_for_each_entry(connector, &display->connectors, item) {
		connector->output = NULL;
	}

	list_for_each_entry(output, &display->outputs, item) {
		output->crtc = NULL;
	}

	list_for_each_entry(drm_connector, &dev->mode_config.connector_list, head) {
		if (drm_connector->encoder) {
			output = to_nv50_output(drm_connector->encoder);
			connector = to_nv50_connector(drm_connector);

			/* hook up output to connector. */
			connector->output = output;

			if (drm_connector->encoder->crtc) {
				crtc = to_nv50_crtc(drm_connector->encoder->crtc);

				/* hook up output to crtc. */
				output->crtc = crtc;
			}
		}
	}

	/* mirror crtc active state */
	list_for_each_entry(drm_crtc, &dev->mode_config.crtc_list, head) {
		crtc = to_nv50_crtc(drm_crtc);

		crtc->enabled = drm_crtc->enabled;
	}
}

/*
 * FB functions.
 */

static void nv50_kms_framebuffer_destroy(struct drm_framebuffer *fb)
{
	struct nv50_framebuffer *nv50_fb = nv50_framebuffer(fb);

	drm_gem_object_unreference(nv50_fb->gem);
	drm_framebuffer_cleanup(&nv50_fb->base);
	kfree(nv50_fb);
}

static int
nv50_kms_framebuffer_create_handle(struct drm_framebuffer *fb,
				   struct drm_file *file_priv,
				   unsigned int *handle)
{
	struct nv50_framebuffer *nv50_fb = nv50_framebuffer(fb);

	return drm_gem_handle_create(file_priv, nv50_fb->gem, handle);
}

static const struct drm_framebuffer_funcs nv50_kms_fb_funcs = {
	.destroy = nv50_kms_framebuffer_destroy,
	.create_handle = nv50_kms_framebuffer_create_handle,
};

/*
 * Mode config functions.
 */

struct drm_framebuffer *
nv50_framebuffer_create(struct drm_device *dev, struct drm_gem_object *gem,
			struct drm_mode_fb_cmd *mode_cmd)
{
	struct nv50_framebuffer *nv50_fb;

	nv50_fb = kzalloc(sizeof(struct nv50_framebuffer), GFP_KERNEL);
	if (!nv50_fb)
		return NULL;

	drm_framebuffer_init(dev, &nv50_fb->base, &nv50_kms_fb_funcs);
	drm_helper_mode_fill_fb_struct(&nv50_fb->base, mode_cmd);

	nv50_fb->gem = gem;
	return &nv50_fb->base;
}

static struct drm_framebuffer *
nv50_kms_framebuffer_create(struct drm_device *dev, struct drm_file *file_priv,
			    struct drm_mode_fb_cmd *mode_cmd)
{
	struct drm_gem_object *gem;

	gem = drm_gem_object_lookup(dev, file_priv, mode_cmd->handle);
	return nv50_framebuffer_create(dev, gem, mode_cmd);
}

static int nv50_kms_fb_changed(struct drm_device *dev)
{
	return 0; /* not needed until nouveaufb? */
}

static const struct drm_mode_config_funcs nv50_kms_mode_funcs = {
	.resize_fb = NULL,
	.fb_create = nv50_kms_framebuffer_create,
	.fb_changed = nv50_kms_fb_changed,
};

/*
 * CRTC functions.
 */

static int nv50_kms_crtc_cursor_set(struct drm_crtc *drm_crtc, 
				    struct drm_file *file_priv,
				    uint32_t buffer_handle,
				    uint32_t width, uint32_t height)
{
	struct drm_device *dev = drm_crtc->dev;
	struct nv50_crtc *crtc = to_nv50_crtc(drm_crtc);
	struct nv50_display *display = nv50_get_display(crtc->dev);
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

static int nv50_kms_crtc_cursor_move(struct drm_crtc *drm_crtc, int x, int y)
{
	struct nv50_crtc *crtc = to_nv50_crtc(drm_crtc);

	return crtc->cursor->set_pos(crtc, x, y);
}

void nv50_kms_crtc_gamma_set(struct drm_crtc *drm_crtc, u16 *r, u16 *g, u16 *b,
		uint32_t size)
{
	struct nv50_crtc *crtc = to_nv50_crtc(drm_crtc);

	if (size != 256)
		return;

	crtc->lut->set(crtc, (uint16_t *)r, (uint16_t *)g, (uint16_t *)b);
}

int nv50_kms_crtc_set_config(struct drm_mode_set *set)
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
	struct nouveau_hw_mode *hw_mode = NULL;
	struct nv50_fb_info fb_info;

	bool blank = false;
	bool switch_fb = false;
	bool modeset = false;

	NV50_DEBUG("\n");

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
		hw_mode = nv50_kms_to_hw_mode(set->mode);

		rval = crtc->validate_mode(crtc, hw_mode);

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

			output = connector->to_output(connector, nv50_kms_connector_get_digital(drm_connector));
			if (!output) {
				DRM_ERROR("No output\n");
				goto out;
			}

			rval = output->validate_mode(output, hw_mode);
			if (rval != MODE_OK) {
				DRM_ERROR("Mode not ok\n");
				goto out;
			}

			/* verify if any "sneaky" changes happened */
			if (output != connector->output)
				modeset = true;

			if (output->crtc != crtc)
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

			output = connector->to_output(connector, nv50_kms_connector_get_digital(drm_connector));
			if (!output) {
				DRM_ERROR("No output\n");
				goto out;
			}

			/* find the encoder public structure that matches out output structure. */
			drm_encoder = to_nv50_kms_encoder(output);

			if (!drm_encoder) {
				DRM_ERROR("No encoder\n");
				goto out;
			}

			drm_encoder->crtc = set->crtc;
			set->crtc->enabled = true;
			drm_connector->encoder = drm_encoder;
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

	/* mirror everything to the private structs */
	nv50_kms_mirror_routing(dev);

	/**
	 * Bind framebuffer.
	 */

	if (switch_fb) {
		crtc = to_nv50_crtc(set->crtc);

		/* set framebuffer */
		set->crtc->fb = set->fb;

		/* set private framebuffer */
		crtc = to_nv50_crtc(set->crtc);
		fb_info.gem = nv50_framebuffer(set->fb)->gem;
		fb_info.width = set->fb->width;
		fb_info.height = set->fb->height;
		fb_info.depth = set->fb->depth;
		fb_info.bpp = set->fb->bits_per_pixel;
		fb_info.pitch = set->fb->pitch;
		fb_info.x = set->x;
		fb_info.y = set->y;

		rval = crtc->fb->bind(crtc, &fb_info);
		if (rval != 0) {
			DRM_ERROR("fb_bind failed\n");
			goto out;
		}
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
		list_for_each_entry(output, &display->outputs, item) {
			if (output->crtc) {
				crtc_mask |= 1 << output->crtc->index;
			} else {
				rval = output->execute_mode(output, true);
				if (rval != 0) {
					DRM_ERROR("detaching output failed\n");
					goto out;
				}
			}
		}

		/* blank any unused crtcs */
		list_for_each_entry(crtc, &display->crtcs, item) {
			if (!(crtc_mask & (1 << crtc->index)))
				crtc->blank(crtc, true);
		}

		crtc = to_nv50_crtc(set->crtc);

		rval = crtc->set_mode(crtc, hw_mode);
		if (rval != 0) {
			DRM_ERROR("crtc mode set failed\n");
			goto out;
		}

		/* find native mode. */
		list_for_each_entry(output, &display->outputs, item) {
			if (output->crtc != crtc)
				continue;

			*crtc->native_mode = *output->native_mode;
			list_for_each_entry(connector, &display->connectors, item) {
				if (connector->output != output)
					continue;

				crtc->requested_scaling_mode = connector->requested_scaling_mode;
				crtc->use_dithering = connector->use_dithering;
				break;
			}

			if (crtc->requested_scaling_mode == SCALE_NON_GPU)
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

		list_for_each_entry(output, &display->outputs, item) {
			if (output->crtc != crtc)
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
		list_for_each_entry(output, &display->outputs, item) {
			if (output->crtc != crtc)
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

	kfree(hw_mode);

	return 0;

out:
	kfree(hw_mode);

	if (rval != 0)
		return rval;
	else
		return -EINVAL;
}

static void nv50_kms_crtc_destroy(struct drm_crtc *drm_crtc)
{
	struct nv50_crtc *crtc = to_nv50_crtc(drm_crtc);

	drm_crtc_cleanup(drm_crtc);

	/* this will even destroy the public structure. */
	crtc->destroy(crtc);
}

static const struct drm_crtc_funcs nv50_kms_crtc_funcs = {
	.save = NULL,
	.restore = NULL,
	.cursor_set = nv50_kms_crtc_cursor_set,
	.cursor_move = nv50_kms_crtc_cursor_move,
	.gamma_set = nv50_kms_crtc_gamma_set,
	.set_config = nv50_kms_crtc_set_config,
	.destroy = nv50_kms_crtc_destroy,
};

static int nv50_kms_crtcs_init(struct drm_device *dev)
{
	struct nv50_display *display = nv50_get_display(dev);
	struct nv50_crtc *crtc = NULL;

	/*
	 * This may look a bit confusing, but:
	 * The internal structure is already allocated and so is the public one.
	 * Just a matter of getting to the memory and register it.
	 */
	list_for_each_entry(crtc, &display->crtcs, item) {
		struct drm_crtc *drm_crtc = to_nv50_kms_crtc(crtc);

		drm_crtc_init(dev, drm_crtc, &nv50_kms_crtc_funcs);

		/* init lut storage */
		drm_mode_crtc_set_gamma_size(drm_crtc, 256);
	}

	return 0;
}

/*
 * Encoder functions
 */

static void nv50_kms_encoder_destroy(struct drm_encoder *drm_encoder)
{
	struct nv50_output *output = to_nv50_output(drm_encoder);

	drm_encoder_cleanup(drm_encoder);

	/* this will even destroy the public structure. */
	output->destroy(output);
}

static const struct drm_encoder_funcs nv50_kms_encoder_funcs = {
	.destroy = nv50_kms_encoder_destroy,
};

static int nv50_kms_encoders_init(struct drm_device *dev)
{
	struct nv50_display *display = nv50_get_display(dev);
	struct nv50_output *output = NULL;

	list_for_each_entry(output, &display->outputs, item) {
		struct drm_encoder *drm_encoder = to_nv50_kms_encoder(output);
		uint32_t type = DRM_MODE_ENCODER_NONE;

		switch (output->type) {
			case OUTPUT_DAC:
				type = DRM_MODE_ENCODER_DAC;
				break;
			case OUTPUT_TMDS:
				type = DRM_MODE_ENCODER_TMDS;
				break;
			case OUTPUT_LVDS:
				type = DRM_MODE_ENCODER_LVDS;
				break;
			case OUTPUT_TV:
				type = DRM_MODE_ENCODER_TVDAC;
				break;
			default:
				type = DRM_MODE_ENCODER_NONE;
				break;
		}

		if (type == DRM_MODE_ENCODER_NONE) {
			DRM_ERROR("DRM_MODE_ENCODER_NONE encountered\n");
			continue;
		}

		drm_encoder_init(dev, drm_encoder, &nv50_kms_encoder_funcs, type);

		/* I've never seen possible crtc's restricted. */
		drm_encoder->possible_crtcs = 3;
		drm_encoder->possible_clones = 0;
	}

	return 0;
}

/*
 * Main functions
 */

int nv50_kms_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nv50_kms_priv *kms_priv = kzalloc(sizeof(struct nv50_kms_priv), GFP_KERNEL);
	struct nv50_display *display = NULL;
	int rval = 0;

	if (!kms_priv)
		return -ENOMEM;

	dev_priv->kms_priv = kms_priv;

	/* function pointers */
	/* an allocation interface that deals with the outside world, without polluting the core. */
	dev_priv->alloc_crtc = nv50_kms_alloc_crtc;
	dev_priv->alloc_output = nv50_kms_alloc_output;

	dev_priv->free_crtc = nv50_kms_free_crtc;
	dev_priv->free_output = nv50_kms_free_output;

	/* bios is needed for tables. */
	rval = nouveau_parse_bios(dev);
	if (rval != 0)
		goto out;

	/* init basic kernel modesetting */
	drm_mode_config_init(dev);

	/* Initialise some optional connector properties. */
	drm_mode_create_scaling_mode_property(dev);
	drm_mode_create_dithering_property(dev);

	dev->mode_config.min_width = 0;
	dev->mode_config.min_height = 0;

	dev->mode_config.funcs = (void *)&nv50_kms_mode_funcs;

	dev->mode_config.max_width = 8192;
	dev->mode_config.max_height = 8192;

	dev->mode_config.fb_base = dev_priv->fb_phys;

	/* init kms lists */
	INIT_LIST_HEAD(&kms_priv->crtcs);
	INIT_LIST_HEAD(&kms_priv->encoders);

	/* init the internal core, must be done first. */
	rval = nv50_display_create(dev);
	if (rval != 0)
		goto out;

	display = nv50_get_display(dev);
	if (!display) {
		rval = -EINVAL;
		goto out;
	}

	/* pre-init now */
	rval = display->pre_init(display);
	if (rval != 0)
		goto out;

	/* init external layer */
	rval = nv50_kms_crtcs_init(dev);
	if (rval != 0)
		goto out;

	rval = nv50_kms_encoders_init(dev);
	if (rval != 0)
		goto out;

	/* init now, this'll kill the textmode */
	rval = display->init(display);
	if (rval != 0)
		goto out;

	/* process cmdbuffer */
	display->update(display);

	return 0;

out:
	kfree(kms_priv);
	dev_priv->kms_priv = NULL;

	return rval;
}

int nv50_kms_destroy(struct drm_device *dev)
{
	drm_mode_config_cleanup(dev);

	return 0;
}

