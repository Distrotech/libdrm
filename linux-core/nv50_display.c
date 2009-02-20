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

#include "nv50_display.h"
#include "nv50_crtc.h"
#include "nv50_output.h"
#include "nv50_connector.h"
#include "nv50_fb.h"
#include "drm_crtc_helper.h"

static int nv50_display_pre_init(struct nv50_display *display)
{
	struct drm_device *dev = display->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int i;
	uint32_t ram_amount;

	DRM_DEBUG("\n");

	nv_wr32(0x00610184, nv_rd32(0x00614004));
	/*
	 * I think the 0x006101XX range is some kind of main control area that enables things.
	 */
	/* CRTC? */
	nv_wr32(0x00610190 + 0 * 0x10, nv_rd32(0x00616100 + 0 * 0x800));
	nv_wr32(0x00610190 + 1 * 0x10, nv_rd32(0x00616100 + 1 * 0x800));
	nv_wr32(0x00610194 + 0 * 0x10, nv_rd32(0x00616104 + 0 * 0x800));
	nv_wr32(0x00610194 + 1 * 0x10, nv_rd32(0x00616104 + 1 * 0x800));
	nv_wr32(0x00610198 + 0 * 0x10, nv_rd32(0x00616108 + 0 * 0x800));
	nv_wr32(0x00610198 + 1 * 0x10, nv_rd32(0x00616108 + 1 * 0x800));
	nv_wr32(0x0061019c + 0 * 0x10, nv_rd32(0x0061610c + 0 * 0x800));
	nv_wr32(0x0061019c + 1 * 0x10, nv_rd32(0x0061610c + 1 * 0x800));
	/* DAC */
	nv_wr32(0x006101d0 + 0 * 0x4, nv_rd32(0x0061a000 + 0 * 0x800));
	nv_wr32(0x006101d0 + 1 * 0x4, nv_rd32(0x0061a000 + 1 * 0x800));
	nv_wr32(0x006101d0 + 2 * 0x4, nv_rd32(0x0061a000 + 2 * 0x800));
	/* SOR */
	nv_wr32(0x006101e0 + 0 * 0x4, nv_rd32(0x0061c000 + 0 * 0x800));
	nv_wr32(0x006101e0 + 1 * 0x4, nv_rd32(0x0061c000 + 1 * 0x800));
	/* Something not yet in use, tv-out maybe. */
	nv_wr32(0x006101f0 + 0 * 0x4, nv_rd32(0x0061e000 + 0 * 0x800));
	nv_wr32(0x006101f0 + 1 * 0x4, nv_rd32(0x0061e000 + 1 * 0x800));
	nv_wr32(0x006101f0 + 2 * 0x4, nv_rd32(0x0061e000 + 2 * 0x800));

	for (i = 0; i < 3; i++) {
		nv_wr32(NV50_PDISPLAY_DAC_REGS_DPMS_CTRL(i), 0x00550000 | NV50_PDISPLAY_DAC_REGS_DPMS_CTRL_PENDING);
		nv_wr32(NV50_PDISPLAY_DAC_REGS_CLK_CTRL1(i), 0x00000001);
	}

	/* This used to be in crtc unblank, but seems out of place there. */
	nv_wr32(NV50_PDISPLAY_UNK_380, 0);
	/* RAM is clamped to 256 MiB. */
	ram_amount = nouveau_mem_fb_amount(display->dev);
	DRM_DEBUG("ram_amount %d\n", ram_amount);
	if (ram_amount > 256*1024*1024)
		ram_amount = 256*1024*1024;
	nv_wr32(NV50_PDISPLAY_RAM_AMOUNT, ram_amount - 1);
	nv_wr32(NV50_PDISPLAY_UNK_388, 0x150000);
	nv_wr32(NV50_PDISPLAY_UNK_38C, 0);

	display->preinit_done = true;

	return 0;
}

