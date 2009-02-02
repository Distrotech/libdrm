/*
 * Copyright (C) 2008 Ben Skeggs.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */
#include "drmP.h"
#include "drm.h"

#include "nouveau_drv.h"
#include "nouveau_drm.h"
#include "nouveau_dma.h"

#define nouveau_gem_pushbuf_sync(chan) 0

int
nouveau_gem_object_new(struct drm_gem_object *gem)
{
	struct nouveau_gem_object *ngem;

	ngem = drm_calloc(1, sizeof(*ngem), DRM_MEM_DRIVER);
	if (!ngem)
		return -ENOMEM;
	ngem->gem = gem;

	INIT_LIST_HEAD(&ngem->entry);

	gem->driver_private = ngem;
	return 0;
}

void
nouveau_gem_object_del(struct drm_gem_object *gem)
{
	struct nouveau_gem_object *ngem = gem->driver_private;

	if (ngem->bo) {
		drm_bo_takedown_vm_locked(ngem->bo);
		drm_bo_usage_deref_locked(&ngem->bo);
	}

	drm_free(ngem, sizeof(*ngem), DRM_MEM_DRIVER);
}

int
nouveau_gem_new(struct drm_device *dev, struct nouveau_channel *chan,
		int size, int align, uint32_t domain,
		struct drm_gem_object **pgem)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gem_object *ngem;
	struct drm_gem_object *gem;
	uint64_t flags;
	int ret;

	flags  = DRM_BO_FLAG_READ | DRM_BO_FLAG_WRITE;
	flags |= DRM_BO_FLAG_MAPPABLE;
	flags |= DRM_BO_FLAG_MEM_LOCAL;

	size = (size + (PAGE_SIZE-1)) & ~(PAGE_SIZE-1);
	if (dev_priv->card_type == NV_50) {
		size = (size + 65535) & ~65535;
		if (align < (65536 / PAGE_SIZE))
			align = (65536 / PAGE_SIZE);

		if (domain & NOUVEAU_GEM_DOMAIN_TILE) {
			flags |= DRM_NOUVEAU_BO_FLAG_TILE;
			if (domain & NOUVEAU_GEM_DOMAIN_TILE_ZETA)
				flags |= DRM_NOUVEAU_BO_FLAG_ZTILE;
		}
	}

	gem = drm_gem_object_alloc(dev, size);
	if (!gem)
		return -ENOMEM;
	ngem = gem->driver_private;

	ret = drm_buffer_object_create(dev, size, drm_bo_type_device, flags,
				       0, align, 0, &ngem->bo);
	if (ret)
		goto out;

	if (chan &&
	    (domain & (NOUVEAU_GEM_DOMAIN_VRAM | NOUVEAU_GEM_DOMAIN_GART))) {
		flags = 0;
		if (domain & NOUVEAU_GEM_DOMAIN_VRAM)
			flags |= DRM_BO_FLAG_MEM_VRAM | DRM_BO_FLAG_MEM_PRIV0;
		if (domain & NOUVEAU_GEM_DOMAIN_GART)
			flags |= DRM_BO_FLAG_MEM_TT;

		ret = drm_bo_do_validate(ngem->bo, flags, DRM_BO_MASK_MEMTYPE,
					 DRM_BO_HINT_DONT_FENCE, chan->id);
	}

out:
	if (ret) {
		mutex_lock(&dev->struct_mutex);
		if (ngem->bo)
			drm_bo_usage_deref_locked(&ngem->bo);
		drm_gem_object_unreference(gem);
		mutex_unlock(&dev->struct_mutex);
		return ret;
	}

	*pgem = gem;
	return 0;
}

int
nouveau_gem_pin(struct drm_gem_object *gem, uint32_t domain)
{
	struct nouveau_gem_object *ngem = gem->driver_private;
	uint64_t flags, mask;
	int ret;

	flags = mask = DRM_BO_FLAG_NO_EVICT;
	if (domain) {
		mask |= DRM_BO_MASK_MEM;
		if (domain & NOUVEAU_GEM_DOMAIN_VRAM)
			flags |= DRM_BO_FLAG_MEM_VRAM;
		if (domain & NOUVEAU_GEM_DOMAIN_GART)
			flags |= DRM_BO_FLAG_MEM_TT;
	}

	ret = drm_bo_do_validate(ngem->bo, flags, mask,
				 DRM_BO_HINT_DONT_FENCE, 0);
	return ret;
}

