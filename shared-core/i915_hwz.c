/*
 Copyright (C) Intel Corp.  2007.  All Rights Reserved.
 Intel funded Tungsten Graphics (http://www.tungstengraphics.com) to
 develop this 3D driver.
 
 Permission is hereby granted, free of charge, to any person obtaining
 a copy of this software and associated documentation files (the
 "Software"), to deal in the Software without restriction, including
 without limitation the rights to use, copy, modify, merge, publish,
 distribute, sublicense, and/or sell copies of the Software, and to
 permit persons to whom the Software is furnished to do so, subject to
 the following conditions:
 
 The above copyright notice and this permission notice (including the
 next paragraph) shall be included in all copies or substantial
 portions of the Software.
 
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 
 **********************************************************************/
 /*
  * Authors:
  *   Michel Dänzer <michel@tungstengraphics.com>
  */

#include "drmP.h"
#include "drm.h"
#include "i915_drm.h"
#include "i915_drv.h"

#define BIN_WIDTH 64
#define BIN_HEIGHT 32
#define BIN_HMASK ~(BIN_HEIGHT - 1)
#define BIN_WMASK ~(BIN_WIDTH - 1)


#define BMP_SIZE PAGE_SIZE
#define BMP_POOL_SIZE ((BMP_SIZE - 32) / 4)

void i915_bmp_free(drm_device_t *dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;

	if (dev_priv->bmp_pool) {
		int i;

		for (i = 0; i < BMP_POOL_SIZE; i++) {
			if (!dev_priv->bmp_pool[i])
				break;

			drm_pci_free(dev, dev_priv->bmp_pool[i]);
		}

		drm_free(dev_priv->bmp_pool, BMP_POOL_SIZE *
			 sizeof(drm_dma_handle_t*), DRM_MEM_DRIVER);
		dev_priv->bmp_pool = NULL;
	}

	if (dev_priv->bmp) {
		drm_pci_free(dev, dev_priv->bmp);
		dev_priv->bmp = NULL;
	}

	I915_WRITE(BMP_BUFFER, 0);

	dev_priv->irq_enable_reg &= ~HWB_OOM_FLAG;
	I915_WRITE(I915REG_INT_ENABLE_R, dev_priv->irq_enable_reg);

	DRM_INFO("BMP freed\n");
}


static int i915_bmp_alloc(drm_device_t *dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	int i;
	u32 *ring;

	dev_priv->bmp = drm_pci_alloc(dev, BMP_SIZE, PAGE_SIZE, 0xffffffff);

	if (!dev_priv->bmp) {
		DRM_ERROR("Failed to allocate BMP ring buffer\n");
		return DRM_ERR(ENOMEM);
	}

	dev_priv->bmp_pool = drm_calloc(1, BMP_POOL_SIZE *
					sizeof(drm_dma_handle_t*),
					DRM_MEM_DRIVER);

	if (!dev_priv->bmp_pool) {
		DRM_ERROR("Failed to allocate BMP pool\n");
		drm_pci_free(dev, dev_priv->bmp);
		dev_priv->bmp = NULL;
		return DRM_ERR(ENOMEM);
	}

	for (i = 0, ring = dev_priv->bmp->vaddr; i < BMP_POOL_SIZE; i++) {
		dev_priv->bmp_pool[i] = drm_pci_alloc(dev, PAGE_SIZE, PAGE_SIZE,
						      0xffffffff);

		if (!dev_priv->bmp_pool[i]) {
			DRM_INFO("Failed to allocate page %d for BMP pool\n",
				 i);
			break;
		}

		ring[i] = dev_priv->bmp_pool[i]->busaddr /*>> PAGE_SHIFT*/;
	}

	I915_WRITE(BMP_PUT, (i / 8) << BMP_OFFSET_SHIFT);
	I915_WRITE(BMP_GET, 0 << BMP_OFFSET_SHIFT);

	dev_priv->irq_enable_reg |= HWB_OOM_FLAG;
	I915_WRITE(I915REG_INT_ENABLE_R, dev_priv->irq_enable_reg);

	I915_WRITE(BMP_BUFFER, dev_priv->bmp->busaddr | BMP_PAGE_SIZE_4K |
		   ((BMP_SIZE / PAGE_SIZE - 1) << BMP_BUFFER_SIZE_SHIFT) |
		   BMP_ENABLE);

	DRM_DEBUG("BMP allocated and initialized\n");

	return 0;
}

#define BPL_ALIGN (16 * 1024)

