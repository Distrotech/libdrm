/**************************************************************************
 *
 * Copyright (c) 2008 Tungsten Graphics, Inc., Cedar Park, TX., USA
 * All Rights Reserved.
 * Copyright (c) 2009 VMware, Inc., Palo Alto, CA., USA,
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
 **************************************************************************/
/*
 * Authors: Thomas Hellstrom <thomas-at-tungstengraphics-dot-com>
 */

#include "drmP.h"
#include "ochr_drm.h"
#include "via_drv.h"
#include "ttm/ttm_object.h"
#include "ttm/ttm_pat_compat.h"
#include "ttm/ttm_lock.h"

#define VIA_AGP_MODE_MASK 0x17
#define VIA_AGPV3_MODE    0x08
#define VIA_AGPV3_8X_MODE 0x02
#define VIA_AGPV3_4X_MODE 0x01
#define VIA_AGP_4X_MODE 0x04
#define VIA_AGP_2X_MODE 0x02
#define VIA_AGP_1X_MODE 0x01
#define VIA_AGP_FW_MODE 0x10

#define DEV_AGP_OFFSET  (0*1024*1024)
#define DEV_VRAM_OFFSET (0*1024*1024)

#define DRM_FILE_PAGE_OFFSET (0x100000000ULL >> PAGE_SHIFT)

/**
 * If there's no thrashing. This is the preferred memory type order.
 */
static uint32_t via_mem_prios[] =
    { TTM_PL_PRIV0, TTM_PL_VRAM, TTM_PL_TT, TTM_PL_SYSTEM };

/**
 * If we have thrashing, most memory will be evicted to TT anyway, so we might as well
 * just move the new buffer into TT from the start.
 */
static uint32_t via_busy_prios[] =
    { TTM_PL_TT, TTM_PL_PRIV0, TTM_PL_VRAM, TTM_PL_SYSTEM };

static struct ttm_bo_driver via_bo_driver = {
	.mem_type_prio = via_mem_prios,
	.mem_busy_prio = via_busy_prios,
	.num_mem_type_prio = ARRAY_SIZE(via_mem_prios),
	.num_mem_busy_prio = ARRAY_SIZE(via_busy_prios),
	.create_ttm_backend_entry = via_create_ttm_backend_entry,
	.invalidate_caches = via_invalidate_caches,
	.init_mem_type = via_init_mem_type,
	.evict_flags = via_evict_flags,
	.move = via_bo_move,
	.verify_access = via_verify_access,
	.sync_obj_signaled = ttm_fence_sync_obj_signaled,
	.sync_obj_wait = ttm_fence_sync_obj_wait,
	.sync_obj_flush = ttm_fence_sync_obj_flush,
	.sync_obj_unref = ttm_fence_sync_obj_unref,
	.sync_obj_ref = ttm_fence_sync_obj_ref,
};

/*
 * From VIA's code.
 */
#if 0
static void via_pcie_engine_init(struct drm_via_private *dev_priv)
{
	VIA_WRITE(0x41c, 0x00100000);
	VIA_WRITE(0x420, 0x680A0000);
	VIA_WRITE(0x420, 0x02000000);
}
#endif

static void via_irq_init(struct drm_device *dev)
{
	if (dev->pdev->device == 0x3108 || dev->pdev->device == 0x7205) {
		return;
	} else {
		(void)drm_irq_install(dev);
	}
}

static void via_agp_engine_init(struct drm_via_private *dev_priv)
{
	VIA_WRITE(VIA_REG_TRANSET, 0x00100000);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x00000000);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x00333004);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x60000000);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x61000000);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x62000000);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x63000000);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x64000000);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x7D000000);

	VIA_WRITE(VIA_REG_TRANSET, 0xfe020000);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x00000000);
}

static void via_2d_engine_init(struct drm_via_private *dev_priv)
{
	int i;

	for (i = 0x04; i < 0x44; i += 4) {
		VIA_WRITE(i, 0x0);
	}
}

static void via_soft_reset(struct drm_via_private *dev_priv)
{
#if 0
	u8 tmp;
	VIA_WRITE8(0x83c4, 0x1A);
	tmp = VIA_READ8(0x83c5);
	tmp |= 0x06;
	VIA_WRITE8(0x83c5, tmp);
	tmp = VIA_READ8(0x83c5);
#endif
}

