/*
 * Copyright 2008 Tungsten Graphics, Inc., Cedar Park, Texas.
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
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL TUNGSTEN GRAPHICS AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *     Thomas Hellstrom <thomas-at-tungstengraphics-dot-com>
 */

#include "drmP.h"
#include "drm.h"
#include "via_drv.h"
#include "via_3d_reg.h"
#include "ttm/ttm_bo_api.h"
#include "ttm/ttm_execbuf_util.h"
#include "ttm/ttm_userobj_api.h"

struct via_validate_buffer {
	struct ttm_validate_buffer base;
	struct drm_via_validate_req req;
	int ret;
	struct drm_via_validate_arg __user *user_val_arg;
	uint32_t flags;
	uint32_t offset;
	uint32_t fence_type;
	int po_correct;
};

/*
 * These must correspond to the enum via_barriers
 * definition.
 */

static uint32_t via_barrier_fence_type[] = {
	VIA_FENCE_TYPE_HQV0,
	VIA_FENCE_TYPE_HQV1,
	VIA_FENCE_TYPE_MPEG0,
	VIA_FENCE_TYPE_MPEG1
};

static int
via_placement_fence_type(struct ttm_buffer_object *bo,
			 uint64_t set_val_flags,
			 uint64_t clr_val_flags,
			 uint32_t new_fence_class, uint32_t * new_fence_type)
{
	int ret;
	uint32_t n_fence_type;
	uint32_t set_flags = set_val_flags & VIA_PLACEMENT_MASK;
	uint32_t clr_flags = clr_val_flags & VIA_PLACEMENT_MASK;
	struct ttm_fence_object *old_fence;
	uint32_t old_fence_types;

	ret = ttm_bo_check_placement(bo, set_flags, clr_flags);
	if (unlikely(ret != 0))
		return ret;

	switch (new_fence_class) {
	case VIA_ENGINE_CMD:
		n_fence_type = TTM_FENCE_TYPE_EXE;
		if (set_val_flags & VIA_VAL_FLAG_HQV0)
			n_fence_type |= VIA_FENCE_TYPE_HQV0;
		if (set_val_flags & VIA_VAL_FLAG_HQV1)
			n_fence_type |= VIA_FENCE_TYPE_HQV1;
		if (set_val_flags & VIA_VAL_FLAG_MPEG0)
			n_fence_type |= VIA_FENCE_TYPE_MPEG0;
		if (set_val_flags & VIA_VAL_FLAG_MPEG1)
			n_fence_type |= VIA_FENCE_TYPE_MPEG1;
		break;
	default:
		n_fence_type = TTM_FENCE_TYPE_EXE;
#if 0
		/*
		 * FIXME
		 */
		if (bo->mem.proposed_placement & TTM_PL_FLAG_SYSTEM)
			n_fence_type |= VIA_FENCE_TYPE_SYSMEM;
#endif
		break;
	}

	*new_fence_type = n_fence_type;
	old_fence = (struct ttm_fence_object *)bo->sync_obj;
	old_fence_types = (uint32_t) (unsigned long)bo->sync_obj_arg;

	if (old_fence && ((new_fence_class != old_fence->fence_class) ||
			  ((n_fence_type ^ old_fence_types) &
			   old_fence_types))) {
		ret = ttm_bo_wait(bo, false, false, false);
		if (unlikely(ret != 0))
			return ret;
	}

	bo->proposed_placement |= set_flags & TTM_PL_MASK_MEMTYPE;
	bo->proposed_placement &= ~(clr_flags & TTM_PL_MASK_MEMTYPE);

	return 0;
}