static int i915_bpl_alloc(drm_device_t *dev,
			  struct drm_i915_driver_file_fields *filp_priv,
			  int num_bins)
{
	int i, bpl_size = (8 * num_bins + PAGE_SIZE - 1) & PAGE_MASK;

	if (num_bins <= 0) {
		DRM_ERROR("Invalid num_bins=%d\n", num_bins);
		return DRM_ERR(EINVAL);
	}

	if (!filp_priv) {
		DRM_ERROR("No driver storage associated with file handle\n");
		return DRM_ERR(EINVAL);
	}

#if !VIRTUAL_BPL
	/* drm_pci_alloc can't handle alignment > size */
	if (bpl_size < BPL_ALIGN)
		bpl_size = BPL_ALIGN;
#endif

	for (i = 0; i < filp_priv->num_bpls; i++) {
		if (filp_priv->bpl[i])
			continue;
#if VIRTUAL_BPL
		if (drm_buffer_object_create(dev, bpl_size, drm_bo_type_kernel,
					     DRM_BO_FLAG_READ | DRM_BO_FLAG_WRITE | 
					     DRM_BO_FLAG_MEM_TT,
					     DRM_BO_HINT_DONT_FENCE,
					     BPL_ALIGN / PAGE_SIZE, 0,
					     &filp_priv->bpl[i]))
			filp_priv->bpl[i] = NULL;
#else
		filp_priv->bpl[i] = drm_pci_alloc(dev, bpl_size, BPL_ALIGN,
						  0xffffffff);
#endif
		if (!filp_priv->bpl[i]) {
			DRM_ERROR("Failed to allocate BPL %d\n", i);
			return DRM_ERR(ENOMEM);
		}
#if VIRTUAL_BPL
		if (filp_priv->bpl[i]->offset & (0x7 << 29)) {
			DRM_ERROR("BPL %d bus address 0x%lx high bits not 0\n",
				  i, filp_priv->bpl[i]->offset);
			mutex_lock(&dev->struct_mutex);
			drm_bo_usage_deref_locked(filp_priv->bpl[i]);
			mutex_unlock(&dev->struct_mutex);
			filp_priv->bpl[i] = NULL;
			return DRM_ERR(ENOMEM);
		}

		DRM_INFO("BPL %d offset=0x%lx\n", i, filp_priv->bpl[i]->offset);
#endif
	}

	DRM_DEBUG("Allocated %d BPLs of %d bytes\n", filp_priv->num_bpls,
		  bpl_size);

	return 0;
}

static void i915_bpl_free(drm_device_t *dev,
			  struct drm_i915_driver_file_fields *filp_priv)
{
	int i;

	if (!filp_priv)
		return;

	for (i = 0; i < 3; i++) {
		if (!filp_priv->bpl[i])
			return;

#if VIRTUAL_BPL
		mutex_lock(&dev->struct_mutex);
		drm_bo_usage_deref_locked(filp_priv->bpl[i]);
		mutex_unlock(&dev->struct_mutex);
#else
		drm_pci_free(dev, filp_priv->bpl[i]);
#endif

		filp_priv->bpl[i] = NULL;
	}
}

#define DEBUG_HWZ 0

static void i915_bpl_print(drm_device_t *dev,
			   struct drm_i915_driver_file_fields *filp_priv, int i)
{
#if DEBUG_HWZ
	u32 *bpl_vaddr;
	int bpl_row;
#if VIRTUAL_BPL
	int ret;
#endif

	if (!filp_priv || !filp_priv->bpl[i])
		return;

#if VIRTUAL_BPL
	ret = drm_mem_reg_ioremap(dev, &filp_priv->bpl[i]->mem,
				  (void*)&bpl_vaddr);

	if (ret) {
		DRM_ERROR("Failed to map BPL %d\n", i);
		return;
	}
#else
	bpl_vaddr = filp_priv->bpl[i]->vaddr;
#endif

	DRM_DEBUG("BPL %d contents:\n", i);

	for (bpl_row = 0; bpl_row < filp_priv->bin_rows; bpl_row++) {
		int bpl_col;

		DRM_DEBUG("Row %d:", bpl_row);

		for (bpl_col = 0; bpl_col < filp_priv->bin_cols; bpl_col++) {
			u32 *bpl = (u32*)bpl_vaddr +
				2 * (bpl_row * filp_priv->bin_cols + bpl_col);
			DRM_DEBUG(" %8p(0x%08x, 0x%08x)", bpl, bpl[0], bpl[1]);
		}

		DRM_DEBUG("\n");
	}
#if VIRTUAL_BPL
	drm_mem_reg_iounmap(dev, &filp_priv->bpl[i]->mem, bpl_vaddr);
#endif

#endif /* DEBUG_HWZ */
}

