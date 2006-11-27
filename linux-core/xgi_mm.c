/**************************************************************************
 *
 * Copyright 2006 Tungsten Graphics, Inc., Bismarck, ND., USA.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 *
 **************************************************************************/

/*
 * Authors:
 *    Thomas Hellström <thomas-at-tungstengraphics-dot-com>
 */

#include "drmP.h"
#include "xgi_drm.h"
#include "xgi_drv.h"

#define VIDEO_TYPE 0
#define AGP_TYPE 1

#define XGI_MM_ALIGN_SHIFT 4
#define XGI_MM_ALIGN_MASK ( (1 << XGI_MM_ALIGN_SHIFT) - 1)

static int xgi_fb_init(DRM_IOCTL_ARGS)
{
	DRM_DEVICE;
	drm_xgi_private_t *dev_priv = dev->dev_private;
	drm_xgi_fb_t fb;
	int ret;

	DRM_COPY_FROM_USER_IOCTL(fb, (drm_xgi_fb_t __user *) data, sizeof(fb));

	mutex_lock(&dev->struct_mutex);
	ret = drm_sman_set_range(&dev_priv->sman, VIDEO_TYPE, 0,
				 fb.size >> XGI_MM_ALIGN_SHIFT);

	if (ret) {
		DRM_ERROR("VRAM memory manager initialisation error\n");
		mutex_unlock(&dev->struct_mutex);
		return ret;
	}

	dev_priv->vram_initialized = 1;
	dev_priv->vram_offset = fb.offset;

	mutex_unlock(&dev->struct_mutex);
	DRM_DEBUG("offset = %u, size = %u", fb.offset, fb.size);

	return 0;
}

static int xgi_drm_alloc(drm_device_t * dev, drm_file_t * priv,
			 unsigned long data, int pool)
{
	drm_xgi_private_t *dev_priv = dev->dev_private;
	drm_xgi_mem_t __user *argp = (drm_xgi_mem_t __user *) data;
	drm_xgi_mem_t mem;
	int retval = 0;
	drm_memblock_item_t *item;

	DRM_COPY_FROM_USER_IOCTL(mem, argp, sizeof(mem));

	mutex_lock(&dev->struct_mutex);

	if (0 == ((pool == 0) ? dev_priv->vram_initialized :
		      dev_priv->agp_initialized)) {
		DRM_ERROR
		    ("Attempt to allocate from uninitialized memory manager.\n");
		return DRM_ERR(EINVAL);
	}

	mem.size = (mem.size + XGI_MM_ALIGN_MASK) >> XGI_MM_ALIGN_SHIFT;
	item = drm_sman_alloc(&dev_priv->sman, pool, mem.size, 0,
			      (unsigned long)priv);

	mutex_unlock(&dev->struct_mutex);
	if (item) {
		mem.offset = ((pool == 0) ?
			      dev_priv->vram_offset : dev_priv->agp_offset) +
		    (item->mm->
		     offset(item->mm, item->mm_info) << XGI_MM_ALIGN_SHIFT);
		mem.free = item->user_hash.key;
		mem.size = mem.size << XGI_MM_ALIGN_SHIFT;
	} else {
		mem.offset = 0;
		mem.size = 0;
		mem.free = 0;
		retval = DRM_ERR(ENOMEM);
	}

	DRM_COPY_TO_USER_IOCTL(argp, mem, sizeof(mem));

	DRM_DEBUG("alloc %d, size = %d, offset = %d\n", pool, mem.size,
		  mem.offset);

	return retval;
}

static int xgi_drm_free(DRM_IOCTL_ARGS)
{
	DRM_DEVICE;
	drm_xgi_private_t *dev_priv = dev->dev_private;
	drm_xgi_mem_t mem;
	int ret;

	DRM_COPY_FROM_USER_IOCTL(mem, (drm_xgi_mem_t __user *) data,
				 sizeof(mem));

	mutex_lock(&dev->struct_mutex);
	ret = drm_sman_free_key(&dev_priv->sman, mem.free);
	mutex_unlock(&dev->struct_mutex);
	DRM_DEBUG("free = 0x%lx\n", mem.free);

	return ret;
}