static int
via_apply_texture_reloc(uint32_t ** cmdbuf,
			uint32_t num_buffers,
			struct via_validate_buffer *buffers,
			const struct drm_via_texture_reloc *reloc)
{
	const struct drm_via_reloc_bufaddr *baddr = reloc->addr;
	uint32_t baseh[4];
	uint32_t *buf = *cmdbuf + reloc->base.offset;
	uint32_t val;
	int i;
	int basereg;
	int shift;
	uint64_t flags = 0;
	uint32_t reg_tex_fm;

	memset(baseh, 0, sizeof(baseh));

	for (i = 0; i <= (reloc->hi_mip - reloc->low_mip); ++i) {
		if (baddr->index > num_buffers) {
			*cmdbuf = buf;
			return -EINVAL;
		}

		val = buffers[baddr->index].offset + baddr->delta;
		if (i == 0)
			flags = buffers[baddr->index].flags;

		*buf++ = ((HC_SubA_HTXnL0BasL + i) << 24) | (val & 0x00FFFFFF);

		basereg = i / 3;
		shift = (3 - (i % 3)) << 3;

		baseh[basereg] |= (val & 0xFF000000) >> shift;
		baddr++;
	}

	if (reloc->low_mip < 3)
		*buf++ = baseh[0] | (HC_SubA_HTXnL012BasH << 24);
	if (reloc->low_mip < 6 && reloc->hi_mip > 2)
		*buf++ = baseh[1] | (HC_SubA_HTXnL345BasH << 24);
	if (reloc->low_mip < 9 && reloc->hi_mip > 5)
		*buf++ = baseh[2] | (HC_SubA_HTXnL678BasH << 24);
	if (reloc->hi_mip > 8)
		*buf++ = baseh[3] | (HC_SubA_HTXnL9abBasH << 24);

	reg_tex_fm = reloc->reg_tex_fm & ~HC_HTXnLoc_MASK;

	if (flags & TTM_PL_FLAG_VRAM) {
		reg_tex_fm |= HC_HTXnLoc_Local;
	} else if (flags & (TTM_PL_FLAG_TT | TTM_PL_FLAG_PRIV0)) {
		reg_tex_fm |= HC_HTXnLoc_AGP;
	} else
		BUG();

	*buf++ = reg_tex_fm;
	*cmdbuf = buf;

	return 0;
}

static int
via_apply_zbuf_reloc(uint32_t ** cmdbuf,
		     uint32_t num_buffers,
		     struct via_validate_buffer *buffers,
		     const struct drm_via_zbuf_reloc *reloc)
{
	uint32_t *buf = *cmdbuf + reloc->base.offset;
	const struct drm_via_reloc_bufaddr *baddr = &reloc->addr;
	const struct via_validate_buffer *val_buf;
	uint32_t val;

	if (baddr->index > num_buffers)
		return -EINVAL;

	val_buf = &buffers[baddr->index];
	if (val_buf->po_correct)
		return 0;

	val = val_buf->offset + baddr->delta;
	*buf++ = (HC_SubA_HZWBBasL << 24) | (val & 0xFFFFFF);
	*buf++ = (HC_SubA_HZWBBasH << 24) | ((val & 0xFF000000) >> 24);

	*cmdbuf = buf;
	return 0;
}

static int
via_apply_yuv_reloc(uint32_t ** cmdbuf,
		    uint32_t num_buffers,
		    struct via_validate_buffer *buffers,
		    const struct drm_via_yuv_reloc *reloc)
{
	uint32_t *buf = *cmdbuf + reloc->base.offset;
	const struct drm_via_reloc_bufaddr *baddr = &reloc->addr;
	const struct via_validate_buffer *val_buf;
	uint32_t val;
	int i;

	if (reloc->planes > 4)
		return -EINVAL;

	if (baddr->index > num_buffers)
		return -EINVAL;

	val_buf = &buffers[baddr->index];
	if (val_buf->po_correct)
		return 0;
	val = val_buf->offset + baddr->delta;

	for (i = 0; i < reloc->planes; ++i) {
		*buf++ = (val + reloc->plane_offs[i]) >> reloc->shift;
		++buf;
	}

	*cmdbuf = buf - 1;
	return 0;
}

static int
via_apply_dstbuf_reloc(uint32_t ** cmdbuf,
		       uint32_t num_buffers,
		       struct via_validate_buffer *buffers,
		       const struct drm_via_zbuf_reloc *reloc)
{
	uint32_t *buf = *cmdbuf + reloc->base.offset;
	const struct drm_via_reloc_bufaddr *baddr = &reloc->addr;
	const struct via_validate_buffer *val_buf;
	uint32_t val;

	if (baddr->index > num_buffers)
		return -EINVAL;

	val_buf = &buffers[baddr->index];
	if (0 && val_buf->po_correct)
		return 0;

	val = val_buf->offset + baddr->delta;
	*buf++ = (HC_SubA_HDBBasL << 24) | (val & 0xFFFFFF);
	*buf++ = (HC_SubA_HDBBasH << 24) | ((val & 0xFF000000) >> 24);

	*cmdbuf = buf;
	return 0;
}

