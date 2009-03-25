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
#include "nouveau_crtc.h"
#include "nouveau_encoder.h"
#include "nouveau_connector.h"
#include "nouveau_fb.h"
#include "drm_crtc_helper.h"

static int nv50_display_pre_init(struct drm_device *dev)
{
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
	nv_wr32(0x006101e0 + 2 * 0x4, nv_rd32(0x0061c000 + 2 * 0x800));
	/* Something not yet in use, tv-out maybe. */
	nv_wr32(0x006101f0 + 0 * 0x4, nv_rd32(0x0061e000 + 0 * 0x800));
	nv_wr32(0x006101f0 + 1 * 0x4, nv_rd32(0x0061e000 + 1 * 0x800));
	nv_wr32(0x006101f0 + 2 * 0x4, nv_rd32(0x0061e000 + 2 * 0x800));

	for (i = 0; i < 3; i++) {
		nv_wr32(NV50_PDISPLAY_DAC_REGS_DPMS_CTRL(i), 0x00550000 |
			NV50_PDISPLAY_DAC_REGS_DPMS_CTRL_PENDING);
		nv_wr32(NV50_PDISPLAY_DAC_REGS_CLK_CTRL1(i), 0x00000001);
	}

	/* This used to be in crtc unblank, but seems out of place there. */
	nv_wr32(NV50_PDISPLAY_UNK_380, 0);
	/* RAM is clamped to 256 MiB. */
	ram_amount = nouveau_mem_fb_amount(dev);
	DRM_DEBUG("ram_amount %d\n", ram_amount);
	if (ram_amount > 256*1024*1024)
		ram_amount = 256*1024*1024;
	nv_wr32(NV50_PDISPLAY_RAM_AMOUNT, ram_amount - 1);
	nv_wr32(NV50_PDISPLAY_UNK_388, 0x150000);
	nv_wr32(NV50_PDISPLAY_UNK_38C, 0);

	return 0;
}

static int
nv50_display_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_timer_engine *ptimer = &dev_priv->engine.timer;
	uint64_t start;
	uint32_t val;

	DRM_DEBUG("\n");

	/* The precise purpose is unknown, i suspect it has something to do
	 * with text mode.
	 */
	if (nv_rd32(NV50_PDISPLAY_SUPERVISOR) & 0x100) {
		nv_wr32(NV50_PDISPLAY_SUPERVISOR, 0x100);
		nv_wr32(0x006194e8, nv_rd32(0x006194e8) & ~1);
		if (!nv_wait(0x006194e8, 2, 0)) {
			DRM_ERROR("timeout: (0x6194e8 & 2) != 0\n");
			DRM_ERROR("0x6194e8 = 0x%08x\n", nv_rd32(0x6194e8));
			return -EBUSY;
		}
	}

	/* taken from nv bug #12637, attempts to un-wedge the hw if it's
	 * stuck in some unspecified state
	 */
	start = ptimer->read(dev);
	nv_wr32(NV50_PDISPLAY_UNK200_CTRL, 0x2b00);
	while ((val = nv_rd32(NV50_PDISPLAY_UNK200_CTRL)) & 0x1e0000) {
		if ((val & 0x9f0000) == 0x20000)
			nv_wr32(NV50_PDISPLAY_UNK200_CTRL, val | 0x800000);

		if ((val & 0x3f0000) == 0x30000)
			nv_wr32(NV50_PDISPLAY_UNK200_CTRL, val | 0x200000);

		if (ptimer->read(dev) - start > 1000000000ULL) {
			DRM_ERROR("timeout: (0x610200 & 0x1e0000) != 0\n");
			DRM_ERROR("0x610200 = 0x%08x\n", val);
			return -EBUSY;
		}
	}

	nv_wr32(NV50_PDISPLAY_CTRL_STATE, NV50_PDISPLAY_CTRL_STATE_ENABLE);
	nv_wr32(NV50_PDISPLAY_UNK200_CTRL, 0x1000b03);
	if (!nv_wait(NV50_PDISPLAY_UNK200_CTRL, 0x40000000, 0x40000000)) {
		DRM_ERROR("timeout: (0x610200 & 0x40000000) == 0x40000000\n");
		DRM_ERROR("0x610200 = 0x%08x\n",
			  nv_rd32(NV50_PDISPLAY_UNK200_CTRL));
		return -EBUSY;
	}

	/* For the moment this is just a wrapper, which should be replaced with a real fifo at some point. */
	OUT_MODE(NV50_UNK84, 0);
	OUT_MODE(NV50_UNK88, 0);
	OUT_MODE(NV50_CRTC0_BLANK_CTRL, NV50_CRTC0_BLANK_CTRL_BLANK);
	OUT_MODE(NV50_CRTC0_UNK800, 0);
	OUT_MODE(NV50_CRTC0_DISPLAY_START, 0);
	OUT_MODE(NV50_CRTC0_UNK82C, 0);

	/* enable clock change interrupts. */