static void via_3d_engine_init(struct drm_via_private *dev_priv)
{
	int i;

	/*
	 * Most of these register values are taken from VIA's
	 * X server code.
	 */

	VIA_WRITE(VIA_REG_TRANSET, 0x00010000);
	for (i = 0; i <= 0x7D; i++)
		VIA_WRITE(VIA_REG_TRANSPACE, (uint32_t) i << 24);

	VIA_WRITE(VIA_REG_TRANSET, 0x00020000);
	for (i = 0; i <= 0x94; i++)
		VIA_WRITE(VIA_REG_TRANSPACE, (uint32_t) i << 24);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x82400000);

	VIA_WRITE(VIA_REG_TRANSET, 0x01020000);
	for (i = 0; i <= 0x94; i++)
		VIA_WRITE(VIA_REG_TRANSPACE, (uint32_t) i << 24);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x82400000);

	VIA_WRITE(VIA_REG_TRANSET, 0xfe020000);
	for (i = 0; i <= 0x03; i++)
		VIA_WRITE(VIA_REG_TRANSPACE, (uint32_t) i << 24);

	VIA_WRITE(VIA_REG_TRANSET, 0x00030000);
	for (i = 0; i <= 0xff; i++)
		VIA_WRITE(VIA_REG_TRANSPACE, 0);

	VIA_WRITE(VIA_REG_TRANSET, 0x00100000);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x00333004);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x10000002);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x60000000);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x61000000);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x62000000);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x63000000);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x64000000);

	VIA_WRITE(VIA_REG_TRANSET, 0x00fe0000);
#if 0
	if (pVia->Chipset == VIA_CLE266 && pVia->ChipRev >= 3)
		VIA_WRITE(VIA_REG_TRANSPACE, 0x40008c0f);
	else
#endif
		VIA_WRITE(VIA_REG_TRANSPACE, 0x4000800f);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x44000000);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x45080C04);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x46800408);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x50000000);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x51000000);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x52000000);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x53000000);

	VIA_WRITE(VIA_REG_TRANSET, 0x00fe0000);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x08000001);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x0A000183);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x0B00019F);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x0C00018B);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x0D00019B);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x0E000000);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x0F000000);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x10000000);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x11000000);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x20000000);
}

static void via_vq_disable(struct drm_via_private *dev_priv)
{
	VIA_WRITE(VIA_REG_TRANSET, 0x00fe0000);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x00000004);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x40008c0f);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x44000000);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x45080c04);
}

static void via_vq_enable(struct drm_via_private *dev_priv)
{
	uint32_t vq_start_offs = dev_priv->vq_bo->offset;
	uint32_t vq_end_offs = vq_start_offs + VIA_VQ_SIZE - 1;

	VIA_WRITE(VIA_REG_TRANSET, 0x00fe0000);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x080003fe);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x0a00027c);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x0b000260);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x0c000274);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x0d000264);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x0e000000);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x0f000020);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x1000027e);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x110002fe);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x200f0060);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x00000006);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x40008c0f);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x44000000);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x45080c04);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x46800408);

	VIA_WRITE(VIA_REG_TRANSPACE, (0x52 << 24) |
		  ((vq_start_offs & 0xFF000000) >> 24) |
		  ((vq_end_offs & 0xFF000000) >> 16));
	VIA_WRITE(VIA_REG_TRANSPACE, (0x50 << 24) |
		  (vq_start_offs & 0x00FFFFFF));
	VIA_WRITE(VIA_REG_TRANSPACE, (0x51 << 24) | (vq_end_offs & 0x00FFFFFF));
	VIA_WRITE(VIA_REG_TRANSPACE, (0x53 << 24) | (VIA_VQ_SIZE >> 3));
}