static int
via_apply_2d_reloc(uint32_t ** cmdbuf,
		   uint32_t num_buffers,
		   const struct via_validate_buffer *buffers,
		   const struct drm_via_2d_reloc *reloc)
{
	uint32_t *buf = *cmdbuf + reloc->base.offset;
	const struct drm_via_reloc_bufaddr *baddr = &reloc->addr;
	const struct via_validate_buffer *val_buf;
	uint32_t val;
	uint32_t x;

	if (baddr->index > num_buffers)
		return -EINVAL;

	val_buf = &buffers[baddr->index];
	if (val_buf->po_correct)
		return 0;

	val = val_buf->base.bo->offset + baddr->delta;
	x = val & 0x1f;

	if (reloc->bpp == 32)
		x >>= 2;
	else if (reloc->bpp == 16)
		x >>= 1;

	*buf = (val & ~0x1f) >> 3;
	buf += 2;
	*buf++ = reloc->pos + x;

	*cmdbuf = buf;
	return 0;
}

int via_apply_reloc(struct drm_via_private *dev_priv,
		    void **reloc_buf,
		    struct via_validate_buffer *bufs,
		    uint32_t num_validate_buffers, uint32_t * cmdbuf)
{
	const struct drm_via_base_reloc *reloc =
	    (const struct drm_via_base_reloc *)*reloc_buf;
	size_t size;
	int ret;

	switch (reloc->type) {
	case VIA_RELOC_TEX:

		{
			const struct drm_via_texture_reloc *tex_reloc =
			    (const struct drm_via_texture_reloc *)reloc;

			ret =
			    via_apply_texture_reloc(&cmdbuf,
						    num_validate_buffers, bufs,
						    (const struct
						     drm_via_texture_reloc *)
						    reloc);

			size = offsetof(struct drm_via_texture_reloc, addr) -
			    offsetof(struct drm_via_texture_reloc, base) +
			    (tex_reloc->hi_mip - tex_reloc->low_mip + 1) *
			    sizeof(struct drm_via_reloc_bufaddr);
		}
		break;

	case VIA_RELOC_ZBUF:
		ret = via_apply_zbuf_reloc(&cmdbuf, num_validate_buffers, bufs,
					   (const struct drm_via_zbuf_reloc *)
					   reloc);
		size = sizeof(struct drm_via_zbuf_reloc);
		break;

	case VIA_RELOC_DSTBUF:
		ret =
		    via_apply_dstbuf_reloc(&cmdbuf, num_validate_buffers, bufs,
					   (const struct drm_via_zbuf_reloc *)
					   reloc);
		size = sizeof(struct drm_via_zbuf_reloc);
		break;

	case VIA_RELOC_2D:
		ret = via_apply_2d_reloc(&cmdbuf, num_validate_buffers, bufs,
					 (const struct drm_via_2d_reloc *)
					 reloc);
		size = sizeof(struct drm_via_2d_reloc);
		break;

	case VIA_RELOC_YUV:
		ret = via_apply_yuv_reloc(&cmdbuf, num_validate_buffers, bufs,
					  (const struct drm_via_yuv_reloc *)
					  reloc);
		size = sizeof(struct drm_via_yuv_reloc);
		break;
	default:
		DRM_ERROR("Illegal relocation type %d\n", reloc->type);
		ret = -EINVAL;
		break;
	}

	if (ret)
		return ret;

	*reloc_buf = (void *)((char *)*reloc_buf + size);
	return 0;
}

static int
via_apply_reloc_chain(struct drm_via_private *dev_priv,
		      uint64_t data,
		      uint32_t * cmdbuf_addr,
		      int is_iomem,
		      struct via_cpriv *cpriv, uint32_t num_validate_buffers)
{
	struct drm_via_reloc_header __user *user_header =
	    (struct drm_via_reloc_header __user *)(unsigned long)data;
	struct drm_via_reloc_header *header;
	void *relocs;
	int count = 0;
	int i;
	uint32_t size;
	int ret;

