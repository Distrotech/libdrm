/* via_irq.c
 *
 * Copyright 2004 BEAM Ltd.
 * Copyright (c) 2002-2008 Tungsten Graphics, Inc., Cedar Park, TX., USA,
 * All Rights Reserved.
 * Copyright 2005 Thomas Hellstrom.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * BEAM LTD, TUNGSTEN GRAPHICS  AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Terry Barnaby <terry1@beam.ltd.uk>
 *    Keith Whitwell <keith@tungstengraphics.com>
 *    Thomas Hellstrom <unichrome@shipmail.org>
 *
 * This code provides standard DRM access to the Via Unichrome / Pro Vertical blank
 * interrupt, as well as an infrastructure to handle other interrupts of the chip.
 * The refresh rate is also calculated for video playback sync purposes.
 */

#include "drmP.h"
#include "drm.h"
#include "ochr_drm.h"
#include "via_drv.h"

#define VIA_REG_INTERRUPT       0x200

/* VIA_REG_INTERRUPT */
#define VIA_IRQ_GLOBAL	  (1 << 31)
#define VIA_IRQ_VBLANK_ENABLE   (1 << 19)
#define VIA_IRQ_VBLANK_PENDING  (1 << 3)
#define VIA_IRQ_HQV0_ENABLE     (1 << 11)
#define VIA_IRQ_HQV1_ENABLE     (1 << 25)
#define VIA_IRQ_HQV0_PENDING    (1 << 9)
#define VIA_IRQ_HQV1_PENDING    (1 << 10)
#define VIA_IRQ_DMA0_DD_ENABLE  (1 << 20)
#define VIA_IRQ_DMA0_TD_ENABLE  (1 << 21)
#define VIA_IRQ_DMA1_DD_ENABLE  (1 << 22)
#define VIA_IRQ_DMA1_TD_ENABLE  (1 << 23)
#define VIA_IRQ_DMA0_DD_PENDING (1 << 4)
#define VIA_IRQ_DMA0_TD_PENDING (1 << 5)
#define VIA_IRQ_DMA1_DD_PENDING (1 << 6)
#define VIA_IRQ_DMA1_TD_PENDING (1 << 7)

u32 via_get_vblank_counter(struct drm_device * dev, int crtc)
{
	struct drm_via_private *dev_priv = dev->dev_private;
	if (crtc != 0)
		return 0;

	return atomic_read(&dev_priv->vbl_received);
}

irqreturn_t via_driver_irq_handler(DRM_IRQ_ARGS)
{
	struct drm_device *dev = (struct drm_device *)arg;
	struct drm_via_private *dev_priv = via_priv(dev);
	u32 status;
	u32 pending;
	int handled = 0;

	spin_lock(&dev_priv->irq_lock);
	status = VIA_READ(VIA_REG_INTERRUPT);

	pending = status & dev_priv->irq_pending_mask;

	if (pending & VIA_IRQ_HQV0_PENDING) {
		via_ttm_fence_cmd_handler(dev_priv, VIA_FENCE_TYPE_HQV0);
		handled = 1;
	}
	if (pending & VIA_IRQ_HQV1_PENDING) {
		via_ttm_fence_cmd_handler(dev_priv, VIA_FENCE_TYPE_HQV1);
		handled = 1;
	}

	if (pending & VIA_IRQ_DMA0_TD_PENDING) {
		via_ttm_fence_dmablit_handler(dev_priv, VIA_ENGINE_DMA0);
		handled = 1;
	}
	if (pending & VIA_IRQ_DMA1_TD_PENDING) {
		handled = 1;
		via_ttm_fence_dmablit_handler(dev_priv, VIA_ENGINE_DMA1);
	}

	if (status & VIA_IRQ_VBLANK_PENDING) {
		atomic_inc(&dev_priv->vbl_received);
		drm_handle_vblank(dev, 0);
		handled = 1;
	}

	/* Acknowlege interrupts */
	VIA_WRITE(VIA_REG_INTERRUPT, status);
	(void)VIA_READ(VIA_REG_INTERRUPT);

	spin_unlock(&dev_priv->irq_lock);

	if (handled)
		return IRQ_HANDLED;
	else
		return IRQ_NONE;
}

static __inline__ void viadrv_acknowledge_irqs(struct drm_via_private *dev_priv)
{
	u32 status;

	if (dev_priv) {
		/* Acknowlege interrupts */
		status = VIA_READ(VIA_REG_INTERRUPT);
		VIA_WRITE(VIA_REG_INTERRUPT, status);
		(void)VIA_READ(VIA_REG_INTERRUPT);
	}
}