static int via_exec_init(struct drm_device *dev)
{
	struct drm_via_private *dev_priv = via_priv(dev);
	int ret;
	int dummy;

	memset(dev_priv->barriers, 0, sizeof(dev_priv->barriers));

	/*
	 * VRAM buffer for fence sequences.
	 */

	ret = ttm_buffer_object_create(&dev_priv->bdev, PAGE_SIZE,
				       ttm_bo_type_kernel,
				       TTM_PL_FLAG_VRAM |
				       TTM_PL_FLAG_NO_EVICT,
				       0, 0, 0, NULL, &dev_priv->fence_bo);
	if (unlikely(ret))
		return ret;

	ret = ttm_bo_kmap(dev_priv->fence_bo, 0, dev_priv->fence_bo->num_pages,
			  &dev_priv->fence_bmo);
	if (unlikely(ret))
		goto out_err0;

	dev_priv->fence_map =
	    ttm_kmap_obj_virtual(&dev_priv->fence_bmo, &dummy);
	list_del_init(&dev_priv->fence_bo->lru);

	/*
	 * VRAM buffer for the virtual command queue, which corresponds
	 * to engine (fence_class) 2.
	 */

	ret = ttm_buffer_object_create(&dev_priv->bdev, VIA_VQ_SIZE,
				       ttm_bo_type_kernel,
				       TTM_PL_FLAG_VRAM |
				       TTM_PL_FLAG_NO_EVICT,
				       0, 0, 0, NULL, &dev_priv->vq_bo);
	if (unlikely(ret))
		goto out_err1;

	list_del_init(&dev_priv->vq_bo->lru);

	/*
	 * AGP DMA command buffer.
	 */

	ret = ttm_buffer_object_create(&dev_priv->bdev, VIA_AGPC_SIZE,
				       ttm_bo_type_kernel,
				       TTM_PL_FLAG_TT |
				       TTM_PL_FLAG_NO_EVICT,
				       0, 0, 0, NULL, &dev_priv->agpc_bo);
	if (unlikely(ret))
		goto out_err2;

	ret = ttm_bo_kmap(dev_priv->agpc_bo, 0, dev_priv->agpc_bo->num_pages,
			  &dev_priv->agpc_bmo);

	if (unlikely(ret))
		goto out_err3;

	list_del_init(&dev_priv->agpc_bo->lru);

	dev_priv->agpc_map = ttm_kmap_obj_virtual(&dev_priv->agpc_bmo, &dummy);

	via_soft_reset(dev_priv);
	via_3d_engine_init(dev_priv);
	via_2d_engine_init(dev_priv);
	via_agp_engine_init(dev_priv);
	via_vq_enable(dev_priv);
	via_dma_initialize(dev_priv);

	iowrite32(atomic_read(&dev_priv->fence_seq[VIA_ENGINE_CMD]),
		  dev_priv->fence_map + (VIA_FENCE_OFFSET_CMD >> 2));

	return 0;

      out_err3:
	ttm_bo_unref(&dev_priv->agpc_bo);
      out_err2:
	ttm_bo_unref(&dev_priv->vq_bo);
      out_err1:
	dev_priv->fence_map = NULL;
	ttm_bo_kunmap(&dev_priv->fence_bmo);
      out_err0:
	ttm_bo_unref(&dev_priv->fence_bo);

	return ret;
}

void via_exec_takedown(struct drm_via_private *dev_priv)
{
	int i;

	for (i = 0; i < VIA_NUM_BARRIERS; ++i) {
		if (dev_priv->barriers[i])
			ttm_fence_object_unref(&dev_priv->barriers[i]);
	}

	via_dma_takedown(dev_priv);
	via_vq_disable(dev_priv);
	dev_priv->agpc_map = NULL;
	ttm_bo_kunmap(&dev_priv->agpc_bmo);

	BUG_ON(!list_empty(&dev_priv->agpc_bo->lru));
	BUG_ON(dev_priv->agpc_bo->sync_obj != NULL);
	ttm_bo_unref(&dev_priv->agpc_bo);

	BUG_ON(!list_empty(&dev_priv->vq_bo->lru));
	BUG_ON(dev_priv->vq_bo->sync_obj != NULL);
	ttm_bo_unref(&dev_priv->vq_bo);

	/*
	 * Unmap and free the fence sequence buffer object.
	 */

	dev_priv->fence_map = NULL;
	ttm_bo_kunmap(&dev_priv->fence_bmo);

	BUG_ON(!list_empty(&dev_priv->fence_bo->lru));
	BUG_ON(dev_priv->fence_bo->sync_obj != NULL);
	ttm_bo_unref(&dev_priv->fence_bo);
}

/*
 * Probe the RAM controller and PCI resources for VRAM start and size.
 */