static int i915_hwb_idle(drm_device_t *dev,
			 struct drm_i915_driver_file_fields *filp_priv,
			 unsigned bpl_num)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
#if DEBUG_HWZ
	int i, firsttime = 1;
#endif
	int ret = 0;

	if (i915_wait_ring(dev_priv, &dev_priv->hwb_ring,
			   dev_priv->hwb_ring.Size - 8,  __FUNCTION__)) {
		DRM_ERROR("Timeout waiting for HWB ring to go idle"
			  ", PRB head: %x tail: %x/%x HWB head: %x tail: %x/%x\n",
			  I915_READ(LP_RING + RING_HEAD) & HEAD_ADDR,
			  I915_READ(LP_RING + RING_TAIL) & HEAD_ADDR,
			  dev_priv->ring.tail,
			  I915_READ(HWB_RING + RING_HEAD) & HEAD_ADDR,
			  I915_READ(HWB_RING + RING_TAIL) & HEAD_ADDR,
			  dev_priv->hwb_ring.tail);
		DRM_ERROR("ESR: 0x%x DMA_FADD_S: 0x%x IPEIR: 0x%x SCPD0: 0x%x "
			  "IIR: 0x%x\n", I915_READ(ESR), I915_READ(DMA_FADD_S),
			  I915_READ(IPEIR), I915_READ(SCPD0),
			  I915_READ(I915REG_INT_IDENTITY_R));
		DRM_ERROR("BCPD: 0x%x BMCD: 0x%x BDCD: 0x%x BPCD: 0x%x\n"
			  "BINSCENE: 0x%x BINSKPD: 0x%x HWBSKPD: 0x%x\n", I915_READ(BCPD),
			  I915_READ(BMCD), I915_READ(BDCD), I915_READ(BPCD),
			  I915_READ(BINSCENE), I915_READ(BINSKPD), I915_READ(HWBSKPD));

		ret = DRM_ERR(EBUSY);
	}

#if DEBUG_HWZ
	if (!filp_priv)
		return ret;

	if (ret)
		bpl_num = (bpl_num - 1) % filp_priv->num_bpls;

	i915_bpl_print(dev, filp_priv, bpl_num);

	for (i = 0; i < filp_priv->num_bins; i++) {
		u32 *bin;
		int k;

		if (!filp_priv->bins[bpl_num] || !filp_priv->bins[bpl_num][i])
			continue;

		bin = filp_priv->bins[bpl_num][i]->vaddr;

		for (k = 0; k < 1024; k++) {
			if (bin[k]) {
				int j;

				DRM_DEBUG("BPL %d bin %d busaddr=0x%x non-empty:\n",
					  bpl_num, i,
					  filp_priv->bins[bpl_num][i]->busaddr);

				if (!firsttime)
					break;

				for (j = 0; j < 128; j++) {
					u32 *data = filp_priv->bins[bpl_num][i]->vaddr +
						j * 8 * 4;

					if (data[0] || data[1] || data[2] || data[3] ||
					    data[4] || data[5] || data[6] || data[7])
						DRM_DEBUG("%p: %8x %8x %8x %8x %8x %8x %8x %8x\n",
							  data, data[0], data[1], data[2], data[3],
							  data[4], data[5], data[6], data[7]);
				}

				firsttime = 0;

				break;
			}
		}
	}
#endif

	return ret;
}

static void i915_bin_free(drm_device_t *dev,
			  struct drm_i915_driver_file_fields *filp_priv)
{
	int i, j;

	if (!filp_priv)
		return;

	i915_hwb_idle(dev, filp_priv, 0);

	for (i = 0; i < filp_priv->num_bins; i++) {
		if (!filp_priv->bin_rects || !filp_priv->bin_rects)
			goto free_arrays;

		for (j = 0; j < filp_priv->bin_nrects[i]; j++) {
			drm_free(filp_priv->bin_rects[i],
				 filp_priv->bin_nrects[i] *
				 sizeof(drm_clip_rect_t),
				 DRM_MEM_DRIVER);
		}

free_arrays:
		drm_free(filp_priv->bin_rects, filp_priv->num_bins *
			 sizeof(drm_clip_rect_t*), DRM_MEM_DRIVER);
		filp_priv->bin_rects = NULL;

		drm_free(filp_priv->bin_nrects, filp_priv->num_bins *
			 sizeof(unsigned int), DRM_MEM_DRIVER);
		filp_priv->bin_nrects = NULL;
	}