int
nouveau_gem_ioctl_new(struct drm_device *dev, void *data,
		      struct drm_file *file_priv)
{
	struct drm_nouveau_gem_new *req = data;
	struct nouveau_gem_object *ngem = NULL;
	struct drm_gem_object *gem;
	struct nouveau_channel *chan = NULL;
	int ret = 0;

	NOUVEAU_CHECK_INITIALISED_WITH_RETURN;
	NOUVEAU_CHECK_MM_ENABLED_WITH_RETURN;

	if (req->channel_hint) {
		NOUVEAU_GET_USER_CHANNEL_WITH_RETURN(req->channel_hint,
						     file_priv, chan);
	}

	req->size = roundup(req->size, PAGE_SIZE);

	ret = nouveau_gem_new(dev, chan, req->size, req->align, req->domain,
			      &gem);
	if (ret)
		return ret;
	ngem = gem->driver_private;

	req->offset = ngem->bo->offset;
	if (ngem->bo->mem.mem_type == DRM_BO_MEM_VRAM ||
	    ngem->bo->mem.mem_type == DRM_BO_MEM_PRIV0)
		req->domain = NOUVEAU_GEM_DOMAIN_VRAM;
	else
	if (ngem->bo->mem.mem_type == DRM_BO_MEM_TT)
		req->domain = NOUVEAU_GEM_DOMAIN_GART;
	else
		req->domain = 0;

	ret = drm_gem_handle_create(file_priv, ngem->gem, &req->handle);
	mutex_lock(&dev->struct_mutex);
	drm_gem_object_handle_unreference(ngem->gem);
	mutex_unlock(&dev->struct_mutex);

	if (ret)
		drm_gem_object_unreference(ngem->gem);
	return ret;
}

static int
nouveau_gem_set_domain(struct nouveau_channel *chan, struct drm_gem_object *gem,
		       uint32_t read_domains, uint32_t write_domains,
		       uint32_t valid_domains)
{
	struct nouveau_gem_object *ngem = gem->driver_private;
	struct drm_buffer_object *bo = ngem->bo;
	uint64_t mask = DRM_BO_MASK_MEM | DRM_BO_FLAG_READ | DRM_BO_FLAG_WRITE;
	uint64_t flags;
	int ret;

	if (!valid_domains || (!read_domains && !write_domains))
		return -EINVAL;

	if (write_domains) {
		if ((valid_domains & NOUVEAU_GEM_DOMAIN_VRAM) &&
		    (write_domains & NOUVEAU_GEM_DOMAIN_VRAM))
			flags = (DRM_BO_FLAG_MEM_VRAM | DRM_BO_FLAG_MEM_PRIV0);
		else
		if ((valid_domains & NOUVEAU_GEM_DOMAIN_GART) &&
		    (write_domains & NOUVEAU_GEM_DOMAIN_GART))
			flags = DRM_BO_FLAG_MEM_TT;
		else
			return -EINVAL;
	} else {
		if ((valid_domains & NOUVEAU_GEM_DOMAIN_VRAM) &&
		    (read_domains & NOUVEAU_GEM_DOMAIN_VRAM) &&
		    (bo->mem.mem_type == DRM_BO_MEM_VRAM ||
		     bo->mem.mem_type == DRM_BO_MEM_PRIV0))
			flags = (DRM_BO_FLAG_MEM_VRAM | DRM_BO_FLAG_MEM_PRIV0);
		else
		if ((valid_domains & NOUVEAU_GEM_DOMAIN_GART) &&
		    (read_domains & NOUVEAU_GEM_DOMAIN_GART) &&
		    bo->mem.mem_type == DRM_BO_MEM_TT)
			flags = DRM_BO_FLAG_MEM_TT;
		else
		if ((valid_domains & NOUVEAU_GEM_DOMAIN_VRAM) &&
		    (read_domains & NOUVEAU_GEM_DOMAIN_VRAM))
			flags = (DRM_BO_FLAG_MEM_VRAM | DRM_BO_FLAG_MEM_PRIV0);
		else
			flags = DRM_BO_FLAG_MEM_TT;
	}

	if (read_domains)
		flags |= DRM_BO_FLAG_READ;

	if (write_domains)
		flags |= DRM_BO_FLAG_WRITE;

	ret = drm_bo_do_validate(ngem->bo, flags, mask, 0, chan->id);
	if (ret)
		return ret;

	return 0;
}