static int nv50_display_init(struct nv50_display *display)
{
	struct drm_device *dev = display->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t val;

	DRM_DEBUG("\n");

	/* The precise purpose is unknown, i suspect it has something to do with text mode. */
	if (nv_rd32(NV50_PDISPLAY_SUPERVISOR) & 0x100) {
		nv_wr32(NV50_PDISPLAY_SUPERVISOR, 0x100);
		nv_wr32(0x006194e8, nv_rd32(0x006194e8) & ~1);
		while (nv_rd32(0x006194e8) & 2);
	}

	/* taken from nv bug #12637 */
	nv_wr32(NV50_PDISPLAY_UNK200_CTRL, 0x2b00);
	do {
		val = nv_rd32(NV50_PDISPLAY_UNK200_CTRL);
		if ((val & 0x9f0000) == 0x20000)
			nv_wr32(NV50_PDISPLAY_UNK200_CTRL, val | 0x800000);

		if ((val & 0x3f0000) == 0x30000)
			nv_wr32(NV50_PDISPLAY_UNK200_CTRL, val | 0x200000);
	} while (val & 0x1e0000);

	nv_wr32(NV50_PDISPLAY_CTRL_STATE, NV50_PDISPLAY_CTRL_STATE_ENABLE);
	nv_wr32(NV50_PDISPLAY_UNK200_CTRL, 0x1000b03);
	while (!(nv_rd32(NV50_PDISPLAY_UNK200_CTRL) & 0x40000000));

	/* For the moment this is just a wrapper, which should be replaced with a real fifo at some point. */
	OUT_MODE(NV50_UNK84, 0);
	OUT_MODE(NV50_UNK88, 0);
	OUT_MODE(NV50_CRTC0_BLANK_CTRL, NV50_CRTC0_BLANK_CTRL_BLANK);
	OUT_MODE(NV50_CRTC0_UNK800, 0);
	OUT_MODE(NV50_CRTC0_DISPLAY_START, 0);
	OUT_MODE(NV50_CRTC0_UNK82C, 0);

	/* enable clock change interrupts. */
	nv_wr32(NV50_PDISPLAY_SUPERVISOR_INTR, nv_rd32(NV50_PDISPLAY_SUPERVISOR_INTR) | 0x70);

	/* enable hotplug interrupts */
	nv_wr32(NV50_PCONNECTOR_HOTPLUG_INTR, 0x7FFF7FFF);

	display->init_done = true;

	return 0;
}

static int nv50_display_disable(struct nv50_display *display)
{
	struct drm_device *dev = display->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct drm_crtc *drm_crtc;
	int i;

	DRM_DEBUG("\n");

	list_for_each_entry(drm_crtc, &dev->mode_config.crtc_list, head) {
		struct nv50_crtc *crtc = to_nv50_crtc(drm_crtc);

		crtc->blank(crtc, true);
	}

	display->update(display);

	/* Almost like ack'ing a vblank interrupt, maybe in the spirit of cleaning up? */
	list_for_each_entry(drm_crtc, &dev->mode_config.crtc_list, head) {
		struct nv50_crtc *crtc = to_nv50_crtc(drm_crtc);

		if (crtc->base.enabled) {
			uint32_t mask;

			if (crtc->index == 1)
				mask = NV50_PDISPLAY_SUPERVISOR_CRTC1;
			else
				mask = NV50_PDISPLAY_SUPERVISOR_CRTC0;

			nv_wr32(NV50_PDISPLAY_SUPERVISOR, mask);
			while (!(nv_rd32(NV50_PDISPLAY_SUPERVISOR) & mask));
		}
	}

	nv_wr32(NV50_PDISPLAY_UNK200_CTRL, 0);
	nv_wr32(NV50_PDISPLAY_CTRL_STATE, 0);
	while ((nv_rd32(NV50_PDISPLAY_UNK200_CTRL) & 0x1e0000) != 0);

	for (i = 0; i < 2; i++) {
		while (nv_rd32(NV50_PDISPLAY_SOR_REGS_DPMS_STATE(i)) & NV50_PDISPLAY_SOR_REGS_DPMS_STATE_WAIT);
	}

	/* disable clock change interrupts. */
	nv_wr32(NV50_PDISPLAY_SUPERVISOR_INTR, nv_rd32(NV50_PDISPLAY_SUPERVISOR_INTR) & ~0x70);

	/* disable hotplug interrupts */
	nv_wr32(NV50_PCONNECTOR_HOTPLUG_INTR, 0);

	display->init_done = false;

	return 0;
}