static int via_detect_vram(struct drm_device *dev)
{
	struct drm_via_private *dev_priv = via_priv(dev);
	struct pci_dev *fn3 = NULL;
	struct pci_dev *fn0 = NULL;
	struct pci_bus *bus;
	u8 size;
	int ret;

	bus = pci_find_bus(0, 0);
	if (bus == NULL) {
		ret = -EINVAL;
		goto out_err;
	}

	fn0 = pci_get_slot(bus, PCI_DEVFN(0, 0));
	fn3 = pci_get_slot(bus, PCI_DEVFN(0, 3));

	switch (dev->pdev->device) {
	case 0x3122:
	case 0x7205:
		if (fn0 == NULL) {
			ret = -EINVAL;
			goto out_err;
		}
		ret = pci_read_config_byte(fn0, 0xE1, &size);
		if (ret)
			goto out_err;
		dev_priv->vram_size = (1 << ((size & 0x70) >> 4)) << 10;
		break;
	case 0x3118:
	case 0x3344:
	case 0x3108:
		if (fn3 == NULL) {
			ret = -EINVAL;
			goto out_err;
		}
		ret = pci_read_config_byte(fn3, 0xA1, &size);
		if (ret)
			goto out_err;
		dev_priv->vram_size = (1 << ((size & 0x70) >> 4)) << 10;
		break;
	case 0x3230:
	case 0x3343:
	case 0x3371:
	case 0x3157:
		if (fn3 == NULL) {
			ret = -EINVAL;
			goto out_err;
		}
		ret = pci_read_config_byte(fn3, 0xA1, &size);
		if (ret)
			goto out_err;
		dev_priv->vram_size = (1 << ((size & 0x70) >> 4)) << 12;
		break;
	default:
		DRM_ERROR("Unknown device 0x%04x. "
			  "Could not detect VRAM size.\n", dev->pdev->device);
		ret = -EINVAL;
		goto out_err;
	}

	/*
	 * Detect VRAM start.
	 */

	dev_priv->vram_direct = 0;
	if (fn3 != NULL) {
		switch (fn3->device) {
		case 0x3204:
			pci_read_config_byte(fn3, 0x47, &size);
			dev_priv->vram_start = size << 24;
			dev_priv->vram_start -= dev_priv->vram_size * 1024;
			dev_priv->vram_direct = 1;
			break;
		default:
			dev_priv->vram_start = pci_resource_start(dev->pdev, 0);
			break;
		}
	} else {		
		dev_priv->vram_start = pci_resource_start(dev->pdev, 0);
	}

	if (dev_priv->vram_direct)
		DRM_INFO("TTM is mapping Video RAM directly.\n");

	if (fn0)
		pci_dev_put(fn0);
	if (fn3)
		pci_dev_put(fn3);

	DRM_INFO("Detected %llu kiBytes of Video RAM at "
		 "physical address 0x%08llx.\n",
		 (unsigned long long)dev_priv->vram_size,
		 (unsigned long long)dev_priv->vram_start);

	return 0;
      out_err:

	if (fn0)
		pci_dev_put(fn0);
	if (fn3)
		pci_dev_put(fn3);

	DRM_ERROR("Failed reading PCI configuration to detect VRAM size.\n");
	return ret;
}

/*
 * Called at load time to set up the VRAM and TT memory types.
 */

static int via_detect_agp(struct drm_device *dev)
{
	struct drm_via_private *dev_priv = via_priv(dev);
	struct drm_agp_mode mode;
	struct drm_agp_info agp_info;
	int ret;

	ret = drm_agp_acquire(dev);
	if (ret) {
		DRM_ERROR("Failed acquiring AGP device.\n");
		return ret;
	}

	ret = drm_agp_info(dev, &agp_info);
	if (ret) {
		DRM_ERROR("Failed detecting AGP aperture size.\n");
		goto out_err0;
	}

	mode.mode = agp_info.mode & ~VIA_AGP_MODE_MASK;
	if (mode.mode & VIA_AGPV3_MODE)
		mode.mode |= VIA_AGPV3_8X_MODE;
	else
		mode.mode |= VIA_AGP_4X_MODE;

	mode.mode |= VIA_AGP_FW_MODE;
	ret = drm_agp_enable(dev, mode);
	if (ret) {
		DRM_ERROR("Failed to enable the AGP bus.\n");
		goto out_err0;
	}

	DRM_INFO("Detected %lu MiBytes of AGP Aperture at "
		 "physical address 0x%08lx.\n",
		 agp_info.aperture_size / (1024 * 1024),
		 agp_info.aperture_base);

	dev_priv->tt_size = agp_info.aperture_size;
	dev_priv->tt_start = agp_info.aperture_base;

	return 0;

      out_err0:
	drm_agp_release(dev);
	return ret;
}