static int
nouveau_gem_pushbuf_bo_validate(struct nouveau_channel *chan,
				struct drm_file *file_priv,
				struct drm_nouveau_gem_pushbuf_bo *b,
				uint64_t user_bo, int nr_buffers,
				struct drm_fence_object **fence,
				int *apply_relocs)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gem_object *ngem;
	struct list_head validated, *entry, *tmp;
	int ret, i;

	INIT_LIST_HEAD(&validated);

	ret = drm_fence_object_create(dev, chan->id, DRM_FENCE_TYPE_EXE,
				      0, fence);
	if (ret)
		return ret;

	if (nr_buffers == 0)
		return 0;

	if (apply_relocs)
		*apply_relocs = 0;

	mutex_lock(&dev_priv->submit_mutex);

	for (i = 0; i < nr_buffers; i++, b++) {
		struct nouveau_gem_object *ngem;
		struct drm_gem_object *gem;

		gem = drm_gem_object_lookup(dev, file_priv, b->handle);
		if (!gem) {
			DRM_ERROR("Unknown handle 0x%08x\n", b->handle);
			ret = -EINVAL;
			break;
		}
		ngem = gem->driver_private;

		ret = nouveau_gem_set_domain(chan, gem, b->read_domains,
					     b->write_domains,
					     b->valid_domains);
		if (ret)
			break;

		list_add_tail(&ngem->entry, &validated);

		if (ngem->bo->offset == b->presumed_offset &&
		    (((ngem->bo->mem.mem_type == DRM_BO_MEM_VRAM ||
		       ngem->bo->mem.mem_type == DRM_BO_MEM_PRIV0) &&
		      b->presumed_domain & NOUVEAU_GEM_DOMAIN_VRAM) ||
		     (ngem->bo->mem.mem_type == DRM_BO_MEM_TT &&
		      b->presumed_domain & NOUVEAU_GEM_DOMAIN_GART)))
			continue;

		if (ngem->bo->mem.mem_type == DRM_BO_MEM_TT)
			b->presumed_domain = NOUVEAU_GEM_DOMAIN_GART;
		else
			b->presumed_domain = NOUVEAU_GEM_DOMAIN_VRAM;
		b->presumed_offset = ngem->bo->offset;
		b->presumed_ok = 0;
		if (apply_relocs)
			(*apply_relocs)++;

		if (DRM_COPY_TO_USER((void __user *)user_bo + (i * sizeof(*b)),
				     b, sizeof(*b))) {
			ret = -EFAULT;
			break;
		}
	}

	mutex_lock(&dev->struct_mutex);
	list_for_each_safe(entry, tmp, &validated) {
		ngem = list_entry(entry, struct nouveau_gem_object, entry);
		drm_gem_object_unreference(ngem->gem);
		list_del(&ngem->entry);
	}
	mutex_unlock(&dev->struct_mutex);

	if (!ret)
		ret = drm_fence_buffer_objects(dev, NULL, 0, *fence, fence);

	if (ret) {
		drm_putback_buffer_objects(dev);
		drm_fence_usage_deref_unlocked(fence);
	}

	mutex_unlock(&dev_priv->submit_mutex);
	return ret;
}

