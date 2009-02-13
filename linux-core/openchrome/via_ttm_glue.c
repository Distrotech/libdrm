/*
 * Copyright (c) 2007-2008 Tungsten Graphics, Inc., Cedar Park, TX., USA,
 * All Rights Reserved.
 * Copyright (c) 2009 VMware, Inc., Palo Alto, CA., USA,
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
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
 */

#include "drmP.h"
#include "via_drv.h"
#include "ttm/ttm_userobj_api.h"

static struct vm_operations_struct via_ttm_vm_ops;
static struct vm_operations_struct *ttm_vm_ops = NULL;

int via_fence_signaled_ioctl(struct drm_device *dev, void *data,
			     struct drm_file *file_priv)
{
	return ttm_fence_signaled_ioctl(via_fpriv(file_priv)->tfile, data);
}

int via_fence_finish_ioctl(struct drm_device *dev, void *data,
			   struct drm_file *file_priv)
{
	return ttm_fence_finish_ioctl(via_fpriv(file_priv)->tfile, data);
}

int via_fence_unref_ioctl(struct drm_device *dev, void *data,
			  struct drm_file *file_priv)
{

	return ttm_fence_unref_ioctl(via_fpriv(file_priv)->tfile, data);
}

int via_pl_waitidle_ioctl(struct drm_device *dev, void *data,
			  struct drm_file *file_priv)
{
	return ttm_pl_waitidle_ioctl(via_fpriv(file_priv)->tfile, data);
}

int via_pl_setstatus_ioctl(struct drm_device *dev, void *data,
			   struct drm_file *file_priv)
{
	return ttm_pl_setstatus_ioctl(via_fpriv(file_priv)->tfile,
				      &via_priv(dev)->ttm_lock, data);
}

int via_pl_synccpu_ioctl(struct drm_device *dev, void *data,
			 struct drm_file *file_priv)
{
	return ttm_pl_synccpu_ioctl(via_fpriv(file_priv)->tfile, data);
}

int via_pl_unref_ioctl(struct drm_device *dev, void *data,
		       struct drm_file *file_priv)
{
	return ttm_pl_unref_ioctl(via_fpriv(file_priv)->tfile, data);
}

int via_pl_reference_ioctl(struct drm_device *dev, void *data,
			   struct drm_file *file_priv)
{
	return ttm_pl_reference_ioctl(via_fpriv(file_priv)->tfile, data);
}

int via_pl_create_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv)
{
	struct drm_via_private *dev_priv = via_priv(dev);

	return ttm_pl_create_ioctl(via_fpriv(file_priv)->tfile,
				   &dev_priv->bdev, &dev_priv->ttm_lock, data);
}

/**
 * psb_ttm_fault - Wrapper around the ttm fault method.
 *
 * @vma: The struct vm_area_struct as in the vm fault() method.
 * @vmf: The struct vm_fault as in the vm fault() method.
 *
 * Since ttm_fault() will reserve buffers while faulting,
 * we need to take the ttm read lock around it, as this driver
 * relies on the ttm_lock in write mode to exclude all threads from
 * reserving and thus validating buffers in aperture- and memory shortage
 * situations.
 */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,26))
static int via_ttm_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct ttm_buffer_object *bo = (struct ttm_buffer_object *)
	    vma->vm_private_data;
	struct drm_via_private *dev_priv =
	    container_of(bo->bdev, struct drm_via_private, bdev);
	int ret;

	ret = ttm_read_lock(&dev_priv->ttm_lock, true);
	if (unlikely(ret != 0))
		return VM_FAULT_NOPAGE;

	ret = ttm_vm_ops->fault(vma, vmf);

	ttm_read_unlock(&dev_priv->ttm_lock);

	return ret;
}

#else

static unsigned long via_ttm_nopfn(struct vm_area_struct *vma, unsigned long address)
{
	struct ttm_buffer_object *bo = (struct ttm_buffer_object *)
	    vma->vm_private_data;
	struct drm_via_private *dev_priv =
	    container_of(bo->bdev, struct drm_via_private, bdev);
	int ret;

	ret = ttm_read_lock(&dev_priv->ttm_lock, true);
	if (unlikely(ret != 0))
		return NOPFN_REFAULT;

	ret = ttm_vm_ops->nopfn(vma, address);

	ttm_read_unlock(&dev_priv->ttm_lock);

	return ret;
}
#endif

int via_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct drm_file *file_priv;
	struct drm_via_private *dev_priv;
	int ret;

	if (unlikely(vma->vm_pgoff < DRM_FILE_PAGE_OFFSET))
		return drm_mmap(filp, vma);

	file_priv = (struct drm_file *)filp->private_data;
	dev_priv = via_priv(file_priv->minor->dev);
	ret = ttm_bo_mmap(filp, vma, &dev_priv->bdev);

	if (unlikely(ret != 0))
		return ret;

	if (unlikely(ttm_vm_ops == NULL)) {
		ttm_vm_ops = vma->vm_ops;
		via_ttm_vm_ops = *ttm_vm_ops;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,26))
		via_ttm_vm_ops.fault = &via_ttm_fault;
#else
		via_ttm_vm_ops.nopfn = &via_ttm_nopfn;
#endif
	}

	vma->vm_ops = &via_ttm_vm_ops;
	return 0;
}

ssize_t via_ttm_write(struct file * filp, const char __user * buf,
		      size_t count, loff_t * f_pos)
{
	struct drm_file *file_priv = (struct drm_file *)filp->private_data;
	struct drm_via_private *dev_priv = via_priv(file_priv->minor->dev);

	return ttm_bo_io(&dev_priv->bdev, filp, buf, NULL, count, f_pos, true);
}

ssize_t via_ttm_read(struct file * filp, char __user * buf,
		     size_t count, loff_t * f_pos)
{
	struct drm_file *file_priv = (struct drm_file *)filp->private_data;
	struct drm_via_private *dev_priv = via_priv(file_priv->minor->dev);

	return ttm_bo_io(&dev_priv->bdev, filp, NULL, buf, count, f_pos, false);
}

int via_verify_access(struct ttm_buffer_object *bo, struct file *filp)
{
	struct drm_file *file_priv = (struct drm_file *)filp->private_data;

	return ttm_pl_verify_access(bo, via_fpriv(file_priv)->tfile);
}

static int via_ttm_mem_global_init(struct drm_global_reference *ref)
{
	return ttm_mem_global_init(ref->object);
}

static void via_ttm_mem_global_release(struct drm_global_reference *ref)
{
	ttm_mem_global_release(ref->object);
}

int via_ttm_global_init(struct drm_via_private *dev_priv)
{
	struct drm_global_reference *global_ref;
	int ret;

	global_ref = &dev_priv->mem_global_ref;
	global_ref->global_type = DRM_GLOBAL_TTM_MEM;
	global_ref->size = sizeof(struct ttm_mem_global);
	global_ref->init = &via_ttm_mem_global_init;
	global_ref->release = &via_ttm_mem_global_release;

	ret = drm_global_item_ref(global_ref);
	if (unlikely(ret != 0)) {
		DRM_ERROR("Failed referencing a global TTM memory object.\n");
		return ret;
	}

	return 0;
}

void via_ttm_global_release(struct drm_via_private *dev_priv)
{
	drm_global_item_unref(&dev_priv->mem_global_ref);
}