//	nv_wr32(NV50_PDISPLAY_SUPERVISOR_INTR, nv_rd32(NV50_PDISPLAY_SUPERVISOR_INTR) | 0x70);

	/* enable hotplug interrupts */
	nv_wr32(NV50_PCONNECTOR_HOTPLUG_CTRL, 0x7FFF7FFF);
//	nv_wr32(NV50_PCONNECTOR_HOTPLUG_INTR, 0x7FFF7FFF);


	return 0;
}

static int nv50_display_disable(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct drm_crtc *drm_crtc;
	int i;

	DRM_DEBUG("\n");

	dev_priv->in_modeset = true;
	list_for_each_entry(drm_crtc, &dev->mode_config.crtc_list, head) {
		struct drm_crtc_helper_funcs *helper = drm_crtc->helper_private;

		helper->dpms(drm_crtc, DRM_MODE_DPMS_OFF);
	}
	dev_priv->in_modeset = false;

	OUT_MODE(NV50_UPDATE_DISPLAY, 0);

	/* Almost like ack'ing a vblank interrupt, maybe in the spirit of cleaning up? */
	list_for_each_entry(drm_crtc, &dev->mode_config.crtc_list, head) {
		struct nouveau_crtc *crtc = to_nouveau_crtc(drm_crtc);

		if (crtc->base.enabled) {
			uint32_t mask;

			if (crtc->index == 1)
				mask = NV50_PDISPLAY_SUPERVISOR_CRTC1;
			else
				mask = NV50_PDISPLAY_SUPERVISOR_CRTC0;

			nv_wr32(NV50_PDISPLAY_SUPERVISOR, mask);
			if (!nv_wait(NV50_PDISPLAY_SUPERVISOR, mask, mask)) {
				DRM_ERROR("timeout: (0x610024 & 0x%08x) == "
					  "0x%08x\n", mask, mask);
				DRM_ERROR("0x610024 = 0x%08x\n",
					  nv_rd32(0x610024));
			}
		}
	}

#if 0
	nv_wr32(NV50_PDISPLAY_UNK200_CTRL, 0);
	nv_wr32(NV50_PDISPLAY_CTRL_STATE, 0);
	if (!nv_wait(NV50_PDISPLAY_UNK200_CTRL, 0x1e0000, 0)) {
		DRM_ERROR("timeout: (0x610200 & 0x1e0000) == 0\n");
		DRM_ERROR("0x610200 = 0x%08x\n",
			  nv_rd32(NV50_PDISPLAY_UNK200_CTRL));
	}
#endif

	for (i = 0; i < NV50_PDISPLAY_SOR_REGS__LEN; i++) {
		if (!nv_wait(NV50_PDISPLAY_SOR_REGS_DPMS_STATE(i),
			     NV50_PDISPLAY_SOR_REGS_DPMS_STATE_WAIT, 0)) {
			DRM_ERROR("timeout: SOR_DPMS_STATE_WAIT(%d) == 0\n", i);
			DRM_ERROR("SOR_DPMS_STATE(%d) = 0x%08x\n", i,
				  nv_rd32(NV50_PDISPLAY_SOR_REGS_DPMS_STATE(i)));
		}
	}

	/* disable clock change interrupts. */
	nv_wr32(NV50_PDISPLAY_SUPERVISOR_INTR,
		nv_rd32(NV50_PDISPLAY_SUPERVISOR_INTR) & ~0x70);

	/* disable hotplug interrupts */
	nv_wr32(NV50_PCONNECTOR_HOTPLUG_INTR, 0);

	return 0;
}