static int
nouveau_gem_pushbuf_reloc_apply(struct drm_nouveau_gem_pushbuf_reloc *reloc,
				struct drm_nouveau_gem_pushbuf_bo *bo,
				uint32_t *pushbuf, int nr_relocs,
				int nr_buffers, int nr_dwords)
{
	int i;
	
	for (i = 0; i < nr_relocs; i++) {
		struct drm_nouveau_gem_pushbuf_reloc *r = &reloc[i];
		struct drm_nouveau_gem_pushbuf_bo *b;
		uint32_t data;

		if (r->bo_index >= nr_buffers || r->reloc_index >= nr_dwords) {
			DRM_ERROR("Bad relocation %d\n", i);
			DRM_ERROR("  bo: %d max %d\n", r->bo_index, nr_buffers);
			DRM_ERROR("  id: %d max %d\n", r->reloc_index, nr_dwords);
			return -EINVAL;
		}

		b = &bo[r->bo_index];
		if (b->presumed_ok)
			continue;

		if (r->flags & NOUVEAU_GEM_RELOC_LOW)
			data = b->presumed_offset + r->data;
		else
		if (r->flags & NOUVEAU_GEM_RELOC_HIGH)
			data = (b->presumed_offset + r->data) >> 32;
		else
			data = r->data;

		if (r->flags & NOUVEAU_GEM_RELOC_OR) {
			if (b->presumed_domain == NOUVEAU_GEM_DOMAIN_GART)
				data |= r->tor;
			else
				data |= r->vor;
		}

		pushbuf[r->reloc_index] = data;
	}

	return 0;
}

static inline void *
u_memcpya(uint64_t user, unsigned nmemb, unsigned size)
{
	void *mem;

	mem = drm_alloc(nmemb * size, DRM_MEM_DRIVER);
	if (!mem)
		return (void *)-ENOMEM;

	if (DRM_COPY_FROM_USER(mem, (void __user *)user, nmemb * size)) {
		drm_free(mem, nmemb * size, DRM_MEM_DRIVER);
		return (void *)-EFAULT;
	}

	return mem;
}

int
nouveau_gem_ioctl_pushbuf(struct drm_device *dev, void *data,
			  struct drm_file *file_priv)
{
	struct drm_nouveau_gem_pushbuf *req = data;
	struct drm_nouveau_gem_pushbuf_bo *bo = NULL;
	struct drm_nouveau_gem_pushbuf_reloc *reloc = NULL;
	struct drm_fence_object *fence = NULL;
	struct nouveau_channel *chan;
	uint32_t *pushbuf = NULL;
	int ret = 0, i;

	NOUVEAU_CHECK_INITIALISED_WITH_RETURN;
	NOUVEAU_CHECK_MM_ENABLED_WITH_RETURN;
	NOUVEAU_GET_USER_CHANNEL_WITH_RETURN(req->channel, file_priv, chan);

	if (req->nr_dwords >= chan->dma.max ||
	    req->nr_buffers > NOUVEAU_GEM_MAX_BUFFERS ||
	    req->nr_relocs > NOUVEAU_GEM_MAX_RELOCS) {
		DRM_ERROR("Pushbuf config exceeds limits:\n");
		DRM_ERROR("  dwords : %d max %d\n", req->nr_dwords,
			  chan->dma.max - 1);
		DRM_ERROR("  buffers: %d max %d\n", req->nr_buffers,
			  NOUVEAU_GEM_MAX_BUFFERS);
		DRM_ERROR("  relocs : %d max %d\n", req->nr_relocs,
			  NOUVEAU_GEM_MAX_RELOCS);
		return -EINVAL;
	}

	pushbuf = u_memcpya(req->dwords, req->nr_dwords, sizeof(uint32_t));
	if (IS_ERR(pushbuf))
		return (unsigned long)pushbuf;

	bo = u_memcpya(req->buffers, req->nr_buffers, sizeof(*bo));
	if (IS_ERR(bo)) {
		drm_free(pushbuf, req->nr_dwords * sizeof(uint32_t),
			 DRM_MEM_DRIVER);
		return (unsigned long)bo;
	}

	reloc = u_memcpya(req->relocs, req->nr_relocs, sizeof(*reloc));
	if (IS_ERR(reloc)) {
		drm_free(bo, req->nr_buffers * sizeof(*bo), DRM_MEM_DRIVER);
		drm_free(pushbuf, req->nr_dwords * sizeof(uint32_t),
			 DRM_MEM_DRIVER);
		return (unsigned long)reloc;
	}

	/* Validate buffer list */
	ret = nouveau_gem_pushbuf_bo_validate(chan, file_priv, bo,
					      req->buffers, req->nr_buffers,
					      &fence, NULL);
	if (ret)
		goto out;

	/* Apply any relocations that are required */
	ret = nouveau_gem_pushbuf_reloc_apply(reloc, bo, pushbuf,
					      req->nr_relocs, req->nr_buffers,
					      req->nr_dwords);
	if (ret)
		goto out;

