/**************************************************************************
 *
 * Copyright (c) 2006-2008 Tungsten Graphics, Inc., Cedar Park, TX., USA
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

#include "ttm/ttm_placement_user.h"
#include "ttm/ttm_bo_driver.h"
#include "ttm/ttm_object.h"
#include "ttm/ttm_userobj_api.h"
#include "ttm/ttm_lock.h"

struct ttm_bo_user_object {
	struct ttm_base_object base;
	struct ttm_buffer_object bo;
};

static size_t pl_bo_size = 0;

static size_t ttm_pl_size(struct ttm_bo_device *bdev, unsigned long num_pages)
{
	size_t page_array_size =
	    (num_pages * sizeof(void *) + PAGE_SIZE - 1) & PAGE_MASK;

	if (unlikely(pl_bo_size == 0)) {
		pl_bo_size = bdev->ttm_bo_extra_size +
		    ttm_round_pot(sizeof(struct ttm_bo_user_object));
	}

	return bdev->ttm_bo_size + 2 * page_array_size;
}

static struct ttm_bo_user_object *ttm_bo_user_lookup(struct ttm_object_file
						     *tfile, uint32_t handle)
{
	struct ttm_base_object *base;

	base = ttm_base_object_lookup(tfile, handle);
	if (unlikely(base == NULL)) {
		printk(KERN_ERR "Invalid buffer object handle 0x%08lx.\n",
		       (unsigned long)handle);
		return NULL;
	}

	if (unlikely(base->object_type != ttm_buffer_type)) {
		ttm_base_object_unref(&base);
		printk(KERN_ERR "Invalid buffer object handle 0x%08lx.\n",
		       (unsigned long)handle);
		return NULL;
	}

	return container_of(base, struct ttm_bo_user_object, base);
}

struct ttm_buffer_object *ttm_buffer_object_lookup(struct ttm_object_file
						   *tfile, uint32_t handle)
{
	struct ttm_bo_user_object *user_bo;
	struct ttm_base_object *base;

	user_bo = ttm_bo_user_lookup(tfile, handle);
	if (unlikely(user_bo == NULL))
		return NULL;

	(void)ttm_bo_reference(&user_bo->bo);
	base = &user_bo->base;
	ttm_base_object_unref(&base);
	return &user_bo->bo;
}

static void ttm_bo_user_destroy(struct ttm_buffer_object *bo)
{
	struct ttm_bo_user_object *user_bo =
	    container_of(bo, struct ttm_bo_user_object, bo);

	kfree(user_bo);
	ttm_mem_global_free(bo->bdev->mem_glob, bo->acc_size, 0);
}

static void ttm_bo_user_release(struct ttm_base_object **p_base)
{
	struct ttm_bo_user_object *user_bo;
	struct ttm_base_object *base = *p_base;
	struct ttm_buffer_object *bo;

	*p_base = NULL;

	if (unlikely(base == NULL))
		return;

	user_bo = container_of(base, struct ttm_bo_user_object, base);
	bo = &user_bo->bo;
	ttm_bo_unref(&bo);
}

static void ttm_bo_user_ref_release(struct ttm_base_object *base,
				    enum ttm_ref_type ref_type)
{
	struct ttm_bo_user_object *user_bo =
	    container_of(base, struct ttm_bo_user_object, base);
	struct ttm_buffer_object *bo = &user_bo->bo;

	switch (ref_type) {
	case TTM_REF_SYNCCPU_WRITE:
		ttm_bo_synccpu_write_release(bo);
		break;
	default:
		BUG();
	}
}

static void ttm_pl_fill_rep(struct ttm_buffer_object *bo,
			    struct ttm_pl_rep *rep)
{
	struct ttm_bo_user_object *user_bo =
	    container_of(bo, struct ttm_bo_user_object, bo);

	rep->gpu_offset = bo->offset;
	rep->bo_size = bo->num_pages << PAGE_SHIFT;
	rep->map_handle = bo->addr_space_offset;
	rep->placement = bo->mem.flags;
	rep->handle = user_bo->base.hash.key;
	rep->sync_object_arg = (uint32_t) (unsigned long)bo->sync_obj_arg;
}

int ttm_pl_create_ioctl(struct ttm_object_file *tfile,
			struct ttm_bo_device *bdev,
			struct ttm_lock *lock, void *data)
{
	union ttm_pl_create_arg *arg = data;
	struct ttm_pl_create_req *req = &arg->req;
	struct ttm_pl_rep *rep = &arg->rep;
	struct ttm_buffer_object *bo;
	struct ttm_buffer_object *tmp;
	struct ttm_bo_user_object *user_bo;
	uint32_t flags;
	int ret = 0;
	struct ttm_mem_global *mem_glob = bdev->mem_glob;
	size_t acc_size =
	    ttm_pl_size(bdev, (req->size + PAGE_SIZE - 1) >> PAGE_SHIFT);
	ret = ttm_mem_global_alloc(mem_glob, acc_size, 0, 0, 0);
	if (unlikely(ret != 0))
		return ret;

	flags = req->placement;
	user_bo = kzalloc(sizeof(*user_bo), GFP_KERNEL);
	if (unlikely(user_bo == NULL)) {
		ttm_mem_global_free(mem_glob, acc_size, 0);
		return -ENOMEM;
	}

	bo = &user_bo->bo;
	ret = ttm_read_lock(lock, true);
	if (unlikely(ret != 0)) {
		kfree(user_bo);
		ttm_mem_global_free(mem_glob, acc_size, 0);
		return ret;
	}

	ret = ttm_buffer_object_init(bdev, bo, req->size,
				     ttm_bo_type_device, flags,
				     req->page_alignment, 0, 1,
				     NULL, acc_size, &ttm_bo_user_destroy);
	ttm_read_unlock(lock);

	/*
	 * Note that the ttm_buffer_object_init function
	 * would've called the destroy function on failure!!
	 */

	if (unlikely(ret != 0))
		goto out;

	tmp = ttm_bo_reference(bo);
	ret = ttm_base_object_init(tfile, &user_bo->base,
				   flags & TTM_PL_FLAG_SHARED,
				   ttm_buffer_type,
				   &ttm_bo_user_release,
				   &ttm_bo_user_ref_release);
	if (unlikely(ret != 0))
		goto out_err;

	mutex_lock(&bo->mutex);
	ttm_pl_fill_rep(bo, rep);
	mutex_unlock(&bo->mutex);
	ttm_bo_unref(&bo);
      out:
	return 0;
      out_err:
	ttm_bo_unref(&tmp);
	ttm_bo_unref(&bo);
	return ret;
}