	while (user_header != NULL) {
		//          DRM_INFO("Reloc header %d\n", count);
		ret = get_user(size, &user_header->used);
		if (unlikely(ret != 0))
			return ret;

		//              DRM_INFO("Header size %d\n", size);
		if (unlikely(size > VIA_RELOC_BUF_SIZE)) {
			DRM_ERROR("Illegal relocation buffer size.\n");
			return -EINVAL;
		}

		ret = copy_from_user(cpriv->reloc_buf, user_header, size);
		if (unlikely(ret != 0))
			return ret;

		header = (struct drm_via_reloc_header *)cpriv->reloc_buf;
		relocs = (void *)((unsigned long)header + sizeof(*header));

		for (i = 0; i < header->num_relocs; ++i) {
			//                  DRM_INFO("Reloc %d\n", i);
			ret =
			    via_apply_reloc(dev_priv, &relocs, cpriv->val_bufs,
					    num_validate_buffers, cmdbuf_addr);
			if (ret)
				return ret;
		}

		user_header =
		    (struct drm_via_reloc_header __user *)(unsigned long)
		    header->next_header;
		count++;
	}

	return 0;
}

static int via_check_presumed(struct drm_via_validate_req *req,
			      struct ttm_buffer_object *bo,
			      struct drm_via_validate_arg __user * data,
			      int *presumed_ok)
{
	struct drm_via_validate_req __user *user_req = &(data->d.req);

	*presumed_ok = 0;
	if (unlikely(!(req->presumed_flags & VIA_USE_PRESUMED)))
		return 0;

	if (bo->mem.mem_type == TTM_PL_SYSTEM) {
		*presumed_ok = 1;
		return 0;
	}

	if (bo->offset == req->presumed_gpu_offset &&
	    !((req->presumed_flags & VIA_PRESUMED_AGP) &&
	      !(bo->mem.placement & (TTM_PL_FLAG_PRIV0 | TTM_PL_FLAG_TT)))) {
		*presumed_ok = 1;
		return 0;
	}

	return __put_user(req->presumed_flags & ~VIA_USE_PRESUMED,
			  &user_req->presumed_flags);
}

static int via_lookup_validate_buffer(struct drm_file *file_priv,
				      uint64_t data,
				      struct via_validate_buffer *item)
{
	struct ttm_object_file *tfile = via_fpriv(file_priv)->tfile;

	item->user_val_arg =
	    (struct drm_via_validate_arg __user *)(unsigned long)data;

	if (copy_from_user(&item->req, &item->user_val_arg->d.req,
			   sizeof(item->req)))
		return -EFAULT;

	item->base.bo =
	    ttm_buffer_object_lookup(tfile, item->req.buffer_handle);
	if (item->base.bo == NULL)
		return -EINVAL;

	return 0;
}

static void via_unreference_buffers(struct list_head *list)
{
	struct ttm_validate_buffer *entry, *next;
	struct via_validate_buffer *vbuf;

	list_for_each_entry_safe(entry, next, list, head) {
		vbuf = container_of(entry, struct via_validate_buffer, base);
		list_del(&entry->head);
		ttm_bo_unref(&entry->bo);
	}
}

static int via_reference_buffers(struct drm_file *file_priv,
				 struct list_head *list,
				 struct via_cpriv *cpriv,
				 uint64_t data, uint32_t * num_buffers)
{
	struct drm_device *dev = file_priv->minor->dev;
	struct drm_via_private *dev_priv = via_priv(dev);
	struct via_validate_buffer *item;
	uint32_t buf_count;
	int ret;

	INIT_LIST_HEAD(list);

	if (unlikely(*num_buffers == 0)) {
		if (data == 0ULL)
			return 0;
		else
			goto out_err0;
	}

	if (unlikely(*num_buffers > dev_priv->max_validate_buffers))
		goto out_err0;

	buf_count = 0;
	while (likely(data != 0)) {
		item = &cpriv->val_bufs[buf_count];

		ret = via_lookup_validate_buffer(file_priv, data, item);
		if (unlikely(ret != 0))
			goto out_err1;

		list_add_tail(&item->base.head, list);
		++buf_count;
		data = item->req.next;
	}

	*num_buffers = buf_count;
	return 0;

      out_err1:
	via_unreference_buffers(list);
	return ret;

      out_err0:
	DRM_ERROR("Too many validate buffers on validate list.\n");
	return -EINVAL;
}

