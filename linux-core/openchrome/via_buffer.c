/**************************************************************************
 *
 * Copyright (c) 2007 Tungsten Graphics, Inc., Cedar Park, TX., USA,
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
 * Authors: Thomas Hellström <thomas-at-tungstengraphics-dot-com>
 */

#include "drmP.h"
#include "ochr_drm.h"
#include "via_drv.h"
#include "ttm/ttm_bo_driver.h"
#include "ttm/ttm_placement_common.h"
#include "ttm/ttm_execbuf_util.h"

struct ttm_backend *via_create_ttm_backend_entry(struct ttm_bo_device *bdev)
{
	struct drm_via_private *dev_priv =
	    container_of(bdev, struct drm_via_private, bdev);

	return ttm_agp_backend_init(bdev, dev_priv->dev->agp->bridge);
}

int via_invalidate_caches(struct ttm_bo_device *bdev, uint32_t flags)
{
	/*
	 * FIXME: Invalidate texture caches here.
	 */

	return 0;
}

int via_init_mem_type(struct ttm_bo_device *bdev, uint32_t type,
		      struct ttm_mem_type_manager *man)
{
	struct drm_via_private *dev_priv =
	    container_of(bdev, struct drm_via_private, bdev);

	switch (type) {
	case TTM_PL_SYSTEM:
		/* System memory */

		man->flags = TTM_MEMTYPE_FLAG_MAPPABLE;
		man->available_caching = TTM_PL_MASK_CACHING;
		man->default_caching = TTM_PL_FLAG_CACHED;
		break;

	case TTM_PL_TT:
		man->gpu_offset = dev_priv->tt_start;
		man->io_offset = dev_priv->tt_start;
		man->io_size = dev_priv->tt_size;
		man->io_addr = NULL;
		man->flags = TTM_MEMTYPE_FLAG_NEEDS_IOREMAP |
		    TTM_MEMTYPE_FLAG_MAPPABLE;
		man->available_caching = TTM_PL_FLAG_UNCACHED | TTM_PL_FLAG_WC;
		man->default_caching = TTM_PL_FLAG_WC;
		break;

	case TTM_PL_VRAM:
		/* "On-card" video ram */
		man->gpu_offset = 0;
		man->io_offset = dev_priv->vram_start;
		man->io_size = dev_priv->vram_size * 1024;
		man->flags = TTM_MEMTYPE_FLAG_FIXED |
		    TTM_MEMTYPE_FLAG_NEEDS_IOREMAP | TTM_MEMTYPE_FLAG_MAPPABLE;
		man->io_addr = NULL;
		man->available_caching = TTM_PL_FLAG_UNCACHED | TTM_PL_FLAG_WC;
		man->default_caching = TTM_PL_FLAG_WC;
		break;

	case TTM_PL_PRIV0:
		man->gpu_offset = dev_priv->agp_bo->offset;
		man->io_offset = dev_priv->agp_bo->offset;
		man->io_size = (dev_priv->agp_bo->num_pages << PAGE_SHIFT);
		man->io_addr = NULL;
		man->flags = TTM_MEMTYPE_FLAG_FIXED |
		    TTM_MEMTYPE_FLAG_NEEDS_IOREMAP | TTM_MEMTYPE_FLAG_MAPPABLE;
		man->available_caching = TTM_PL_FLAG_UNCACHED | TTM_PL_FLAG_WC;
		man->default_caching = TTM_PL_FLAG_WC;
		break;

	default:
		DRM_ERROR("Unsupported memory type %u\n", (unsigned)type);
		return -EINVAL;
	}
	return 0;
}

uint32_t via_evict_flags(struct ttm_buffer_object * bo)
{
	uint32_t cur_placement = bo->mem.placement & ~TTM_PL_MASK_MEMTYPE;

	switch (bo->mem.mem_type) {
		/*
		 * Evict pre-bound AGP to VRAM, since
		 * that's the only fastpath we have.
		 * That is, when that fast-path is implemented.
		 */
	case TTM_PL_PRIV0:
		return cur_placement | TTM_PL_FLAG_SYSTEM | TTM_PL_FLAG_WC;
	case TTM_PL_TT:
		return cur_placement | TTM_PL_FLAG_SYSTEM;
	default:
		return (cur_placement & ~TTM_PL_MASK_CACHING) |
			TTM_PL_FLAG_SYSTEM |
			TTM_PL_FLAG_CACHED;
	}
}