	for (i = 0; i < 3; i++) {
		if (!filp_priv->bins[i])
			return;

		for (j = 0; j < filp_priv->num_bins; j++)
			drm_pci_free(dev, filp_priv->bins[i][j]);

		drm_free(filp_priv->bins[i], filp_priv->num_bins *
			 sizeof(drm_dma_handle_t*), DRM_MEM_DRIVER);
		filp_priv->bins[i] = NULL;
	}
}

static int i915_bin_alloc(drm_device_t *dev,
			  struct drm_i915_driver_file_fields *filp_priv,
			  drm_clip_rect_t *cliprects,
			  unsigned int num_cliprects)
{
	int i, j;

	if (!filp_priv) {
		DRM_ERROR("No driver storage associated with file handle\n");
		return DRM_ERR(EINVAL);
	}

	filp_priv->bin_rects = drm_calloc(1, filp_priv->num_bins *
					  sizeof(drm_clip_rect_t*),
					  DRM_MEM_DRIVER);

	if (!filp_priv->bin_rects) {
		DRM_ERROR("Failed to allocate bin rects pool\n");
		return DRM_ERR(ENOMEM);
	}

	filp_priv->bin_nrects = drm_calloc(1, filp_priv->num_bins *
					   sizeof(unsigned int),
					   DRM_MEM_DRIVER);

	if (!filp_priv->bin_nrects) {
		DRM_ERROR("Failed to allocate bin nrects array\n");
		return DRM_ERR(ENOMEM);
	}

	filp_priv->num_rects = 0;

	for (i = 0; i < filp_priv->num_bins; i++) {
		unsigned short bin_row = i / filp_priv->bin_cols;
		unsigned short bin_col = i % filp_priv->bin_cols;
		unsigned short bin_y1 = max(filp_priv->bin_y1, (unsigned short)
					    ((filp_priv->bin_y1 + bin_row *
					      BIN_HEIGHT) & BIN_HMASK));
		unsigned short bin_x1 = max(filp_priv->bin_x1, (unsigned short)
					    ((filp_priv->bin_x1 + bin_col *
					      BIN_WIDTH) & BIN_WMASK));
		unsigned short bin_y2 = min(filp_priv->bin_y2 - 1,
					    ((bin_y1 + BIN_HEIGHT) & BIN_HMASK)
					    - 1);
		unsigned short bin_x2 = min(filp_priv->bin_x2 - 1,
					    ((bin_x1 + BIN_WIDTH) & BIN_WMASK)
					    - 1);

		for (j = 0; j < num_cliprects; j++) {
			unsigned short x1 = max(bin_x1, cliprects[j].x1);
			unsigned short x2 = min(bin_x2, cliprects[j].x2);
			unsigned short y1 = max(bin_y1, cliprects[j].y1);
			unsigned short y2 = min(bin_y2, cliprects[j].y2);
			drm_clip_rect_t *rect;

			if (x1 >= x2 || y1 >= y2)
				continue;

			filp_priv->bin_rects[i] =
				drm_realloc(filp_priv->bin_rects[i],
					    filp_priv->bin_nrects[i] *
					    sizeof(drm_clip_rect_t),
					    (filp_priv->bin_nrects[i] + 1) *
					    sizeof(drm_clip_rect_t),
					    DRM_MEM_DRIVER);

			rect = &filp_priv->bin_rects[i]
					[filp_priv->bin_nrects[i]++];

			rect->x1 = x1;
			rect->x2 = x2;
			rect->y1 = y1;
			rect->y2 = y2;

			DRM_DEBUG("Bin %d cliprect %d: (%d, %d) - (%d, %d)\n",
				  i, filp_priv->bin_nrects[i], x1, y1, x2, y2);

			filp_priv->num_rects++;
		}
	}

	for (i = 0; i < filp_priv->num_bpls; i++) {
		filp_priv->bins[i] = drm_calloc(1, filp_priv->num_bins *
						sizeof(drm_dma_handle_t*),
						DRM_MEM_DRIVER);

		if (!filp_priv->bins[i]) {
			DRM_ERROR("Failed to allocate bin pool %d\n", i);
			return DRM_ERR(ENOMEM);
		}

		for (j = 0; j < filp_priv->num_bins; j++) {
			filp_priv->bins[i][j] = drm_pci_alloc(dev, PAGE_SIZE,
							      PAGE_SIZE,
							      0xffffffff);

			if (!filp_priv->bins[i][j]) {
				DRM_ERROR("Failed to allocate page for bin %d "
					  "of buffer %d\n", j, i);
				return DRM_ERR(ENOMEM);
			}
		}
	}

	DRM_INFO("Allocated %d times %d bins and %d cliprects\n",
		 filp_priv->num_bpls, filp_priv->num_bins, filp_priv->num_rects);

	return 0;
}