static int via_validate_buffer_list(struct drm_file *file_priv,
				    uint32_t fence_class,
				    struct list_head *list,
				    int *po_correct,
				    uint32_t * buffers_fence_types)
{
	struct via_validate_buffer *item;
	struct ttm_buffer_object *bo;
	int ret;
	struct drm_via_validate_req *req;
	uint32_t fence_types = 0;
	uint32_t cur_fence_type;
	struct ttm_validate_buffer *entry;

	*po_correct = 1;

	list_for_each_entry(entry, list, head) {
		item = container_of(entry, struct via_validate_buffer, base);
		bo = entry->bo;
		item->ret = 0;
		req = &item->req;

		mutex_lock(&bo->mutex);
		ret = via_placement_fence_type(bo,
					       req->set_flags,
					       req->clear_flags,
					       fence_class, &cur_fence_type);
		if (unlikely(ret != 0))
			goto out_err;

		ret = ttm_buffer_object_validate(bo, bo->proposed_placement,
						 true, false);

		if (unlikely(ret != 0))
			goto out_err;

		fence_types |= cur_fence_type;
		entry->new_sync_obj_arg = (void *)(unsigned long)cur_fence_type;

		item->offset = bo->offset;
		item->flags = bo->mem.placement;
		mutex_unlock(&bo->mutex);

		ret = via_check_presumed(&item->req, bo, item->user_val_arg,
					 &item->po_correct);
		if (unlikely(ret != 0))
			goto out_err;

		if (unlikely(!item->po_correct))
			*po_correct = 0;

		item++;
	}

	*buffers_fence_types = fence_types;

	return 0;
      out_err:
	mutex_unlock(&bo->mutex);
	item->ret = ret;
	return ret;
}

static int via_handle_copyback(struct drm_device *dev,
			       struct list_head *list, int ret)
{
	int err = ret;
	struct ttm_validate_buffer *entry;
	struct drm_via_validate_arg arg;

	if (ret)
		ttm_eu_backoff_reservation(list);

	if (ret != -ERESTART) {
		list_for_each_entry(entry, list, head) {
			struct via_validate_buffer *vbuf =
			    container_of(entry, struct via_validate_buffer,
					 base);
			arg.handled = 1;
			arg.ret = vbuf->ret;
			if (!arg.ret) {
				struct ttm_buffer_object *bo = entry->bo;

				mutex_lock(&bo->mutex);
				arg.d.rep.gpu_offset = bo->offset;
				arg.d.rep.placement = bo->mem.placement;
				arg.d.rep.fence_type_mask =
				    (uint32_t) (unsigned long)
				    entry->new_sync_obj_arg;
				mutex_unlock(&bo->mutex);
			}

			if (__copy_to_user(vbuf->user_val_arg,
					   &arg, sizeof(arg)))
				err = -EFAULT;

			if (arg.ret)
				break;
		}
	}

	return err;
}

/*
 * Create a fence object, and if that fails, pretend that everything is
 * OK and just idle the GPU.
 */

static void via_fence_or_sync(struct drm_file *file_priv,
			      uint32_t fence_class,
			      uint32_t fence_types,
			      uint32_t fence_flags,
			      struct list_head *list,
			      struct drm_via_ttm_fence_rep *fence_arg,
			      struct ttm_fence_object **fence_p)
{
	struct drm_device *dev = file_priv->minor->dev;
	struct drm_via_private *dev_priv = via_priv(dev);
	struct ttm_fence_device *fdev = &dev_priv->fdev;
	int ret;
	struct ttm_fence_object *fence;
	struct ttm_object_file *tfile = via_fpriv(file_priv)->tfile;
	uint32_t handle;

	ret = ttm_fence_user_create(fdev, tfile,
				    fence_class, fence_types,
				    TTM_FENCE_FLAG_EMIT, &fence, &handle);
	if (ret) {

		/*
		 * Fence creation failed.
		 * Fall back to synchronous operation and idle the engine.
		 */

		(void)via_driver_dma_quiescent(dev);
		if (!(fence_flags & DRM_VIA_FENCE_NO_USER)) {

			/*
			 * Communicate to user-space that
			 * fence creation has failed and that
			 * the engine is idle.
			 */

			fence_arg->handle = ~0;
			fence_arg->error = ret;
		}

		ttm_eu_backoff_reservation(list);
		if (fence_p)
			*fence_p = NULL;
		return;
	}