static int via_move_dmablit(struct ttm_buffer_object *bo,
			    bool evict, bool no_wait, struct ttm_mem_reg *new_mem)
{
	struct drm_via_private *dev_priv =
	    container_of(bo->bdev, struct drm_via_private, bdev);
	int ret;
	int fence_class;
	struct ttm_fence_object *fence;

	if (no_wait) {
		DRM_ERROR("Move dmablit busy.\n");
		return -EBUSY;
	}

	ret = via_dmablit_bo(bo, new_mem, NULL, &fence_class);
	if (unlikely(ret != 0)) {
		DRM_ERROR("Dmablit error %d\n", ret);
		return ret;
	}
	ret = ttm_fence_object_create(&dev_priv->fdev, fence_class,
				      TTM_FENCE_TYPE_EXE,
				      TTM_FENCE_FLAG_EMIT, &fence);
	if (unlikely(ret != 0)) {
		(void)via_driver_dma_quiescent(dev_priv->dev);
		if (fence)
			ttm_fence_object_unref(&fence);
	}

	ret = ttm_bo_move_accel_cleanup(bo, (void *)fence,
					(void *)(unsigned long)
					TTM_FENCE_TYPE_EXE, evict, no_wait,
					new_mem);
	if (fence)
		ttm_fence_object_unref(&fence);
	return ret;
}

static int via_move_vram_tt(struct ttm_buffer_object *bo,
			    bool evict, bool no_wait, struct ttm_mem_reg *new_mem)
{
	struct ttm_mem_reg *old_mem = &bo->mem;
	int ret;

	if (old_mem->mem_type == TTM_PL_TT) {
		struct ttm_mem_reg tmp_mem = *old_mem;

		tmp_mem.mm_node = NULL;
		tmp_mem.mem_type = TTM_PL_SYSTEM;
		ret = ttm_bo_move_ttm(bo, evict, no_wait, &tmp_mem);
		if (ret)
			return ret;

		return ttm_bo_move_memcpy(bo, evict, no_wait, new_mem);
	} else {
		struct ttm_mem_reg tmp_mem = *new_mem;

		tmp_mem.mm_node = NULL;
		tmp_mem.mem_type = TTM_PL_SYSTEM;
		ret = via_move_dmablit(bo, true, no_wait, &tmp_mem);
		if (ret)
			return ret;
		return ttm_bo_move_ttm(bo, evict, no_wait, new_mem);
	}
	return 0;
}

static void via_move_null(struct ttm_buffer_object *bo,
			  struct ttm_mem_reg *new_mem)
{
	struct ttm_mem_reg *old_mem = &bo->mem;

	BUG_ON(old_mem->mm_node != NULL);
	*old_mem = *new_mem;
	new_mem->mm_node = NULL;
}

int via_bo_move(struct ttm_buffer_object *bo,
		bool evict, bool interruptible, bool no_wait,
		struct ttm_mem_reg *new_mem)
{
	struct ttm_mem_reg *old_mem = &bo->mem;

	if (old_mem->mem_type == TTM_PL_SYSTEM && bo->ttm == NULL) {
		via_move_null(bo, new_mem);
		return 0;
	}

	if (old_mem->mem_type == TTM_PL_VRAM &&
	    new_mem->mem_type == TTM_PL_SYSTEM) {
		int ret = via_move_dmablit(bo, evict, no_wait, new_mem);

		if (likely(ret == 0))
			return 0;
	}

	if ((old_mem->mem_type == TTM_PL_VRAM &&
	     new_mem->mem_type == TTM_PL_TT) ||
	    (old_mem->mem_type == TTM_PL_TT &&
	     new_mem->mem_type == TTM_PL_VRAM)) {
		int ret = via_move_vram_tt(bo, evict, no_wait, new_mem);

		if (likely(ret == 0))
			return 0;
	}

	return ttm_bo_move_memcpy(bo, evict, no_wait, new_mem);
}