static int i915_hwz_alloc(drm_device_t *dev,
			  struct drm_i915_driver_file_fields *filp_priv,
			  struct drm_i915_hwz_alloc *alloc)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	unsigned short x1 = dev_priv->sarea_priv->width - 1, x2 = 0;
	unsigned short y1 = dev_priv->sarea_priv->height - 1, y2 = 0;
	int bin_rows, bin_cols;
	drm_clip_rect_t __user *cliprects;
	int i, ret;

	if (!dev_priv->bmp) {
		DRM_DEBUG("HWZ not initialized\n");
		return DRM_ERR(EINVAL);
	}

	if (alloc->num_buffers > 3) {
		DRM_ERROR("Only up to 3 buffers allowed\n");
		return DRM_ERR(EINVAL);
	}

	if (!filp_priv) {
		DRM_ERROR("No driver storage associated with file handle\n");
		return DRM_ERR(EINVAL);
	}

	cliprects = drm_alloc(alloc->num_cliprects * sizeof(drm_clip_rect_t),
			      DRM_MEM_DRIVER);

	if (!cliprects) {
		DRM_ERROR("Failed to allocate memory to hold %u cliprects\n",
			  alloc->num_cliprects);
		return DRM_ERR(ENOMEM);
	}

	if (DRM_COPY_FROM_USER(cliprects,
			       (void*)(unsigned long)alloc->cliprects,
			       alloc->num_cliprects * sizeof(drm_clip_rect_t))) {
		DRM_ERROR("DRM_COPY_TO_USER failed for %u cliprects\n",
			  alloc->num_cliprects);
		return DRM_ERR(EFAULT);
	}

	for (i = 0; i < alloc->num_cliprects; i++) {
		x1 = min(x1, cliprects[i].x1);
		x2 = max(x2, cliprects[i].x2);
		y1 = min(y1, cliprects[i].y1);
		y2 = max(y2, cliprects[i].y2);
	}

	x2 = min(x2, (unsigned short)(dev_priv->sarea_priv->width - 1));
	if (y2 >= dev_priv->sarea_priv->height)
		y2 = dev_priv->sarea_priv->height - 1;

	bin_rows = (((y2 + BIN_HEIGHT - 1) & BIN_HMASK) -
		    (y1 & BIN_HMASK)) / BIN_HEIGHT;
	bin_cols = (((x2 + BIN_WIDTH - 1) & BIN_WMASK) -
		    (x1 & BIN_WMASK)) / BIN_WIDTH;

	if (bin_cols <= 0 || bin_rows <= 0) {
		DRM_DEBUG("bin_cols=%d bin_rows=%d => nothing to allocate\n",
			  bin_cols, bin_rows);
		return DRM_ERR(EINVAL);
	}

	if (filp_priv->num_bpls != alloc->num_buffers ||
	    filp_priv->bin_rows != bin_rows ||
	    filp_priv->bin_cols != bin_cols) {
		i915_bpl_free(dev, filp_priv);
	}

	filp_priv->bin_x1 = x1;
	filp_priv->bin_x2 = x2;
	filp_priv->bin_cols = bin_cols;
	filp_priv->bin_y1 = y1;
	filp_priv->bin_y2 = y2;
	filp_priv->bin_rows = bin_rows;
	filp_priv->num_bpls = alloc->num_buffers;

	i915_bin_free(dev, filp_priv);

	filp_priv->num_bins = bin_cols * bin_rows;

	ret = i915_bin_alloc(dev, filp_priv, cliprects, alloc->num_cliprects);

	drm_free(cliprects, alloc->num_cliprects * sizeof(drm_clip_rect_t),
		 DRM_MEM_DRIVER);

	if (ret) {
		DRM_ERROR("Failed to allocate bins\n");
		i915_bpl_free(dev, filp_priv);
		return ret;
	}

	ret = i915_bpl_alloc(dev, filp_priv, bin_cols * ((bin_rows + 3) & ~3));

	if (ret) {
		DRM_ERROR("Failed to allocate BPLs\n");
		return ret;
	}

	return 0;
}

int i915_hwz_free(drm_device_t *dev, drm_file_t *filp_priv)
{
	struct drm_i915_driver_file_fields *filp_i915priv;

	if (!filp_priv || !filp_priv->driver_priv)
		return 0;

	filp_i915priv = filp_priv->driver_priv;

	i915_bin_free(dev, filp_i915priv);
	i915_bpl_free(dev, filp_i915priv);

	return 0;
}

