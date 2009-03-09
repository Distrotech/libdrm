#include "drmP.h"
#include "nouveau_drv.h"
#include "nouveau_dma.h"
#include "nouveau_fbcon.h"

static int
nv50_fbcon_sync(struct fb_info *info)
{
	struct nouveau_fbcon_par *par = info->par;
	struct drm_device *dev = par->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_channel *chan = dev_priv->channel;
	int ret, i;

	if (info->state != FBINFO_STATE_RUNNING ||
	    info->flags & FBINFO_HWACCEL_DISABLED)
		return 0;

	if (RING_SPACE(chan, 4)) {
		DRM_ERROR("GPU lockup - switching to software fbcon\n");
		info->flags |= FBINFO_HWACCEL_DISABLED;
		return 0;
	}

	BEGIN_RING(chan, 0, 0x0104, 1);
	OUT_RING  (chan, 0);
	BEGIN_RING(chan, 0, 0x0100, 1);
	OUT_RING  (chan, 0);
	chan->m2mf_ntfy_map[3] = 0xffffffff;
	FIRE_RING (chan);

	ret = -EBUSY;
	for (i = 0; i < 100000; i++) {
		if (chan->m2mf_ntfy_map[3] == 0) {
			ret = 0;
			break;
		}
		DRM_UDELAY(1);
	}

	if (ret) {
		DRM_ERROR("GPU lockup - switching to software fbcon\n");
		info->flags |= FBINFO_HWACCEL_DISABLED;
		return 0;
	}

	return 0;
}

static void
nv50_fbcon_fillrect(struct fb_info *info, const struct fb_fillrect *rect)
{
	struct nouveau_fbcon_par *par = info->par;
	struct drm_nouveau_private *dev_priv = par->dev->dev_private;
	struct nouveau_channel *chan = dev_priv->channel;

	if (info->state != FBINFO_STATE_RUNNING)
		return;

	if (info->flags & FBINFO_HWACCEL_DISABLED) {
		cfb_fillrect(info, rect);
		return;
	}

	if (RING_SPACE(chan, 9)) {
		DRM_ERROR("GPU lockup - switching to software fbcon\n");

		info->flags |= FBINFO_HWACCEL_DISABLED;
		cfb_fillrect(info, rect);
		return;
	}

	BEGIN_RING(chan, NvSub2D, 0x02ac, 1);
	OUT_RING  (chan, rect->rop == ROP_COPY ? 3 : 1);
	BEGIN_RING(chan, NvSub2D, 0x0588, 1);
	OUT_RING  (chan, rect->color);
	BEGIN_RING(chan, NvSub2D, 0x0600, 4);
	OUT_RING  (chan, rect->dx);
	OUT_RING  (chan, rect->dy);
	OUT_RING  (chan, rect->dx + rect->width);
	OUT_RING  (chan, rect->dy + rect->height);
	FIRE_RING (chan);
}

static void
nv50_fbcon_copyarea(struct fb_info *info, const struct fb_copyarea *region)
{
	struct nouveau_fbcon_par *par = info->par;
	struct drm_nouveau_private *dev_priv = par->dev->dev_private;
	struct nouveau_channel *chan = dev_priv->channel;

	if (info->state != FBINFO_STATE_RUNNING)
		return;

	if (info->flags & FBINFO_HWACCEL_DISABLED) {
		cfb_copyarea(info, region);
		return;
	}

	if (RING_SPACE(chan, 17)) {
		DRM_ERROR("GPU lockup - switching to software fbcon\n");

		info->flags |= FBINFO_HWACCEL_DISABLED;
		cfb_copyarea(info, region);
		return;
	}

	BEGIN_RING(chan, NvSub2D, 0x02ac, 1);
	OUT_RING  (chan, 3);
	BEGIN_RING(chan, NvSub2D, 0x0110, 1);
	OUT_RING  (chan, 0);
	BEGIN_RING(chan, NvSub2D, 0x08b0, 12);
	OUT_RING  (chan, region->dx);
	OUT_RING  (chan, region->dy);
	OUT_RING  (chan, region->width);
	OUT_RING  (chan, region->height);
	OUT_RING  (chan, 0);
	OUT_RING  (chan, 1);
	OUT_RING  (chan, 0);
	OUT_RING  (chan, 1);
	OUT_RING  (chan, 0);
	OUT_RING  (chan, region->sx);
	OUT_RING  (chan, 0);
	OUT_RING  (chan, region->sy);
	FIRE_RING (chan);
}