int ttm_pl_ub_create_ioctl(struct ttm_object_file *tfile,
			   struct ttm_bo_device *bdev,
			   struct ttm_lock *lock, void *data)
{
	union ttm_pl_create_ub_arg *arg = data;
	struct ttm_pl_create_ub_req *req = &arg->req;
	struct ttm_pl_rep *rep = &arg->rep;
	struct ttm_buffer_object *bo;
	struct ttm_buffer_object *tmp;
	struct ttm_bo_user_object *user_bo;
	uint32_t flags;
	int ret = 0;
	struct ttm_mem_global *mem_glob = bdev->mem_glob;
	size_t acc_size =
	    ttm_pl_size(bdev, (req->size + PAGE_SIZE - 1) >> PAGE_SHIFT);
	ret = ttm_mem_global_alloc(mem_glob, acc_size, 0, 0, 0);
	if (unlikely(ret != 0))
		return ret;

	flags = req->placement;
	user_bo = kzalloc(sizeof(*user_bo), GFP_KERNEL);
	if (unlikely(user_bo == NULL)) {
		ttm_mem_global_free(mem_glob, acc_size, 0);
		return -ENOMEM;
	}
	ret = ttm_read_lock(lock, true);
	if (unlikely(ret != 0)) {
		ttm_mem_global_free(mem_glob, acc_size, 0);
		kfree(user_bo);
		return ret;
	}
	bo = &user_bo->bo;
	ret = ttm_buffer_object_init(bdev, bo, req->size,
				     ttm_bo_type_user, flags,
				     req->page_alignment, req->user_address,
				     1, NULL, acc_size, &ttm_bo_user_destroy);

