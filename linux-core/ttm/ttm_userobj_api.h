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

#ifndef _TTM_USEROBJ_API_H_
#define _TTM_USEROBJ_API_H_

#include "ttm/ttm_placement_user.h"
#include "ttm/ttm_fence_user.h"
#include "ttm/ttm_object.h"
#include "ttm/ttm_fence_api.h"
#include "ttm/ttm_bo_api.h"

struct ttm_lock;

/*
 * User ioctls.
 */

extern int ttm_pl_create_ioctl(struct ttm_object_file *tfile,
			       struct ttm_bo_device *bdev,
			       struct ttm_lock *lock, void *data);
extern int ttm_pl_ub_create_ioctl(struct ttm_object_file *tfile,
				  struct ttm_bo_device *bdev,
				  struct ttm_lock *lock, void *data);
extern int ttm_pl_reference_ioctl(struct ttm_object_file *tfile, void *data);
extern int ttm_pl_unref_ioctl(struct ttm_object_file *tfile, void *data);
extern int ttm_pl_synccpu_ioctl(struct ttm_object_file *tfile, void *data);
extern int ttm_pl_setstatus_ioctl(struct ttm_object_file *tfile,
				  struct ttm_lock *lock, void *data);
extern int ttm_pl_waitidle_ioctl(struct ttm_object_file *tfile, void *data);
extern int ttm_fence_signaled_ioctl(struct ttm_object_file *tfile, void *data);
extern int ttm_fence_finish_ioctl(struct ttm_object_file *tfile, void *data);
extern int ttm_fence_unref_ioctl(struct ttm_object_file *tfile, void *data);

extern int
ttm_fence_user_create(struct ttm_fence_device *fdev,
		      struct ttm_object_file *tfile,
		      uint32_t fence_class,
		      uint32_t fence_types,
		      uint32_t create_flags,
		      struct ttm_fence_object **fence, uint32_t * user_handle);

extern struct ttm_buffer_object *ttm_buffer_object_lookup(struct ttm_object_file
							  *tfile,
							  uint32_t handle);

extern int
ttm_pl_verify_access(struct ttm_buffer_object *bo,
		     struct ttm_object_file *tfile);
#endif