static void
nv50_fbcon_imageblit(struct fb_info *info, const struct fb_image *image)
{
	if (info->state != FBINFO_STATE_RUNNING)
		return;

	if (info->flags & FBINFO_HWACCEL_DISABLED) {
		cfb_imageblit(info, image);
		return;
	}

	cfb_imageblit(info, image);
}

int
nv50_fbcon_accel_init(struct fb_info *info)
{
	struct nouveau_fbcon_par *par = info->par;
	struct drm_device *dev = par->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_channel *chan = dev_priv->channel;
	struct nouveau_gpuobj *eng2d = NULL;
	int ret, format;

	switch (info->var.bits_per_pixel) {
	case 16:
		format = 0xe8;
		break;
	default:
		format = 0xe6;
		break;
	}

	ret = nouveau_gpuobj_gr_new(dev_priv->channel, 0x502d, &eng2d);
	if (ret)
		return ret;

	ret = nouveau_gpuobj_ref_add(dev, dev_priv->channel, Nv2D, eng2d, NULL);
	if (ret)
		return ret;

	ret = RING_SPACE(chan, 34);
	if (ret) {
		DRM_ERROR("GPU lockup - switching to software fbcon\n");
		return ret;
	}

	BEGIN_RING(chan, NvSub2D, 0x0000, 1);
	OUT_RING  (chan, Nv2D);
	BEGIN_RING(chan, NvSub2D, 0x0180, 4);
	OUT_RING  (chan, NvNotify0);
	OUT_RING  (chan, chan->vram_handle);
	OUT_RING  (chan, chan->vram_handle);
	OUT_RING  (chan, chan->vram_handle);
	BEGIN_RING(chan, NvSub2D, 0x0290, 1);
	OUT_RING  (chan, 0);
	BEGIN_RING(chan, NvSub2D, 0x0888, 1);
	OUT_RING  (chan, 1);
	BEGIN_RING(chan, NvSub2D, 0x02a0, 1);
	OUT_RING  (chan, 0x55);
	BEGIN_RING(chan, NvSub2D, 0x0580, 2);
	OUT_RING  (chan, 4);
	OUT_RING  (chan, format);
	BEGIN_RING(chan, NvSub2D, 0x0200, 2);
	OUT_RING  (chan, format);
	OUT_RING  (chan, 1);
	BEGIN_RING(chan, NvSub2D, 0x0214, 5);
	OUT_RING  (chan, info->fix.line_length);
	OUT_RING  (chan, info->var.xres_virtual);
	OUT_RING  (chan, info->var.yres_virtual);
	OUT_RING  (chan, 0);
	OUT_RING  (chan, info->fix.smem_start - dev_priv->fb_phys +
			 dev_priv->vm_vram_base);
	BEGIN_RING(chan, NvSub2D, 0x0230, 2);
	OUT_RING  (chan, format);
	OUT_RING  (chan, 1);
	BEGIN_RING(chan, NvSub2D, 0x0244, 5);
	OUT_RING  (chan, info->fix.line_length);
	OUT_RING  (chan, info->var.xres_virtual);
	OUT_RING  (chan, info->var.yres_virtual);
	OUT_RING  (chan, 0);
	OUT_RING  (chan, info->fix.smem_start - dev_priv->fb_phys +
			 dev_priv->vm_vram_base);

	info->fbops->fb_fillrect = nv50_fbcon_fillrect;
	info->fbops->fb_copyarea = nv50_fbcon_copyarea;
	info->fbops->fb_imageblit = nv50_fbcon_imageblit;
	info->fbops->fb_sync = nv50_fbcon_sync;
	return 0;
}