int nv50_display_create(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t bus_mask = 0;
	uint32_t bus_digital = 0, bus_analog = 0;
	int ret, i;

	DRM_DEBUG("\n");

	/* init basic kernel modesetting */
	drm_mode_config_init(dev);

	/* Initialise some optional connector properties. */
	drm_mode_create_scaling_mode_property(dev);
	drm_mode_create_dithering_property(dev);

	dev->mode_config.min_width = 0;
	dev->mode_config.min_height = 0;

	dev->mode_config.funcs = (void *)&nouveau_mode_config_funcs;

	dev->mode_config.max_width = 8192;
	dev->mode_config.max_height = 8192;

	dev->mode_config.fb_base = dev_priv->fb_phys;

	ret = nv50_display_pre_init(dev);
	if (ret)
		return ret;

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

	ret = nv50_display_init(dev);
	if (ret)
		return ret;

	return 0;
}

int nv50_display_destroy(struct drm_device *dev)
{
	DRM_DEBUG("\n");

	nv50_display_disable(dev);

	drm_mode_config_cleanup(dev);

	return 0;
}

/* This can be replaced with a real fifo in the future. */
static void nv50_display_vclk_update(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct drm_encoder *drm_encoder;
	struct drm_crtc *drm_crtc;
	struct nouveau_encoder *encoder = NULL;
	struct nouveau_crtc *crtc = NULL;
	int crtc_index;
	uint32_t unk30 = nv_rd32(NV50_PDISPLAY_UNK30_CTRL);

	for (crtc_index = 0; crtc_index < 2; crtc_index++) {
		bool clock_change = false;
		bool clock_ack = false;

		if (crtc_index == 0 && (unk30 & NV50_PDISPLAY_UNK30_CTRL_UPDATE_VCLK0))
			clock_change = true;

		if (crtc_index == 1 && (unk30 & NV50_PDISPLAY_UNK30_CTRL_UPDATE_VCLK1))
			clock_change = true;

		if (clock_change)
			clock_ack = true;

#if 0
		if (dev_priv->last_crtc == crtc_index)
#endif
			clock_ack = true;

		list_for_each_entry(drm_crtc, &dev->mode_config.crtc_list, head) {
			crtc = to_nouveau_crtc(drm_crtc);
			if (crtc->index == crtc_index)
				break;
		}

		if (clock_change)
			crtc->set_clock(crtc, crtc->mode);

		DRM_DEBUG("index %d clock_change %d clock_ack %d\n", crtc_index, clock_change, clock_ack);

		if (!clock_ack)
			continue;

		crtc->set_clock_mode(crtc);

		list_for_each_entry(drm_encoder, &dev->mode_config.encoder_list, head) {
			encoder = to_nouveau_encoder(drm_encoder);

			if (!drm_encoder->crtc)
				continue;

			if (drm_encoder->crtc == drm_crtc)
				encoder->set_clock_mode(encoder, crtc->mode);
		}
	}
}

void nv50_display_command(struct drm_device *dev, uint32_t mthd, uint32_t val)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_timer_engine *ptimer = &dev_priv->engine.timer;
	uint64_t start;

	DRM_DEBUG("mthd 0x%03X val 0x%08X\n", mthd, val);

	nv_wr32(NV50_PDISPLAY_CTRL_VAL, val);
	nv_wr32(NV50_PDISPLAY_CTRL_STATE, NV50_PDISPLAY_CTRL_STATE_PENDING |
					   NV50_PDISPLAY_CTRL_STATE_ENABLE |
					   0x10000 | mthd);

	start = ptimer->read(dev);
	while (nv_rd32(NV50_PDISPLAY_CTRL_STATE) & NV50_PDISPLAY_CTRL_STATE_PENDING) {
		const uint32_t super = nv_rd32(NV50_PDISPLAY_SUPERVISOR);
		uint32_t state;

		state   = (super & NV50_PDISPLAY_SUPERVISOR_CLK_MASK);
		state >>= NV50_PDISPLAY_SUPERVISOR_CLK_MASK__SHIFT;
		if (state) {
			if (state == 2)
				nv50_display_vclk_update(dev);

			nv_wr32(NV50_PDISPLAY_SUPERVISOR,
				(super & NV50_PDISPLAY_SUPERVISOR_CLK_MASK));
			nv_wr32(NV50_PDISPLAY_UNK30_CTRL,
				NV50_PDISPLAY_UNK30_CTRL_PENDING);
		}

		if (ptimer->read(dev) - start > 1000000000ULL) {
			DRM_ERROR("timeout: 0x610300 = 0x%08x\n",
				  nv_rd32(NV50_PDISPLAY_CTRL_STATE));
			break;
		}
	}
}

struct nv50_display *nv50_get_display(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	return (struct nv50_display *) dev_priv->display_priv;
}