static int i915_bin_init(drm_device_t *dev,
			 struct drm_i915_driver_file_fields *filp_priv, int i)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	u32 *bpl_vaddr;
	int bpl_row;
	drm_dma_handle_t **bins = filp_priv->bins[i];
#if VIRTUAL_BPL
	int ret;
#endif

	if (!bins) {
		DRM_ERROR("Bins not allocated\n");
		return DRM_ERR(EINVAL);
	}

	if (!filp_priv) {
		DRM_ERROR("No driver storage associated with file handle\n");
		return DRM_ERR(EINVAL);
	}

#if VIRTUAL_BPL
	ret = drm_mem_reg_ioremap(dev, &filp_priv->bpl[i]->mem,
				  (void*)&bpl_vaddr);

	if (ret) {
		DRM_ERROR("Failed to map BPL %d\n", i);
		return ret;
	}
#else
	bpl_vaddr = filp_priv->bpl[i]->vaddr;
#endif

	for (bpl_row = 0; bpl_row < filp_priv->bin_rows; bpl_row += 4) {
		int bpl_col;

		for (bpl_col = 0; bpl_col < filp_priv->bin_cols; bpl_col++) {
			u32 *bpl = (u32*)bpl_vaddr +
				2 * (bpl_row * filp_priv->bin_cols + 4 * bpl_col);
			int j, num_bpls = filp_priv->bin_rows - bpl_row;

			if (num_bpls > 4)
				num_bpls = 4;

			DRM_DEBUG("bpl_row=%d bpl_col=%d vaddr=%p => bpl=%p num_bpls = %d\n",
				  bpl_row, bpl_col, bpl_vaddr, bpl, num_bpls);

			for (j = 0; j < num_bpls; j++) {
				unsigned idx = (bpl_row + j) *
					filp_priv->bin_cols + bpl_col;
				drm_dma_handle_t *bin = bins[idx];

				DRM_DEBUG("j=%d => idx=%u bpl=%p busaddr=0x%x\n",
					  j, idx, bpl, bin->busaddr);

				*bpl++ = bin->busaddr;
				*bpl++ = 1 << 2 | 1 << 0;
			}
		}
	}

#if VIRTUAL_BPL
	drm_mem_reg_iounmap(dev, &filp_priv->bpl[i]->mem, bpl_vaddr);
#else
	flush_agp_cache();
#endif

	I915_WRITE(GFX_FLSH_CNTL, 1);

	i915_bpl_print(dev, filp_priv, i);

	DRM_DEBUG("BPL %d initialized for %d bins\n", i, filp_priv->bin_rows *
		  filp_priv->bin_cols);

	return 0;
}

static int i915_hwz_render(drm_device_t *dev,
			  struct drm_i915_driver_file_fields *filp_priv,
			   struct drm_i915_hwz_render *render)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	int ret, i;
	int static_state_off = render->static_state_offset -
		virt_to_phys((void*)dev_priv->priv1_addr);
	RING_LOCALS;

	if (static_state_off < 0 || render->static_state_size <= 0 ||
	    static_state_off + 4 * render->static_state_size >
	    ((1 << dev_priv->priv1_order) * PAGE_SIZE)) {
	  	DRM_ERROR("Invalid static indirect state\n");
		return DRM_ERR(EINVAL);
	}

	if (!filp_priv) {
		DRM_ERROR("No driver storage associated with file handle\n");
		return DRM_ERR(EINVAL);
	}

	DRM_DEBUG("bpl_num = %d, batch_start = 0x%x\n", render->bpl_num,
		  render->batch_start);

	if (dev_priv->hwb_ring.tail != (I915_READ(HWB_RING + RING_TAIL)
					& TAIL_ADDR)) {
		DRM_INFO("Refreshing contexts of HWZ ring buffers\n");
		i915_kernel_lost_context(dev_priv, &dev_priv->hwb_ring);
		i915_kernel_lost_context(dev_priv, &dev_priv->hwz_ring);
	}

	if (i915_hwb_idle(dev, filp_priv, render->bpl_num)) {
		return DRM_ERR(EBUSY);
	}

	ret = i915_bin_init(dev, filp_priv, render->bpl_num);

	if (ret) {
		DRM_ERROR("Failed to initialize  BPL %d\n", render->bpl_num);
		return ret;
	}

	/* Write the HWB command stream */
	I915_WRITE(BINSCENE, (filp_priv->bin_rows - 1) << 16 |
		   (filp_priv->bin_cols - 1) << 10 | BS_MASK);
	I915_WRITE(BINSKPD, (VIRTUAL_BPL<<7) | (1<<(7+16)));