static int via_ttm_init(struct drm_device *dev)
{
	struct drm_via_private *dev_priv = via_priv(dev);
	int ret;

	ret = via_detect_vram(dev);
	if (ret)
		return ret;

	ret = via_detect_agp(dev);
	if (ret)
		return ret;

	dev_priv->vram_mtrr = drm_mtrr_add(dev_priv->vram_start,
					   dev_priv->vram_size * 1024,
					   DRM_MTRR_WC);

	ret = ttm_bo_device_init(&dev_priv->bdev,
				 dev_priv->mem_global_ref.object,
				 &via_bo_driver, DRM_FILE_PAGE_OFFSET);
	if (ret) {
		DRM_ERROR("Failed initializing buffer object driver.\n");
		goto out_err0;
	}
	ttm_lock_init(&dev_priv->ttm_lock);
	ttm_lock_set_kill(&dev_priv->ttm_lock, true, SIGTERM);

	ret = ttm_bo_init_mm(&dev_priv->bdev, TTM_PL_VRAM,
			     DEV_VRAM_OFFSET >> PAGE_SHIFT,
			     (dev_priv->vram_size * 1024 -
			      DEV_VRAM_OFFSET) >> PAGE_SHIFT);
	if (ret) {
		DRM_ERROR("Failed initializing VRAM heap.\n");
		goto out_err1;
	}

	ret = ttm_bo_init_mm(&dev_priv->bdev, TTM_PL_TT,
			     DEV_AGP_OFFSET >> PAGE_SHIFT,
			     (dev_priv->tt_size -
			      DEV_AGP_OFFSET) >> PAGE_SHIFT);

	if (ret) {
		DRM_ERROR("Failed initializing AGP aperture heap.\n");
		goto out_err2;
	}

	return 0;
      out_err2:
	(void)ttm_bo_clean_mm(&dev_priv->bdev, TTM_PL_VRAM);
      out_err1:
	(void)ttm_bo_device_release(&dev_priv->bdev);
      out_err0:
	drm_mtrr_del(dev_priv->vram_mtrr, dev_priv->vram_start,
		     dev_priv->vram_size * 1024, DRM_MTRR_WC);
	drm_agp_release(dev);
	return ret;
}

/*
 * Called at unload time to take down VRAM and TT memory types.
 */

static void via_ttm_takedown(struct drm_device *dev)
{
	struct drm_via_private *dev_priv = via_priv(dev);

	(void)ttm_bo_clean_mm(&dev_priv->bdev, TTM_PL_VRAM);
	(void)ttm_bo_clean_mm(&dev_priv->bdev, TTM_PL_TT);
	(void)ttm_bo_device_release(&dev_priv->bdev);
	drm_mtrr_del(dev_priv->vram_mtrr, dev_priv->vram_start,
		     dev_priv->vram_size * 1024, DRM_MTRR_WC);
	drm_agp_release(dev);
}

static int via_setup_mmio_map(struct drm_device *dev)
{
	struct drm_via_private *dev_priv = via_priv(dev);
	unsigned long mmio_start;
	unsigned long mmio_len;

	mmio_start = pci_resource_start(dev->pdev, 1);
	mmio_len = pci_resource_len(dev->pdev, 1);
	dev_priv->mmio_map = ioremap_nocache(mmio_start, mmio_len);
	if (dev_priv->mmio_map == NULL) {
		DRM_ERROR("Failed mapping graphics controller registers.\n");
		return -ENOMEM;
	}
	return 0;
}