static int nv50_display_update(struct nv50_display *display)
{
	struct drm_device *dev = display->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	DRM_DEBUG("\n");

	OUT_MODE(NV50_UPDATE_DISPLAY, 0);

	return 0;
}

static void nv50_user_framebuffer_destroy(struct drm_framebuffer *drm_fb)
{
	struct nv50_framebuffer *fb = to_nv50_framebuffer(drm_fb);
	struct drm_device *dev = drm_fb->dev;

	if (drm_fb->fbdev)
		DRM_ERROR("radeonfb_remove(dev, drm_fb);\n");

	if (fb->gem) {
		mutex_lock(&dev->struct_mutex);
		drm_gem_object_unreference(fb->gem);
		mutex_unlock(&dev->struct_mutex);
	}

	drm_framebuffer_cleanup(drm_fb);
	kfree(fb);
}

static int nv50_user_framebuffer_create_handle(struct drm_framebuffer *drm_fb,
					       struct drm_file *file_priv,
					       unsigned int *handle)
{
	struct nv50_framebuffer *fb = to_nv50_framebuffer(drm_fb);

	return drm_gem_handle_create(file_priv, fb->gem, handle);
}

static const struct drm_framebuffer_funcs nv50_framebuffer_funcs = {
	.destroy = nv50_user_framebuffer_destroy,
	.create_handle = nv50_user_framebuffer_create_handle,
};

struct drm_framebuffer *
nv50_framebuffer_create(struct drm_device *dev, struct drm_gem_object *gem,
			struct drm_mode_fb_cmd *mode_cmd)
{
	struct nv50_framebuffer *fb;

	fb = kzalloc(sizeof(struct nv50_framebuffer), GFP_KERNEL);
	if (!fb)
		return NULL;

	drm_framebuffer_init(dev, &fb->base, &nv50_framebuffer_funcs);
	drm_helper_mode_fill_fb_struct(&fb->base, mode_cmd);

	fb->gem = gem;
	return &fb->base;
}

static struct drm_framebuffer *
nv50_user_framebuffer_create(struct drm_device *dev, struct drm_file *file_priv,
			     struct drm_mode_fb_cmd *mode_cmd)
{
	struct drm_gem_object *gem;

	gem = drm_gem_object_lookup(dev, file_priv, mode_cmd->handle);
	return nv50_framebuffer_create(dev, gem, mode_cmd);
}

static int nv50_framebuffer_changed(struct drm_device *dev)
{
	return 0; /* not needed until nouveaufb? */
}

static const struct drm_mode_config_funcs nv50_mode_config_funcs = {
	.fb_create = nv50_user_framebuffer_create,
	.fb_changed = nv50_framebuffer_changed,
};