	ttm_eu_fence_buffer_objects(list, (void *)fence);
	if (!(fence_flags & DRM_VIA_FENCE_NO_USER)) {
		struct ttm_fence_info info = ttm_fence_get_info(fence);
		fence_arg->handle = handle;
		fence_arg->fence_class = fence->fence_class;
		fence_arg->fence_type = fence->fence_type;
		fence_arg->signaled_types = info.signaled_types;
		fence_arg->error = 0;
	} else {
		ttm_ref_object_base_unref(tfile, handle, ttm_fence_type);
	}

	if (fence_p)
		*fence_p = fence;
	else if (fence)
		ttm_fence_object_unref(&fence);
}

static int via_apply_cliprect(uint32_t * cmdbuf, uint32_t offset, uint64_t clip)
{
	struct drm_via_clip_rect clip_rect;
	void __user *user_clip = (void *)(unsigned long)clip;
	int ret;

	ret = copy_from_user(&clip_rect, user_clip, sizeof(clip_rect));
	if (unlikely(ret))
		return ret;

	cmdbuf += offset;
	cmdbuf[0] = (HC_SubA_HClipTB << 24) |
	    ((clip_rect.y1) << 12) | (clip_rect.y2);
	cmdbuf[1] = (HC_SubA_HClipLR << 24) |
	    ((clip_rect.x1) << 12) | (clip_rect.x2);

	return 0;
}

static int via_dispatch_clip(struct drm_device *dev,
			     struct drm_via_ttm_execbuf_arg *exec_buf,
			     struct drm_via_ttm_execbuf_control *control,
			     int po_correct,
			     struct via_cpriv *cpriv,
			     uint32_t num_buffers,
			     uint32_t * cmdbuf_addr, int cmdbuf_iomem)
{
	struct drm_via_private *dev_priv = via_priv(dev);
	uint32_t num_clip;
	uint32_t first_clip;
	uint32_t i;
	uint64_t cliprect_addr = 0;
	int ret = 0;
	bool emit_seq;

	if (exec_buf->exec_flags & DRM_VIA_HAVE_CLIP) {
		first_clip = control->first_clip;
		cliprect_addr = exec_buf->cliprect_addr +
		    first_clip * sizeof(struct drm_via_clip_rect);
		num_clip = exec_buf->num_cliprects;
	} else {
		control->first_clip = 0;
		first_clip = 0;
		num_clip = 1;
	}

	for (i = first_clip; i < num_clip; ++i) {
		emit_seq = ((i == num_clip - 1) &&
			    !(exec_buf->exec_flags & DRM_VIA_DEFER_FENCING));

		if (i > first_clip && drm_via_disable_verifier &&
		    exec_buf->mechanism == _VIA_MECHANISM_AGP) {
			ret = via_copy_cmdbuf(dev_priv, exec_buf->cmd_buffer,
					      exec_buf->cmd_buffer_size,
					      exec_buf->mechanism,
					      &cmdbuf_addr, &cmdbuf_iomem);

			if (unlikely(ret != 0))
				goto out;

			if (unlikely(!po_correct)) {
				ret = via_apply_reloc_chain(dev_priv,
							    exec_buf->
							    reloc_list,
							    cmdbuf_addr,
							    cmdbuf_iomem, cpriv,
							    num_buffers);
				if (unlikely(ret != 0))
					goto out;
			}
		}

		if (i == 0 && (!drm_via_disable_verifier ||
			       exec_buf->mechanism != _VIA_MECHANISM_AGP)) {

			ret = via_verify_command_stream(cmdbuf_addr,
							exec_buf->
							cmd_buffer_size, dev,
							exec_buf->mechanism ==
							_VIA_MECHANISM_AGP);
			if (unlikely(ret != 0)) {
				DRM_ERROR("Command verifier error\n");
				goto out;
			}
		}

		if (exec_buf->exec_flags & DRM_VIA_HAVE_CLIP) {
			ret = via_apply_cliprect(cmdbuf_addr,
						 exec_buf->cliprect_offset,
						 cliprect_addr);
			cliprect_addr += sizeof(struct drm_via_clip_rect);
		}

		ret = via_dispatch_commands(dev, exec_buf->cmd_buffer_size,
					    exec_buf->mechanism, emit_seq);

		if (unlikely(ret != 0))
			goto out;

		++control->first_clip;
	}

      out:
	return ret;
}