int via_driver_load(struct drm_device *dev, unsigned long chipset)
{
	struct drm_via_private *dev_priv;
	int ret = 0;
	int i;

	dev_priv =
	    drm_calloc(1, sizeof(struct drm_via_private), DRM_MEM_DRIVER);
	if (dev_priv == NULL)
		return -ENOMEM;

	ttm_pat_init();
	dev->dev_private = (void *)dev_priv;
	dev_priv->dev = dev;
	dev_priv->max_validate_buffers = 1000;
	dev_priv->chipset = chipset;

	mutex_init(&dev_priv->init_mutex);
	mutex_init(&dev_priv->cmdbuf_mutex);
	INIT_LIST_HEAD(&dev_priv->dma_trackers);
	dev_priv->irq_lock = SPIN_LOCK_UNLOCKED;
	rwlock_init(&dev_priv->context_lock);

	ret = via_ttm_global_init(dev_priv);
	if (ret)
		goto out_err0;

	dev_priv->tdev = ttm_object_device_init
	    (dev_priv->mem_global_ref.object, 12);

	if (unlikely(dev_priv->tdev == NULL)) {
		DRM_ERROR("Unable to initialize TTM object management.\n");
		ret = -ENOMEM;
		goto out_err1;
	}

	hrtimer_init(&dev_priv->fence_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	dev_priv->fence_timer.function = via_ttm_fence_timer_func;

	for (i = 0; i < VIA_NUM_ENGINES; ++i) {
		atomic_set(&dev_priv->fence_seq[i], 0);
	}

	ret = via_setup_mmio_map(dev);
	if (ret)
		goto out_err2;

	ret = via_ttm_fence_device_init(dev_priv);
	if (ret)
		goto out_err3;

	ret = via_ttm_init(dev);
	if (ret)
		goto out_err4;

	via_init_dmablit(dev);
	return 0;

      out_err4:
	ttm_fence_device_release(&dev_priv->fdev);
      out_err3:
	iounmap(dev_priv->mmio_map);
      out_err2:
	ttm_object_device_release(&dev_priv->tdev);
      out_err1:
	via_ttm_global_release(dev_priv);
      out_err0:
	drm_free(dev_priv, sizeof(*dev_priv), DRM_MEM_DRIVER);
	return ret;
}

int via_driver_unload(struct drm_device *dev)
{
	struct drm_via_private *dev_priv = via_priv(dev);

	via_ttm_takedown(dev);
	ttm_fence_device_release(&dev_priv->fdev);
	iounmap(dev_priv->mmio_map);
	ttm_object_device_release(&dev_priv->tdev);
	via_ttm_global_release(dev_priv);
	drm_free(dev_priv, sizeof(struct drm_via_private), DRM_MEM_DRIVER);
	ttm_pat_takedown();

	return 0;
}

int via_open(struct inode *inode, struct file *filp)
{
	struct drm_file *file_priv;
	struct drm_via_private *dev_priv;
	struct via_fpriv *via_fp;
	int ret;

	ret = drm_open(inode, filp);
	if (unlikely(ret))
		return ret;

	via_fp = drm_calloc(1, sizeof(*via_fp), DRM_MEM_FILES);

	if (unlikely(via_fp == NULL))
		goto out_err0;

	file_priv = (struct drm_file *)filp->private_data;
	dev_priv = via_priv(file_priv->minor->dev);

	via_fp->tfile = ttm_object_file_init(dev_priv->tdev, 10);
	if (unlikely(via_fp->tfile == NULL))
		goto out_err1;

	file_priv->driver_priv = via_fp;

	if (unlikely(dev_priv->bdev.dev_mapping == NULL))
		dev_priv->bdev.dev_mapping = dev_priv->dev->dev_mapping;

	return 0;

      out_err1:
	drm_free(via_fp, sizeof(*via_fp), DRM_MEM_FILES);
      out_err0:
	(void)drm_release(inode, filp);
	return ret;
}

int via_release(struct inode *inode, struct file *filp)
{
	struct drm_file *file_priv;
	struct via_fpriv *via_fp;

	file_priv = (struct drm_file *)filp->private_data;
	via_fp = via_fpriv(file_priv);
	ttm_object_file_release(&via_fp->tfile);
	drm_free(via_fp, sizeof(*via_fp), DRM_MEM_FILES);
	return drm_release(inode, filp);
}

static int via_firstopen_locked(struct drm_device *dev)
{
	struct drm_via_private *dev_priv = via_priv(dev);
	int ret;

	/*
	 * Allocate a BO for the AGP memory region.
	 */

	ret = ttm_buffer_object_create(&dev_priv->bdev, VIA_AGPBO_SIZE,
				       ttm_bo_type_kernel,
				       TTM_PL_FLAG_TT |
				       TTM_PL_FLAG_NO_EVICT,
				       0, 0, 0, NULL, &dev_priv->agp_bo);
	if (ret)
		return ret;

	list_del_init(&dev_priv->agp_bo->lru);

	ret = ttm_bo_init_mm(&dev_priv->bdev, TTM_PL_PRIV0,
			     0, dev_priv->agp_bo->num_pages);
	if (ret)
		goto out_err1;

	via_irq_init(dev);
	ret = via_exec_init(dev);
	if (ret)
		goto out_err2;

	ttm_lock_set_kill(&dev_priv->ttm_lock, false, SIGTERM);

	return 0;
      out_err2:
	(void)drm_irq_uninstall(dev);
	(void)ttm_bo_clean_mm(&dev_priv->bdev, TTM_PL_PRIV0);
      out_err1:
	ttm_bo_unref(&dev_priv->agp_bo);

	return ret;
}

int via_firstopen(struct drm_device *dev)
{
	struct drm_via_private *dev_priv = via_priv(dev);
	int ret;

	mutex_lock(&dev_priv->init_mutex);
	ret = via_firstopen_locked(dev);

	ret = drm_ht_create(&dev_priv->context_hash, 10);
	if (unlikely(ret != 0))
		goto out_err0;

	mutex_unlock(&dev_priv->init_mutex);

	return ret;
      out_err0:
	via_lastclose(dev);
	return ret;
}

void via_lastclose(struct drm_device *dev)
{
	struct drm_via_private *dev_priv = via_priv(dev);

	if (!dev_priv)
		return;

	mutex_lock(&dev_priv->init_mutex);
	dev_priv->sarea = NULL;
	dev_priv->sarea_priv = NULL;

	ttm_lock_set_kill(&dev_priv->ttm_lock, true, SIGTERM);
	via_ttm_signal_fences(dev_priv);
	(void)ttm_bo_evict_mm(&dev_priv->bdev, TTM_PL_VRAM);
	(void)ttm_bo_evict_mm(&dev_priv->bdev, TTM_PL_TT);
	hrtimer_cancel(&dev_priv->fence_timer);
	if (dev_priv->agp_bo) {
		(void)ttm_bo_clean_mm(&dev_priv->bdev, TTM_PL_PRIV0);

		BUG_ON(dev_priv->agp_bo->sync_obj != NULL);
		BUG_ON(!list_empty(&dev_priv->agp_bo->lru));

		ttm_bo_unref(&dev_priv->agp_bo);
		via_exec_takedown(dev_priv);

		if (unlikely
		    (!drm_mm_clean(&dev_priv->bdev.man[TTM_PL_VRAM].manager)))
			DRM_ERROR("Vram manager was not clean.\n");
		if (unlikely
		    (!drm_mm_clean(&dev_priv->bdev.man[TTM_PL_TT].manager)))
			DRM_ERROR("TT manager was not clean.\n");
	}
	(void)drm_irq_uninstall(dev);
	drm_ht_remove(&dev_priv->context_hash);
	mutex_unlock(&dev_priv->init_mutex);
}

static int via_leavevt(struct drm_device *dev, struct drm_file *file_priv)
{
	struct drm_via_private *dev_priv = via_priv(dev);
	int ret;

	/*
	 * Block all TTM operations.
	 */

	ret = ttm_write_lock(&dev_priv->ttm_lock, 1,
			     via_fpriv(file_priv)->tfile);
	if (ret)
		return ret;

	ttm_lock_set_kill(&dev_priv->ttm_lock, true, SIGTERM);

	/*
	 * Clear and disable the VIA_AGP memory type.
	 */

	mutex_lock(&dev_priv->init_mutex);
	via_ttm_signal_fences(dev_priv);
	hrtimer_cancel(&dev_priv->fence_timer);

	ret = ttm_bo_evict_mm(&dev_priv->bdev, TTM_PL_VRAM);
	if (ret)
		goto out_unlock;

	ret = ttm_bo_evict_mm(&dev_priv->bdev, TTM_PL_TT);
	if (ret)
		goto out_unlock;

	if (dev_priv->agp_bo) {
		(void)ttm_bo_clean_mm(&dev_priv->bdev, TTM_PL_PRIV0);

		BUG_ON(dev_priv->agp_bo->sync_obj != NULL);
		BUG_ON(!list_empty(&dev_priv->agp_bo->lru));

		ttm_bo_unref(&dev_priv->agp_bo);
		via_exec_takedown(dev_priv);
		if (unlikely
		    (!drm_mm_clean(&dev_priv->bdev.man[TTM_PL_PRIV0].manager)))
			DRM_ERROR("AGP manager was not clean.\n");
	}

	if (unlikely(!drm_mm_clean(&dev_priv->bdev.man[TTM_PL_VRAM].manager)))
		DRM_ERROR("Vram manager was not clean.\n");
	if (unlikely(!drm_mm_clean(&dev_priv->bdev.man[TTM_PL_TT].manager)))
		DRM_ERROR("TT manager was not clean.\n");

	ttm_bo_swapout_all(&dev_priv->bdev);

	(void)drm_irq_uninstall(dev);
	mutex_unlock(&dev_priv->init_mutex);
	return 0;
      out_unlock:
	mutex_unlock(&dev_priv->init_mutex);
	ttm_write_unlock(&dev_priv->ttm_lock, via_fpriv(file_priv)->tfile);
	return ret;
}

static int via_entervt(struct drm_device *dev, struct drm_file *file_priv)
{
	struct drm_via_private *dev_priv = via_priv(dev);
	int ret;

	mutex_lock(&dev_priv->init_mutex);
	ret = via_firstopen_locked(dev);
	mutex_unlock(&dev_priv->init_mutex);
	if (ret)
		return ret;

	return ttm_write_unlock(&dev_priv->ttm_lock,
				via_fpriv(file_priv)->tfile);
}

int via_vt_ioctl(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct drm_via_vt *vt = (struct drm_via_vt *)data;

	if (vt->enter)
		return via_entervt(dev, file_priv);
	else
		return via_leavevt(dev, file_priv);
}

static unsigned long via_mm_largest(struct drm_mm *mm)
{
	unsigned long largest_size = 0;
	const struct list_head *free_stack = &mm->fl_entry;
	struct drm_mm_node *entry;

	list_for_each_entry(entry, free_stack, fl_entry) {
		if (entry->size > largest_size)
			largest_size = entry->size;
	}

	return largest_size;
}

int via_getparam_ioctl(struct drm_device *dev, void *data,
		       struct drm_file *file_priv)
{
	struct ttm_mem_type_manager *man;
	struct drm_via_getparam_arg *arg = data;
	struct drm_via_private *dev_priv = via_priv(dev);
	struct ttm_bo_device *bdev = &dev_priv->bdev;

	switch (arg->param) {
	case DRM_VIA_PARAM_VRAM_SIZE:
		man = &bdev->man[TTM_PL_VRAM];
		arg->value = via_mm_largest(&man->manager) << PAGE_SHIFT;
		return 0;
	case DRM_VIA_PARAM_TT_SIZE:
		man = &bdev->man[TTM_PL_TT];
		arg->value = via_mm_largest(&man->manager) << PAGE_SHIFT;
		return 0;
	case DRM_VIA_PARAM_AGP_SIZE:
		man = &bdev->man[TTM_PL_PRIV0];
		arg->value = via_mm_largest(&man->manager) << PAGE_SHIFT;
		return 0;
	case DRM_VIA_PARAM_HAS_IRQ:
		arg->value = dev->irq_enabled;
		return 0;
	case DRM_VIA_PARAM_SAREA_SIZE:
		arg->value = (sizeof(struct via_sarea) + PAGE_SIZE - 1) &
		    ~PAGE_MASK;
		return 0;
	default:
		break;
	}
	return -EINVAL;
}

int via_extension_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv)
{
	union drm_via_extension_arg *arg = data;
	struct drm_via_extension_rep *rep = &arg->rep;

	if (strcmp(arg->extension, "via_ttm_placement_081121") == 0) {
		rep->exists = 1;
		rep->driver_ioctl_offset = DRM_VIA_PLACEMENT_OFFSET;
		rep->driver_sarea_offset = 0;
		rep->major = 1;
		rep->minor = 0;
		rep->pl = 0;
		return 0;
	}
	if (strcmp(arg->extension, "via_ttm_fence_081121") == 0) {
		rep->exists = 1;
		rep->driver_ioctl_offset = DRM_VIA_FENCE_OFFSET;
		rep->driver_sarea_offset = 0;
		rep->major = 1;
		rep->minor = 0;
		rep->pl = 0;
		return 0;
	}
	if (strcmp(arg->extension, "via_ttm_execbuf") == 0) {
		rep->exists = 1;
		rep->driver_ioctl_offset = DRM_VIA_TTM_EXECBUF;
		rep->driver_sarea_offset = 0;
		rep->major = 1;
		rep->minor = 0;
		rep->pl = 0;
		return 0;
	}
	if (strcmp(arg->extension, "via_decoder_futex") == 0) {
		rep->exists = 1;
		rep->driver_ioctl_offset = DRM_VIA_DEC_FUTEX;
		rep->driver_sarea_offset = offsetof(struct via_sarea, sa_xvmc);
		rep->major = 1;
		rep->minor = 0;
		rep->pl = 0;
		return 0;
	}

	rep->exists = 0;
	return 0;
}

int via_resume(struct pci_dev *pdev)
{
	ttm_pat_resume();
	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);
	return pci_enable_device(pdev);
}

int via_suspend(struct pci_dev *pdev, pm_message_t state)
{
	pci_save_state(pdev);
	pci_disable_device(pdev);
	pci_set_power_state(pdev, PCI_D3hot);

	return 0;
}