#if VIRTUAL_BPL
	ret = drm_buffer_object_validate(filp_priv->bpl[render->bpl_num], 0, 0);

	if (ret) {
		DRM_ERROR("Failed to validate BPL %i\n", render->bpl_num);
		return ret;
	}

	DRM_INFO("BPL %d validated to offset 0x%lx\n", render->bpl_num,
		 filp_priv->bpl[render->bpl_num]->offset);

	I915_WRITE(BINCTL, filp_priv->bpl[render->bpl_num]->offset | BC_MASK);
#else
	I915_WRITE(BINCTL, filp_priv->bpl[render->bpl_num]->busaddr | BC_MASK);
#endif

	BEGIN_RING(&dev_priv->hwb_ring, 16);

	OUT_RING(CMD_OP_BIN_CONTROL);
	OUT_RING(0x5 << 4 | 0x6);
	OUT_RING((filp_priv->bin_y1 & BIN_HMASK) << 16 |
		 (filp_priv->bin_x1 & BIN_WMASK));
	OUT_RING((((filp_priv->bin_y2 + BIN_HEIGHT - 1) & BIN_HMASK) - 1) << 16 |
		 (((filp_priv->bin_x2 + BIN_WIDTH - 1) & BIN_WMASK) - 1));
	OUT_RING(filp_priv->bin_y1 << 16 | filp_priv->bin_x1);
	OUT_RING((filp_priv->bin_y2 - 1) << 16 | (filp_priv->bin_x2 - 1));

	OUT_RING(GFX_OP_DRAWRECT_INFO);
	OUT_RING(render->DR1);
	OUT_RING((filp_priv->bin_y1 & BIN_HMASK) << 16 |
		 (filp_priv->bin_x1 & BIN_WMASK));
	OUT_RING((((filp_priv->bin_y2 + BIN_HEIGHT - 1) & BIN_HMASK) - 1) << 16 |
		 (((filp_priv->bin_x2 + BIN_WIDTH - 1) & BIN_WMASK) - 1));
	OUT_RING(render->DR4);

	OUT_RING(GFX_OP_DESTBUFFER_VARS);
	OUT_RING((0x8<<20) | (0x8<<16));

	OUT_RING(GFX_OP_RASTER_RULES | (1<<5) | (2<<3));

	OUT_RING(MI_BATCH_BUFFER_START | (2 << 6));
	OUT_RING(render->batch_start | MI_BATCH_NON_SECURE);

	ADVANCE_RING();

#if DEBUG_HWZ
	i915_hwb_idle(dev, filp_priv, render->bpl_num);
#endif

	BEGIN_RING(&dev_priv->hwb_ring, 2);
	OUT_RING(CMD_MI_FLUSH | MI_END_SCENE | MI_SCENE_COUNT |
		 MI_NO_WRITE_FLUSH);
	OUT_RING(0);
	ADVANCE_RING();

	/* Prepare the Scene Render List */
	DRM_DEBUG("Emitting %d DWORDs of static indirect state\n",
		  render->static_state_size);

	BEGIN_RING(&dev_priv->ring, (7 * filp_priv->num_rects + 20 +
				     (render->wait_flips ? 2 : 0) + 1) & ~1);

	OUT_RING(GFX_OP_LOAD_INDIRECT | (0x3f<<8) | (0<<14) | 10);
	OUT_RING(render->static_state_offset | (1<<1) | (1<<0));
	OUT_RING(render->static_state_size - 1);
	OUT_RING(0);
	OUT_RING(0);
	OUT_RING(0);
	OUT_RING(0);
	OUT_RING(0);
	OUT_RING(0);
	OUT_RING(0);
	OUT_RING(0);
	OUT_RING(0);
	OUT_RING(CMD_MI_FLUSH /*| MI_NO_WRITE_FLUSH*/);
	OUT_RING(CMD_MI_LOAD_REGISTER_IMM);
	OUT_RING(Cache_Mode_0);
	OUT_RING(0x221 << 16 | 0x201);

	if (render->wait_flips) {
		OUT_RING(render->wait_flips & 0x1 ?
			 (MI_WAIT_FOR_EVENT | MI_WAIT_FOR_PLANE_A_FLIP) : 0);
		OUT_RING(render->wait_flips & 0x2 ?
			 (MI_WAIT_FOR_EVENT | MI_WAIT_FOR_PLANE_B_FLIP) : 0);
	}

	for (i = 0; i < filp_priv->num_bins; i++) {
		int j;

		for (j = 0; j < filp_priv->bin_nrects[i]; j++) {
			drm_clip_rect_t *rect = &filp_priv->bin_rects[i][j];

			OUT_RING(GFX_OP_DRAWRECT_INFO);
			OUT_RING(render->DR1);
			OUT_RING(rect->y1 << 16 | rect->x1);
			OUT_RING(rect->y2 << 16 | rect->x2);
			OUT_RING(render->DR4);
			OUT_RING(MI_BATCH_BUFFER_START);
			OUT_RING(filp_priv->bins[render->bpl_num][i]->busaddr);
		}
	}

	OUT_RING(CMD_MI_FLUSH | MI_END_SCENE | MI_SCENE_COUNT);
	OUT_RING(CMD_MI_LOAD_REGISTER_IMM);
	OUT_RING(Cache_Mode_0);
	OUT_RING(0x221 << 16 | 0x20);

	if (filp_priv->num_rects & 0x1)
		OUT_RING(0);

	i915_hwb_idle(dev, filp_priv, render->bpl_num);

	ADVANCE_RING();

	return ret;
}