	/*
	 * Note that the ttm_buffer_object_init function
	 * would've called the destroy function on failure!!
	 */
	ttm_read_unlock(lock);
	if (unlikely(ret != 0))
		goto out;

	tmp = ttm_bo_reference(bo);
	ret = ttm_base_object_init(tfile, &user_bo->base,
				   flags & TTM_PL_FLAG_SHARED,
				   ttm_buffer_type,
				   &ttm_bo_user_release,
				   &ttm_bo_user_ref_release);
	if (unlikely(ret != 0))
		goto out_err;

	mutex_lock(&bo->mutex);
	ttm_pl_fill_rep(bo, rep);
	mutex_unlock(&bo->mutex);
	ttm_bo_unref(&bo);
      out:
	return 0;
      out_err:
	ttm_bo_unref(&tmp);
	ttm_bo_unref(&bo);
	return ret;
}

int ttm_pl_reference_ioctl(struct ttm_object_file *tfile, void *data)
{
	union ttm_pl_reference_arg *arg = data;
	struct ttm_pl_rep *rep = &arg->rep;
	struct ttm_bo_user_object *user_bo;
	struct ttm_buffer_object *bo;
	struct ttm_base_object *base;
	int ret;

	user_bo = ttm_bo_user_lookup(tfile, arg->req.handle);
	if (unlikely(user_bo == NULL)) {
		printk(KERN_ERR "Could not reference buffer object.\n");
		return -EINVAL;
	}

	bo = &user_bo->bo;
	ret = ttm_ref_object_add(tfile, &user_bo->base, TTM_REF_USAGE, NULL);
	if (unlikely(ret != 0)) {
		printk(KERN_ERR
		       "Could not add a reference to buffer object.\n");
		goto out;
	}

	mutex_lock(&bo->mutex);
	ttm_pl_fill_rep(bo, rep);
	mutex_unlock(&bo->mutex);

      out:
	base = &user_bo->base;
	ttm_base_object_unref(&base);
	return ret;
}

int ttm_pl_unref_ioctl(struct ttm_object_file *tfile, void *data)
{
	struct ttm_pl_reference_req *arg = data;

	return ttm_ref_object_base_unref(tfile, arg->handle, TTM_REF_USAGE);
}

int ttm_pl_synccpu_ioctl(struct ttm_object_file *tfile, void *data)
{
	struct ttm_pl_synccpu_arg *arg = data;
	struct ttm_bo_user_object *user_bo;
	struct ttm_buffer_object *bo;
	struct ttm_base_object *base;
	int existed;
	int ret;

	switch (arg->op) {
	case TTM_PL_SYNCCPU_OP_GRAB:
		user_bo = ttm_bo_user_lookup(tfile, arg->handle);
		if (unlikely(user_bo == NULL)) {
			printk(KERN_ERR
			       "Could not find buffer object for synccpu.\n");
			return -EINVAL;
		}
		bo = &user_bo->bo;
		base = &user_bo->base;
		ret = ttm_bo_synccpu_write_grab(bo,
						arg->access_mode &
						TTM_PL_SYNCCPU_MODE_NO_BLOCK);
		if (unlikely(ret != 0)) {
			ttm_base_object_unref(&base);
			goto out;
		}
		ret = ttm_ref_object_add(tfile, &user_bo->base,
					 TTM_REF_SYNCCPU_WRITE, &existed);
		if (existed || ret != 0)
			ttm_bo_synccpu_write_release(bo);
		ttm_base_object_unref(&base);
		break;
	case TTM_PL_SYNCCPU_OP_RELEASE:
		ret = ttm_ref_object_base_unref(tfile, arg->handle,
						TTM_REF_SYNCCPU_WRITE);
		break;
	default:
		ret = -EINVAL;
		break;
	}
      out:
	return ret;
}