static int xgi_fb_alloc(DRM_IOCTL_ARGS)
{
	DRM_DEVICE;
	return xgi_drm_alloc(dev, priv, data, VIDEO_TYPE);
}

static int xgi_ioctl_agp_init(DRM_IOCTL_ARGS)
{
	DRM_DEVICE;
	drm_xgi_private_t *dev_priv = dev->dev_private;
	drm_xgi_agp_t agp;
	int ret;
	dev_priv = dev->dev_private;

	DRM_COPY_FROM_USER_IOCTL(agp, (drm_xgi_agp_t __user *) data,
				 sizeof(agp));
	mutex_lock(&dev->struct_mutex);
	ret = drm_sman_set_range(&dev_priv->sman, AGP_TYPE, 0,
				 agp.size >> XGI_MM_ALIGN_SHIFT);

	if (ret) {
		DRM_ERROR("AGP memory manager initialisation error\n");
		mutex_unlock(&dev->struct_mutex);
		return ret;
	}

	dev_priv->agp_initialized = 1;
	dev_priv->agp_offset = agp.offset;
	mutex_unlock(&dev->struct_mutex);

	DRM_DEBUG("offset = %u, size = %u", agp.offset, agp.size);
	return 0;
}

static int xgi_ioctl_agp_alloc(DRM_IOCTL_ARGS)
{
	DRM_DEVICE;

	return xgi_drm_alloc(dev, priv, data, AGP_TYPE);
}

void xgi_lastclose(struct drm_device *dev)
{
	drm_xgi_private_t *dev_priv = dev->dev_private;

	if (!dev_priv)
		return;

	mutex_lock(&dev->struct_mutex);
	drm_sman_cleanup(&dev_priv->sman);
	dev_priv->vram_initialized = 0;
	dev_priv->agp_initialized = 0;
	dev_priv->mmio = NULL;
	mutex_unlock(&dev->struct_mutex);
}

void xgi_reclaim_buffers_locked(drm_device_t * dev, struct file *filp)
{
	drm_xgi_private_t *dev_priv = dev->dev_private;
	drm_file_t *priv = filp->private_data;

	mutex_lock(&dev->struct_mutex);
	if (drm_sman_owner_clean(&dev_priv->sman, (unsigned long)priv)) {
		mutex_unlock(&dev->struct_mutex);
		return;
	}

	if (dev->driver->dma_quiescent) {
		dev->driver->dma_quiescent(dev);
	}

	drm_sman_owner_cleanup(&dev_priv->sman, (unsigned long)priv);
	mutex_unlock(&dev->struct_mutex);
	return;
}

drm_ioctl_desc_t xgi_ioctls[] = {
	[DRM_IOCTL_NR(DRM_XGI_FB_ALLOC)] = {xgi_fb_alloc, DRM_AUTH},
	[DRM_IOCTL_NR(DRM_XGI_FB_FREE)] = {xgi_drm_free, DRM_AUTH},
	[DRM_IOCTL_NR(DRM_XGI_AGP_INIT)] =
	    {xgi_ioctl_agp_init, DRM_AUTH | DRM_MASTER | DRM_ROOT_ONLY},
	[DRM_IOCTL_NR(DRM_XGI_AGP_ALLOC)] = {xgi_ioctl_agp_alloc, DRM_AUTH},
	[DRM_IOCTL_NR(DRM_XGI_AGP_FREE)] = {xgi_drm_free, DRM_AUTH},
	[DRM_IOCTL_NR(DRM_XGI_FB_INIT)] =
	    {xgi_fb_init, DRM_AUTH | DRM_MASTER | DRM_ROOT_ONLY}
};

int xgi_max_ioctl = DRM_ARRAY_SIZE(xgi_ioctls);