static int via_wait_single_barrier(struct drm_via_private *dev_priv,
				   uint32_t fence_type,
				   uint32_t barrier_type,
				   enum via_barriers barrier)
{
	int ret;

	if (likely(!(fence_type & barrier_type)))
		return 0;
	if (likely(dev_priv->barriers[barrier] == NULL))
		return 0;

	ret = ttm_fence_object_wait(dev_priv->barriers[barrier], false, true,
				    barrier_type);
	if (unlikely(ret != 0))
		return ret;

	ttm_fence_object_unref(&dev_priv->barriers[barrier]);
	return 0;
}

static int via_wait_barriers(struct drm_via_private *dev_priv,
			     uint32_t fence_type)
{
	int ret;
	int i;

	if (likely(fence_type == TTM_FENCE_TYPE_EXE))
		return 0;

	for (i = 0; i < VIA_NUM_BARRIERS; ++i) {
		ret = via_wait_single_barrier(dev_priv, fence_type,
					      via_barrier_fence_type[i],
					      VIA_BARRIER_HQV0 + i);
		if (unlikely(ret != 0))
			return ret;
	}
	return 0;
}

static void via_update_single_barrier(struct drm_via_private *dev_priv,
				      uint32_t barrier_type,
				      enum via_barriers barrier,
				      struct ttm_fence_object *fence)
{
	if (likely(!(fence->fence_type & barrier_type)))
		return;

	if (dev_priv->barriers[barrier] != NULL)
		ttm_fence_object_unref(&dev_priv->barriers[barrier]);

	dev_priv->barriers[barrier] = ttm_fence_object_ref(fence);
}

static void via_update_barriers(struct drm_via_private *dev_priv,
				struct ttm_fence_object *fence)
{
	int i;

	if (likely(fence->fence_type == TTM_FENCE_TYPE_EXE))
		return;

	for (i = 0; i < VIA_NUM_BARRIERS; ++i) {
		via_update_single_barrier(dev_priv,
					  via_barrier_fence_type[i],
					  VIA_BARRIER_HQV0 + i, fence);
	}
}

int via_execbuffer(struct drm_device *dev, void *data,
		   struct drm_file *file_priv)
{
	struct drm_via_private *dev_priv = via_priv(dev);
	struct drm_via_ttm_execbuf_arg *exec_buf =
	    (struct drm_via_ttm_execbuf_arg *)data;
	struct ttm_fence_object *barrier_fence;
	int num_buffers;
	int ret;
	int po_correct;
	uint32_t *cmdbuf_addr;
	int cmdbuf_iomem;
	uint32_t buffers_fence_types;
	struct list_head validate_list;
	uint32_t val_seq;
	struct via_cpriv *cpriv =
	    via_context_lookup(dev_priv, exec_buf->context);
	struct drm_via_ttm_execbuf_control control;
	struct drm_via_ttm_execbuf_control __user *user_control;
	struct drm_via_ttm_fence_rep *fence_rep = &control.rep;

	if (unlikely(cpriv == NULL)) {
		DRM_ERROR("Illegal DRM context: %d.\n", exec_buf->context);
		return -EINVAL;
	}

	if (unlikely(!atomic_inc_and_test(&cpriv->in_execbuf))) {
		DRM_ERROR("Multiple thread simultaneous use of DRM "
			  "context: %d.\n", exec_buf->context);
		ret = -EINVAL;
		goto out_err_ctx1;
	}

	if (unlikely(exec_buf->num_buffers > dev_priv->max_validate_buffers)) {
		DRM_ERROR("Too many buffers on validate list: %d\n",
			  exec_buf->num_buffers);
		ret = -EINVAL;
		goto out_err_ctx1;
	}

	user_control = (struct drm_via_ttm_execbuf_control __user *)
	    (unsigned long)exec_buf->control;

	if (unlikely(!access_ok(VERIFY_WRITE, user_control,
				sizeof(*user_control)))) {
		DRM_ERROR("Invalid execbuf control block address.\n");
		ret = -EFAULT;
		goto out_err_ctx1;
	}