int nv50_display_create(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nv50_display *display;
	uint32_t bus_mask = 0;
	uint32_t bus_digital = 0, bus_analog = 0;
	int ret, i;

	DRM_DEBUG("\n");

	display = kzalloc(sizeof(*display), GFP_KERNEL);
	if (!display)
		return -ENOMEM;
	dev_priv->display_priv = display;

	/* init basic kernel modesetting */
	drm_mode_config_init(dev);

	/* Initialise some optional connector properties. */
	drm_mode_create_scaling_mode_property(dev);
	drm_mode_create_dithering_property(dev);

	dev->mode_config.min_width = 0;
	dev->mode_config.min_height = 0;

	dev->mode_config.funcs = (void *)&nv50_mode_config_funcs;

	dev->mode_config.max_width = 8192;
	dev->mode_config.max_height = 8192;

	dev->mode_config.fb_base = dev_priv->fb_phys;

	/* Create CRTC objects */
	for (i = 0; i < 2; i++) {
		nv50_crtc_create(dev, i);
	}

	/* we setup the outputs up from the BIOS table */
	for (i = 0 ; i < dev_priv->dcb_table.entries; i++) {
		struct dcb_entry *entry = &dev_priv->dcb_table.entry[i];

		switch (entry->type) {
		case DCB_OUTPUT_TMDS:
		case DCB_OUTPUT_LVDS:
			bus_digital |= (1 << entry->bus);
			nv50_sor_create(dev, entry);
			break;
		case DCB_OUTPUT_ANALOG:
			bus_analog |= (1 << entry->bus);
			nv50_dac_create(dev, entry);
			break;
		default:
			break;
		}
	}

	/* setup the connectors based on the output tables. */
	for (i = 0 ; i < dev_priv->dcb_table.entries; i++) {
		struct dcb_entry *entry = &dev_priv->dcb_table.entry[i];
		int connector = 0;

		/* already done? */
		if (bus_mask & (1 << entry->bus))
			continue;

		/* only do it for supported outputs */
		if (entry->type != DCB_OUTPUT_ANALOG &&
		    entry->type != DCB_OUTPUT_TMDS &&
		    entry->type != DCB_OUTPUT_LVDS)
			continue;

		switch (entry->type) {
		case DCB_OUTPUT_TMDS:
		case DCB_OUTPUT_ANALOG:
			if ((bus_digital & (1 << entry->bus)) &&
			    (bus_analog & (1 << entry->bus)))
				connector = DRM_MODE_CONNECTOR_DVII;
			else
			if (bus_digital & (1 << entry->bus))
				connector = DRM_MODE_CONNECTOR_DVID;
			else
			if (bus_analog & (1 << entry->bus))
				connector = DRM_MODE_CONNECTOR_VGA;
			break;
		case DCB_OUTPUT_LVDS:
			connector = DRM_MODE_CONNECTOR_LVDS;
			break;
		default:
			connector = DRM_MODE_CONNECTOR_Unknown;
			break;
		}

		if (connector == DRM_MODE_CONNECTOR_Unknown)
			continue;

		nv50_connector_create(dev, entry->bus, entry->i2c_index,
				      connector);
		bus_mask |= (1 << entry->bus);
	}

	display->dev = dev;

	/* function pointers */
	display->init = nv50_display_init;
	display->pre_init = nv50_display_pre_init;
	display->disable = nv50_display_disable;
	display->update = nv50_display_update;

	ret = display->pre_init(display);
	if (ret)
		return ret;

	ret = display->init(display);
	if (ret)
		return ret;

	display->update(display);
	return 0;
}

int nv50_display_destroy(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nv50_display *display = nv50_get_display(dev);

	DRM_DEBUG("\n");

	if (display->init_done)
		display->disable(display);

	drm_mode_config_cleanup(dev);

	kfree(display);
	dev_priv->display_priv = NULL;

	return 0;
}

/* This can be replaced with a real fifo in the future. */
void nv50_display_command(struct drm_nouveau_private *dev_priv,
			  uint32_t mthd, uint32_t val)
{
	uint32_t counter = 0;

	DRM_DEBUG("mthd 0x%03X val 0x%08X\n", mthd, val);

	nv_wr32(NV50_PDISPLAY_CTRL_VAL, val);
	nv_wr32(NV50_PDISPLAY_CTRL_STATE, NV50_PDISPLAY_CTRL_STATE_PENDING |
					   NV50_PDISPLAY_CTRL_STATE_ENABLE |
					   0x10000 | mthd);

	while (nv_rd32(NV50_PDISPLAY_CTRL_STATE) & NV50_PDISPLAY_CTRL_STATE_PENDING) {
		counter++;
		if (counter > 1000000) {
			DRM_ERROR("You probably need a reboot now\n");
			break;
		}
		udelay(1);
	}
}

struct nv50_display *nv50_get_display(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	return (struct nv50_display *) dev_priv->display_priv;
}