int via_enable_vblank(struct drm_device *dev, int crtc)
{
	struct drm_via_private *dev_priv = via_priv(dev);
	unsigned long irq_flags;

	if (crtc != 0) {
		DRM_ERROR("%s:  bad crtc %d\n", __FUNCTION__, crtc);
		return -EINVAL;
	}

	spin_lock_irqsave(&dev_priv->irq_lock, irq_flags);
	dev_priv->irq_enable_mask |= VIA_IRQ_VBLANK_ENABLE;
	VIA_WRITE(VIA_REG_INTERRUPT, dev_priv->irq_enable_mask);
	(void)VIA_READ(VIA_REG_INTERRUPT);
	spin_unlock_irqrestore(&dev_priv->irq_lock, irq_flags);

	return 0;
}

void via_disable_vblank(struct drm_device *dev, int crtc)
{
	struct drm_via_private *dev_priv = via_priv(dev);
	unsigned long irq_flags;

	if (crtc != 0)
		DRM_ERROR("%s:  bad crtc %d\n", __FUNCTION__, crtc);

	spin_lock_irqsave(&dev_priv->irq_lock, irq_flags);
	dev_priv->irq_enable_mask &= ~VIA_IRQ_VBLANK_ENABLE;
	VIA_WRITE(VIA_REG_INTERRUPT, dev_priv->irq_enable_mask);
	(void)VIA_READ(VIA_REG_INTERRUPT);
	spin_unlock_irqrestore(&dev_priv->irq_lock, irq_flags);
}

/*
 * drm_dma.h hooks
 */

void via_driver_irq_preinstall(struct drm_device *dev)
{
	struct drm_via_private *dev_priv = via_priv(dev);

	DRM_DEBUG("dev_priv: %p\n", dev_priv);
	if (dev_priv) {
		spin_lock_irq(&dev_priv->irq_lock);
		dev_priv->irq_enable_mask =
		    VIA_IRQ_GLOBAL |
		    VIA_IRQ_DMA0_TD_ENABLE | VIA_IRQ_DMA1_TD_ENABLE;
		dev_priv->irq2_enable_mask = 0;
		dev_priv->irq_pending_mask =
		    VIA_IRQ_VBLANK_PENDING |
		    VIA_IRQ_DMA0_TD_PENDING | VIA_IRQ_DMA1_TD_PENDING;
		dev_priv->irq2_pending_mask = 0;

		if (dev_priv->chipset == VIA_PRO_GROUP_A ||
		    dev_priv->chipset == VIA_DX9_0) {
			dev_priv->irq_enable_mask |=
			    VIA_IRQ_HQV0_ENABLE | VIA_IRQ_HQV1_ENABLE;
			dev_priv->irq_pending_mask |=
			    VIA_IRQ_HQV0_PENDING | VIA_IRQ_HQV1_PENDING;
		}

		/* Clear bits if they're already high */
		viadrv_acknowledge_irqs(dev_priv);
		spin_unlock_irq(&dev_priv->irq_lock);
	}
}

int via_driver_irq_postinstall(struct drm_device *dev)
{
	struct drm_via_private *dev_priv = via_priv(dev);

	DRM_DEBUG("via_driver_irq_postinstall\n");
	if (!dev_priv)
		return -EINVAL;

	drm_vblank_init(dev, 1);

	spin_lock_irq(&dev_priv->irq_lock);

	VIA_WRITE(VIA_REG_INTERRUPT, dev_priv->irq_enable_mask);
	wmb();

	/* Some magic, oh for some data sheets ! */
	VIA_WRITE8(0x83d4, 0x11);
	wmb();
	VIA_WRITE8(0x83d5, VIA_READ8(0x83d5) | 0x30);
	(void)VIA_READ8(0x83d5);

	spin_unlock_irq(&dev_priv->irq_lock);
	return 0;
}

void via_driver_irq_uninstall(struct drm_device *dev)
{
	struct drm_via_private *dev_priv = via_priv(dev);
	u32 status;

	DRM_DEBUG("\n");
	if (dev_priv) {
		spin_lock_irq(&dev_priv->irq_lock);
		/* Some more magic, oh for some data sheets ! */

		VIA_WRITE8(0x83d4, 0x11);
		wmb();
		VIA_WRITE8(0x83d5, VIA_READ8(0x83d5) & ~0x30);

		status = VIA_READ(VIA_REG_INTERRUPT);
		VIA_WRITE(VIA_REG_INTERRUPT,
			  status & ~dev_priv->irq_enable_mask);
		(void)VIA_READ(VIA_REG_INTERRUPT);
		spin_unlock_irq(&dev_priv->irq_lock);
	}
}
