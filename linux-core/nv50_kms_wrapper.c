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

void nv50_kms_mirror_routing(struct drm_device *dev)
{
	struct nv50_display *display = nv50_get_display(dev);
	struct nv50_crtc *crtc = NULL;
	struct nv50_output *output = NULL;
	struct nv50_connector *connector = NULL;
	struct drm_connector *drm_connector = NULL;

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