static int i915_hwz_init(drm_device_t *dev, struct drm_i915_hwz_init *init)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	int ret;
	RING_LOCALS;

	if (!dev_priv) {
		DRM_ERROR("called without initialization\n");
		return DRM_ERR(EINVAL);
	}

	if (dev_priv->bmp) {
		DRM_DEBUG("Already initialized\n");
		return DRM_ERR(EBUSY);
	}

	ret = i915_init_priv1(dev);

	if (ret) {
		DRM_ERROR("Failed to initialize PRIV1 memory type\n");
		return ret;
	}

	if (i915_bmp_alloc(dev)) {
		DRM_ERROR("Failed to allocate BMP\n");
		return DRM_ERR(ENOMEM);
	}

	if (i915_init_ring(dev, &dev_priv->hwb_ring, init->hwb_start,
			   init->hwb_end, init->hwb_size, HWB_RING)) {
		DRM_ERROR("Failed to initialize HWB ring buffer\n");
		return DRM_ERR(ENOMEM);
	}

	if (i915_init_ring(dev, &dev_priv->hwz_ring, init->hwz_start,
			   init->hwz_end, init->hwz_size, HP_RING)) {
		DRM_ERROR("Failed to initialize HWZ ring buffer\n");
		return DRM_ERR(ENOMEM);
	}

	DRM_DEBUG("Refreshing contexts of HWZ ring buffers\n");
	i915_kernel_lost_context(dev_priv, &dev_priv->hwb_ring);
	i915_kernel_lost_context(dev_priv, &dev_priv->hwz_ring);

	I915_WRITE(BINSCENE, BS_MASK | BS_OP_LOAD);

	BEGIN_RING(&dev_priv->hwb_ring, 2);
	OUT_RING(CMD_MI_FLUSH);
	OUT_RING(0);
	ADVANCE_RING();

	DRM_INFO("HWZ initialized\n");

	return 0;
}

int i915_hwz(DRM_IOCTL_ARGS)
{
	DRM_DEVICE;
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_file_t *filp_priv;
	struct drm_i915_driver_file_fields *filp_i915priv;
	drm_i915_hwz_t hwz;

	if (!dev_priv) {
		DRM_ERROR("called with no initialization\n");
		return DRM_ERR(EINVAL);
	}

	if (dev_priv->hwb_oom) {
		DRM_ERROR("HWB out of memory\n");
		return DRM_ERR(ENOMEM);
	}

	DRM_COPY_FROM_USER_IOCTL(hwz, (drm_i915_hwz_t __user *) data,
				 sizeof(hwz));

	if (hwz.op == DRM_I915_HWZ_INIT) {
		if (!priv->master) {
			DRM_ERROR("Only master may initialize HWZ\n");
			return DRM_ERR(EINVAL);
		}

		return i915_hwz_init(dev, &hwz.arg.init);
	}

	DRM_GET_PRIV_WITH_RETURN(filp_priv, filp);

	if (hwz.op == DRM_I915_HWZ_FREE)
		return i915_hwz_free(dev, filp_priv);

	filp_i915priv = filp_priv->driver_priv;

	switch (hwz.op) {
	case DRM_I915_HWZ_RENDER:
		LOCK_TEST_WITH_RETURN(dev, filp);
		return i915_hwz_render(dev, filp_i915priv, &hwz.arg.render);
	case DRM_I915_HWZ_ALLOC:
		return i915_hwz_alloc(dev, filp_i915priv, &hwz.arg.alloc);
	default:
		DRM_ERROR("Invalid op 0x%x\n", hwz.op);
		return DRM_ERR(EINVAL);
	}
}
