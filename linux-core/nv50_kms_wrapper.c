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
 * FB functions.
 */

static void nv50_kms_framebuffer_destroy(struct drm_framebuffer *drm_fb)
{
	struct nv50_framebuffer *fb = to_nv50_framebuffer(drm_fb);

	drm_gem_object_unreference(fb->gem);
	drm_framebuffer_cleanup(&fb->base);
	kfree(fb);
}

static int
nv50_kms_framebuffer_create_handle(struct drm_framebuffer *drm_fb,
				   struct drm_file *file_priv,
				   unsigned int *handle)
{
	struct nv50_framebuffer *fb = to_nv50_framebuffer(drm_fb);

	return drm_gem_handle_create(file_priv, fb->gem, handle);
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
	struct nv50_display *display = NULL;
	int rval = 0;

	/* bios is needed for tables. */
	rval = nouveau_parse_bios(dev);
	if (rval != 0)
		return rval;

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

	/* init the internal core, must be done first. */
	rval = nv50_display_create(dev);
	if (rval != 0)
		return rval;

	display = nv50_get_display(dev);
	if (!display)
		return -EINVAL;

	/* pre-init now */
	rval = display->pre_init(display);
	if (rval != 0)
		return rval;

	/* init now, this'll kill the textmode */
	rval = display->init(display);
	if (rval != 0)
		return rval;

	/* process cmdbuffer */
	display->update(display);
	return 0;
}

int nv50_kms_destroy(struct drm_device *dev)
{
	drm_mode_config_cleanup(dev);

	return 0;
}