int ttm_pl_setstatus_ioctl(struct ttm_object_file *tfile,
			   struct ttm_lock *lock, void *data)
{
	union ttm_pl_setstatus_arg *arg = data;
	struct ttm_pl_setstatus_req *req = &arg->req;
	struct ttm_pl_rep *rep = &arg->rep;
	struct ttm_buffer_object *bo;
	struct ttm_bo_device *bdev;
	int ret;

	bo = ttm_buffer_object_lookup(tfile, req->handle);
	if (unlikely(bo == NULL)) {
		printk(KERN_ERR
		       "Could not find buffer object for setstatus.\n");
		return -EINVAL;
	}

	bdev = bo->bdev;

	ret = ttm_read_lock(lock, true);
	if (unlikely(ret != 0))
		goto out_err0;

	ret = ttm_bo_reserve(bo, 1, 0, 0, 0);
	if (unlikely(ret != 0))
		goto out_err1;

	ret = ttm_bo_wait_cpu(bo, 0);
	if (unlikely(ret != 0))
		goto out_err2;

	mutex_lock(&bo->mutex);
	ret = ttm_bo_check_placement(bo, req->set_placement,
				     req->clr_placement);
	if (unlikely(ret != 0))
		goto out_err2;

	bo->proposed_flags = (bo->proposed_flags | req->set_placement)
	    & ~req->clr_placement;
	ret = ttm_buffer_object_validate(bo, 1, 0);
	if (unlikely(ret != 0))
		goto out_err2;

	ttm_pl_fill_rep(bo, rep);
      out_err2:
	mutex_unlock(&bo->mutex);
	ttm_bo_unreserve(bo);
      out_err1:
	ttm_read_unlock(lock);
      out_err0:
	ttm_bo_unref(&bo);
	return ret;
}

int ttm_pl_waitidle_ioctl(struct ttm_object_file *tfile, void *data)
{
	struct ttm_pl_waitidle_arg *arg = data;
	struct ttm_buffer_object *bo;
	int ret;

	bo = ttm_buffer_object_lookup(tfile, arg->handle);
	if (unlikely(bo == NULL)) {
		printk(KERN_ERR "Could not find buffer object for waitidle.\n");
		return -EINVAL;
	}

	ret =
	    ttm_bo_block_reservation(bo, 1,
				     arg->mode & TTM_PL_WAITIDLE_MODE_NO_BLOCK);
	if (unlikely(ret != 0))
		goto out;
	mutex_lock(&bo->mutex);
	ret = ttm_bo_wait(bo,
			  arg->mode & TTM_PL_WAITIDLE_MODE_LAZY,
			  1, arg->mode & TTM_PL_WAITIDLE_MODE_NO_BLOCK);
	mutex_unlock(&bo->mutex);
	ttm_bo_unblock_reservation(bo);
      out:
	ttm_bo_unref(&bo);
	return ret;
}

int ttm_pl_verify_access(struct ttm_buffer_object *bo,
			 struct ttm_object_file *tfile)
{
	struct ttm_bo_user_object *ubo;

	/*
	 * Check bo subclass.
	 */

	if (unlikely(bo->destroy != &ttm_bo_user_destroy))
		return -EPERM;

	ubo = container_of(bo, struct ttm_bo_user_object, bo);
	if (likely(ubo->base.shareable || ubo->base.tfile == tfile))
		return 0;

	return -EPERM;
}
