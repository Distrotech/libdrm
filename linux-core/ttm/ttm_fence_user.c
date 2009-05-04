/**************************************************************************
 *
 * Copyright (c) 2006-2008 Tungsten Graphics, Inc., Cedar Park, TX., USA
 * All Rights Reserved.
 * Copyright (c) 2009 VMware, Inc., Palo Alto, CA., USA
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
#include "ttm/ttm_fence_user.h"
#include "ttm/ttm_object.h"
#include "ttm/ttm_fence_driver.h"
#include "ttm/ttm_userobj_api.h"

/**
 * struct ttm_fence_user_object
 *
 * @base:    The base object used for user-space visibility and refcounting.
 *
 * @fence:   The fence object itself.
 *
 */

struct ttm_fence_user_object {
	struct ttm_base_object base;
	struct ttm_fence_object fence;
};

static struct ttm_fence_user_object *ttm_fence_user_object_lookup(struct
								  ttm_object_file
								  *tfile,
								  uint32_t
								  handle)
{
	struct ttm_base_object *base;

	base = ttm_base_object_lookup(tfile, handle);
	if (unlikely(base == NULL)) {
		printk(KERN_ERR TTM_PFX "Invalid fence handle 0x%08lx\n",
		       (unsigned long)handle);
		return NULL;
	}

	if (unlikely(base->object_type != ttm_fence_type)) {
		ttm_base_object_unref(&base);
		printk(KERN_ERR TTM_PFX "Invalid fence handle 0x%08lx\n",
		       (unsigned long)handle);
		return NULL;
	}

	return container_of(base, struct ttm_fence_user_object, base);
}

/*
 * The fence object destructor.
 */

static void ttm_fence_user_destroy(struct ttm_fence_object *fence)
{
	struct ttm_fence_user_object *ufence =
	    container_of(fence, struct ttm_fence_user_object, fence);

	ttm_mem_global_free(fence->fdev->mem_glob, sizeof(*ufence), false);
	kfree(ufence);
}

/*
 * The base object destructor. We basically unly unreference the
 * attached fence object.
 */

static void ttm_fence_user_release(struct ttm_base_object **p_base)
{
	struct ttm_fence_user_object *ufence;
	struct ttm_base_object *base = *p_base;
	struct ttm_fence_object *fence;

	*p_base = NULL;

	if (unlikely(base == NULL))
		return;

	ufence = container_of(base, struct ttm_fence_user_object, base);
	fence = &ufence->fence;
	ttm_fence_object_unref(&fence);
}

int
ttm_fence_user_create(struct ttm_fence_device *fdev,
		      struct ttm_object_file *tfile,
		      uint32_t fence_class,
		      uint32_t fence_types,
		      uint32_t create_flags,
		      struct ttm_fence_object **fence, uint32_t * user_handle)
{
	int ret;
	struct ttm_fence_object *tmp;
	struct ttm_fence_user_object *ufence;

	ret = ttm_mem_global_alloc(fdev->mem_glob, sizeof(*ufence), false, false, false);
	if (unlikely(ret != 0))
		return -ENOMEM;

	ufence = kmalloc(sizeof(*ufence), GFP_KERNEL);
	if (unlikely(ufence == NULL)) {
		ttm_mem_global_free(fdev->mem_glob, sizeof(*ufence), false);
		return -ENOMEM;
	}

	ret = ttm_fence_object_init(fdev,
				    fence_class,
				    fence_types, create_flags,
				    &ttm_fence_user_destroy, &ufence->fence);

	if (unlikely(ret != 0))
		goto out_err0;

	/*
	 * One fence ref is held by the fence ptr we return.
	 * The other one by the base object. Need to up the
	 * fence refcount before we publish this object to
	 * user-space.
	 */

	tmp = ttm_fence_object_ref(&ufence->fence);
	ret = ttm_base_object_init(tfile, &ufence->base,
				   false, ttm_fence_type,
				   &ttm_fence_user_release, NULL);

	if (unlikely(ret != 0))
		goto out_err1;

	*fence = &ufence->fence;
	*user_handle = ufence->base.hash.key;

	return 0;
      out_err1:
	ttm_fence_object_unref(&tmp);
	tmp = &ufence->fence;
	ttm_fence_object_unref(&tmp);
	return ret;
      out_err0:
	ttm_mem_global_free(fdev->mem_glob, sizeof(*ufence), false);
	kfree(ufence);
	return ret;
}

int ttm_fence_signaled_ioctl(struct ttm_object_file *tfile, void *data)
{
	int ret;
	union ttm_fence_signaled_arg *arg = data;
	struct ttm_fence_object *fence;
	struct ttm_fence_info info;
	struct ttm_fence_user_object *ufence;
	struct ttm_base_object *base;
	ret = 0;

	ufence = ttm_fence_user_object_lookup(tfile, arg->req.handle);
	if (unlikely(ufence == NULL))
		return -EINVAL;

	fence = &ufence->fence;

	if (arg->req.flush) {
		ret = ttm_fence_object_flush(fence, arg->req.fence_type);
		if (unlikely(ret != 0))
			goto out;
	}

	info = ttm_fence_get_info(fence);
	arg->rep.signaled_types = info.signaled_types;
	arg->rep.fence_error = info.error;

      out:
	base = &ufence->base;
	ttm_base_object_unref(&base);
	return ret;
}

int ttm_fence_finish_ioctl(struct ttm_object_file *tfile, void *data)
{
	int ret;
	union ttm_fence_finish_arg *arg = data;
	struct ttm_fence_user_object *ufence;
	struct ttm_base_object *base;
	struct ttm_fence_object *fence;
	ret = 0;

	ufence = ttm_fence_user_object_lookup(tfile, arg->req.handle);
	if (unlikely(ufence == NULL))
		return -EINVAL;

	fence = &ufence->fence;

	ret = ttm_fence_object_wait(fence,
				    arg->req.mode & TTM_FENCE_FINISH_MODE_LAZY,
				    true, arg->req.fence_type);
	if (likely(ret == 0)) {
		struct ttm_fence_info info = ttm_fence_get_info(fence);

		arg->rep.signaled_types = info.signaled_types;
		arg->rep.fence_error = info.error;
	}

	base = &ufence->base;
	ttm_base_object_unref(&base);

	return ret;
}

int ttm_fence_unref_ioctl(struct ttm_object_file *tfile, void *data)
{
	struct ttm_fence_unref_arg *arg = data;
	int ret = 0;

	ret = ttm_ref_object_base_unref(tfile, arg->handle, ttm_fence_type);
	return ret;
}