	/* Emit push buffer to the hw
	 *XXX: OMG ALSO YUCK!!!
	 */
	ret = RING_SPACE(chan, req->nr_dwords);
	if (ret)
		goto out;

	for (i = 0; i < req->nr_dwords; i++)
		OUT_RING (chan, pushbuf[i]);

	ret = drm_fence_object_emit(fence, 0, chan->id,
				    DRM_FENCE_TYPE_EXE);
	if (ret) {
		/*XXX*/
	}

	if (nouveau_gem_pushbuf_sync(chan)) {
		ret = drm_fence_object_wait(fence, 0, 1, 1);
		if (ret) {
			for (i = 0; i < req->nr_dwords; i++)
				DRM_ERROR("0x%08x\n", pushbuf[i]);
			DRM_ERROR("^^ above push buffer is fail :(\n");
		}
	}

	FIRE_RING(chan);

out:
	if (fence)
		drm_fence_usage_deref_unlocked(&fence);

	drm_free(pushbuf, req->nr_dwords * sizeof(uint32_t), DRM_MEM_DRIVER);
	drm_free(bo, req->nr_buffers * sizeof(uint32_t), DRM_MEM_DRIVER);
	drm_free(reloc, req->nr_relocs * sizeof(uint32_t), DRM_MEM_DRIVER);

	return ret;
}

int
nouveau_gem_ioctl_pushbuf_call(struct drm_device *dev, void *data,
			       struct drm_file *file_priv)
{
	return -ENODEV;
}

int
nouveau_gem_ioctl_pin(struct drm_device *dev, void *data,
		      struct drm_file *file_priv)
{
	struct drm_nouveau_gem_pin *req = data;
	struct nouveau_gem_object *ngem;
	struct drm_gem_object *gem;
	int ret = 0;

	NOUVEAU_CHECK_INITIALISED_WITH_RETURN;
	NOUVEAU_CHECK_MM_ENABLED_WITH_RETURN;

	gem = drm_gem_object_lookup(dev, file_priv, req->handle);
	if (!gem)
		return -EINVAL;
	ngem = gem->driver_private;

	ret = nouveau_gem_pin(gem, req->domain);
	if (ret)
		goto out;

	req->offset = ngem->bo->offset;
	req->domain = 0;
	if (ngem->bo->mem.mem_type == DRM_BO_MEM_VRAM ||
	    ngem->bo->mem.mem_type == DRM_BO_MEM_PRIV0)
		req->domain |= NOUVEAU_GEM_DOMAIN_VRAM;
	else
	if (ngem->bo->mem.mem_type == DRM_BO_MEM_TT)
		req->domain |= NOUVEAU_GEM_DOMAIN_GART;

out:
	mutex_lock(&dev->struct_mutex);
	drm_gem_object_unreference(gem);
	mutex_unlock(&dev->struct_mutex);

	return ret;
}

int
nouveau_gem_ioctl_unpin(struct drm_device *dev, void *data,
			struct drm_file *file_priv)
{
	struct drm_nouveau_gem_pin *req = data;
	struct nouveau_gem_object *ngem;
	struct drm_gem_object *gem;
	int ret;

	NOUVEAU_CHECK_INITIALISED_WITH_RETURN;
	NOUVEAU_CHECK_MM_ENABLED_WITH_RETURN;

	gem = drm_gem_object_lookup(dev, file_priv, req->handle);
	if (!gem)
		return -EINVAL;
	ngem = gem->driver_private;

	ret = drm_bo_do_validate(ngem->bo, 0, DRM_BO_FLAG_NO_EVICT,
				 DRM_BO_HINT_DONT_FENCE, 0);

	mutex_lock(&dev->struct_mutex);
	drm_gem_object_unreference(gem);
	mutex_unlock(&dev->struct_mutex);

	return ret;
}

int
nouveau_gem_ioctl_mmap(struct drm_device *dev, void *data,
		       struct drm_file *file_priv)
{
	struct drm_nouveau_gem_mmap *req = data;
	struct nouveau_gem_object *ngem;
	struct drm_gem_object *gem;
	unsigned long addr;
	int ret;