	ret = __get_user(control.first_clip, &user_control->first_clip);
	if (unlikely(ret != 0))
		goto out_err_ctx1;

	/*
	 * The tlock makes it possible for a root process to block
	 * all command submission by taking this lock in write mode.
	 * Used to block command submission on vt switches.
	 */

	ret = ttm_read_lock(&dev_priv->ttm_lock, true);
	if (unlikely(ret != 0))
		goto out_err_ctx1;

	if (unlikely(cpriv->val_bufs == NULL)) {
		cpriv->val_bufs = vmalloc(sizeof(struct via_validate_buffer) *
					  dev_priv->max_validate_buffers);
		if (unlikely(cpriv->val_bufs == NULL)) {
			DRM_ERROR("Failed allocating memory for "
				  "validate list.\n");
			ret = -ENOMEM;
			goto out_err_ctx1;
		}
	}

	num_buffers = exec_buf->num_buffers;
	ret = via_reference_buffers(file_priv,
				    &validate_list,
				    cpriv, exec_buf->buffer_list, &num_buffers);

	if (unlikely(ret != 0))
		goto out_err0;

	val_seq = atomic_add_return(1, &dev_priv->val_seq);

	ret = ttm_eu_reserve_buffers(&validate_list, val_seq);

	if (unlikely(ret != 0))
		goto out_err1;

	ret = via_validate_buffer_list(file_priv, VIA_ENGINE_CMD,
				       &validate_list,
				       &po_correct, &buffers_fence_types);
	if (unlikely(ret != 0))
		goto out_err2;

	/*
	 * The cmdbuf_mutex protects the AGP ring buffer and the
	 * dev_priv->pci_buf command cache. Note that also the
	 * command verifier that operates on this command cache
	 * maintains a single simulated hardware state, so it cannot
	 * operate in parallel on different buffers.
	 */

	ret = mutex_lock_interruptible(&dev_priv->cmdbuf_mutex);
	if (unlikely(ret != 0)) {
		ret = -ERESTART;
		goto out_err2;
	}

	ret = via_copy_cmdbuf(dev_priv, exec_buf->cmd_buffer,
			      exec_buf->cmd_buffer_size,
			      exec_buf->mechanism, &cmdbuf_addr, &cmdbuf_iomem);

	if (unlikely(ret != 0))
		goto out_err3;

	if (unlikely(!po_correct)) {
		ret = via_apply_reloc_chain(dev_priv, exec_buf->reloc_list,
					    cmdbuf_addr, cmdbuf_iomem,
					    cpriv, num_buffers);
		if (unlikely(ret != 0))
			goto out_err3;
	}

	if (exec_buf->exec_flags & DRM_VIA_WAIT_BARRIER) {
		ret = via_wait_barriers(dev_priv, buffers_fence_types);
		if (unlikely(ret != 0))
			goto out_err3;
	}

	ret = via_dispatch_clip(dev, exec_buf, &control, po_correct, cpriv,
				num_buffers, cmdbuf_addr, cmdbuf_iomem);

	if (likely(ret == 0 || control.first_clip != 0)) {
		via_fence_or_sync(file_priv, VIA_ENGINE_CMD,
				  buffers_fence_types,
				  exec_buf->exec_flags,
				  &validate_list, fence_rep, &barrier_fence);
		if (likely(barrier_fence)) {
			via_update_barriers(dev_priv, barrier_fence);
			ttm_fence_object_unref(&barrier_fence);
		}
		if (__copy_to_user(user_control, &control, sizeof(control))) {

			/*
			 * User-space can't unref the fence object so do it here.
			 */

			if (likely(fence_rep->handle != ~0))
				ttm_ref_object_base_unref(via_fpriv(file_priv)->
							  tfile,
							  fence_rep->handle,
							  ttm_fence_type);
			ret = -EFAULT;
			(void)via_driver_dma_quiescent(dev);
		}
	}
      out_err3:
	mutex_unlock(&dev_priv->cmdbuf_mutex);
      out_err2:
	ret = via_handle_copyback(dev, &validate_list, ret);
      out_err1:
	via_unreference_buffers(&validate_list);
      out_err0:
	ttm_read_unlock(&dev_priv->ttm_lock);
      out_err_ctx1:
	atomic_dec(&cpriv->in_execbuf);
	via_context_unref(&cpriv);

	return ret;
}