	NOUVEAU_CHECK_INITIALISED_WITH_RETURN;
	NOUVEAU_CHECK_MM_ENABLED_WITH_RETURN;

	gem = drm_gem_object_lookup(dev, file_priv, req->handle);
	if (!gem)
		return -EINVAL;
	ngem = gem->driver_private;

	if (ngem->bo->mem.mem_type == DRM_BO_MEM_LOCAL) {
		ret = drm_bo_do_validate(ngem->bo, DRM_BO_FLAG_MEM_TT,
					 DRM_BO_MASK_MEMTYPE,
					 DRM_BO_HINT_DONT_FENCE, 0);
		if (ret) {
			mutex_lock(&dev->struct_mutex);
			drm_gem_object_unreference(gem);
			mutex_unlock(&dev->struct_mutex);
			return ret;
		}
	}

	down_write(&current->mm->mmap_sem);
	addr = do_mmap_pgoff(file_priv->filp, 0, ngem->bo->mem.size,
			     PROT_READ | PROT_WRITE, MAP_SHARED,
			     ngem->bo->map_list.hash.key);
	up_write(&current->mm->mmap_sem);

	mutex_lock(&dev->struct_mutex);
	drm_gem_object_unreference(gem);
	mutex_unlock(&dev->struct_mutex);

	if (IS_ERR((void *)addr))
		return addr;
	req->vaddr = (uint64_t)addr;

	return 0;
}

int
nouveau_gem_ioctl_cpu_prep(struct drm_device *dev, void *data,
			   struct drm_file *file_priv)
{
	struct drm_nouveau_gem_mmap *req = data;
	struct nouveau_gem_object *ngem;
	struct drm_gem_object *gem;
	int ret;

	NOUVEAU_CHECK_INITIALISED_WITH_RETURN;
	NOUVEAU_CHECK_MM_ENABLED_WITH_RETURN;

	gem = drm_gem_object_lookup(dev, file_priv, req->handle);
	if (!gem)
		return -EINVAL;
	ngem = gem->driver_private;

	mutex_lock(&ngem->bo->mutex);
	ret = drm_bo_wait(ngem->bo, 0, 0, 0, 0);
	mutex_unlock(&ngem->bo->mutex);

	mutex_lock(&dev->struct_mutex);
	drm_gem_object_unreference(gem);
	mutex_unlock(&dev->struct_mutex);

	return ret;
}

int
nouveau_gem_ioctl_cpu_fini(struct drm_device *dev, void *data,
			   struct drm_file *file_priv)
{
	NOUVEAU_CHECK_INITIALISED_WITH_RETURN;
	NOUVEAU_CHECK_MM_ENABLED_WITH_RETURN;

	return 0;
}

int
nouveau_gem_ioctl_tile(struct drm_device *dev, void *data,
			   struct drm_file *file_priv)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct drm_nouveau_gem_tile *req = data;
	struct nouveau_gem_object *ngem;
	struct drm_gem_object *gem;

	NOUVEAU_CHECK_INITIALISED_WITH_RETURN;
	NOUVEAU_CHECK_MM_ENABLED_WITH_RETURN;

	gem = drm_gem_object_lookup(dev, file_priv, req->handle);
	if (!gem)
		return -EINVAL;
	ngem = gem->driver_private;

	{
		struct nouveau_gpuobj *pt = dev_priv->vm_vram_pt;
		unsigned offset = ngem->bo->offset + req->delta;
		unsigned count = req->size / 65536;
		unsigned tile = 0;
		
		offset -= dev_priv->vm_vram_base;

		if (req->flags & NOUVEAU_MEM_TILE) {
			if (req->flags & NOUVEAU_MEM_TILE_ZETA)
				tile = 0x00002800;
			else
				tile = 0x00007000;
		}

		while (count--) {
			unsigned pte = offset / 65536;

			INSTANCE_WR(pt, (pte * 2) + 0, offset | 1);
			INSTANCE_WR(pt, (pte * 2) + 1, 0x00000000 | tile);
			offset += 65536;
		}
	}

	mutex_lock(&dev->struct_mutex);
	drm_gem_object_unreference(gem);
	mutex_unlock(&dev->struct_mutex);

	return 0;
}

