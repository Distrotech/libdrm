/* radeon_bufs.c -- IOCTLs to manage buffers -*- linux-c -*-
 *
 * Copyright 2000 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Fremont, California.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors: Kevin E. Martin <martin@valinux.com>
 *          Rickard E. (Rik) Faith <faith@valinux.com>
 *	    Jeff Hartmann <jhartmann@valinux.com>
 *
 */

#define __NO_VERSION__
#ifdef __linux__
#include <linux/config.h>
#endif
#include "radeon.h"
#include "drmP.h"
#include "radeon_drv.h"
#ifdef __linux__
#include "linux/un.h"
#endif
#ifdef __FreeBSD__
#include <machine/param.h>
#include <sys/mman.h>
#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_extern.h>
#include <vm/vm_map.h>
#endif


#if defined(CONFIG_AGP) || defined(CONFIG_AGP_MODULE)
int radeon_addbufs_agp(struct inode *inode, struct file *filp,
		       unsigned int cmd, unsigned long arg)
{
	drm_file_t       *priv = filp->private_data;
	drm_device_t     *dev  = priv->dev;
	drm_device_dma_t *dma  = dev->dma;
	drm_buf_desc_t    request;
	drm_buf_entry_t  *entry;
	drm_buf_t        *buf;
	unsigned long     offset;
	unsigned long     agp_offset;
	int               count;
	int               order;
	int               size;
	int               alignment;
	int               page_order;
	int               total;
	int               byte_count;
	int               i;

	if (!dma) return DRM_OS_RETURN(EINVAL);

	DRM_OS_KRNFROMUSR( request, (drm_buf_desc_t *)data, sizeof(request) );

	count      = request.count;
	order      = drm_order(request.size);
	size       = 1 << order;

	alignment  = (request.flags & _DRM_PAGE_ALIGN) ? PAGE_ALIGN(size):size;
	page_order = order - PAGE_SHIFT > 0 ? order - PAGE_SHIFT : 0;
	total      = PAGE_SIZE << page_order;

	byte_count = 0;
	agp_offset = dev->agp->base + request.agp_start;

	DRM_DEBUG("count:      %d\n",  count);
	DRM_DEBUG("order:      %d\n",  order);
	DRM_DEBUG("size:       %d\n",  size);
	DRM_DEBUG("agp_offset: %ld\n", agp_offset);
	DRM_DEBUG("alignment:  %d\n",  alignment);
	DRM_DEBUG("page_order: %d\n",  page_order);
	DRM_DEBUG("total:      %d\n",  total);

	if (order < DRM_MIN_ORDER || order > DRM_MAX_ORDER)
		DRM_OS_RETURN(EINVAL);
	if (dev->queue_count) 
		DRM_OS_RETURN(EBUSY); /* Not while in use */

	DRM_OS_SPINLOCK(&dev->count_lock);
	if (dev->buf_use) {
		DRM_OS_SPINUNLOCK(&dev->count_lock);
		DRM_OS_RETURN(EBUSY);
	}
	atomic_inc(&dev->buf_alloc);
	DRM_OS_SPINUNLOCK(&dev->count_lock);

	DRM_OS_LOCK;
	entry = &dma->bufs[order];
	if (entry->buf_count) {
		DRM_OS_UNLOCK;
		atomic_dec(&dev->buf_alloc);
		DRM_OS_RETURN(ENOMEM); /* May only call once for each order */
	}

	entry->buflist = drm_alloc(count * sizeof(*entry->buflist),
				   DRM_MEM_BUFS);
	if (!entry->buflist) {
		up(&dev->struct_sem);
		atomic_dec(&dev->buf_alloc);
		DRM_OS_RETURN(ENOMEM);
	}
	memset(entry->buflist, 0, count * sizeof(*entry->buflist));

	entry->buf_size   = size;
	entry->page_order = page_order;
	offset            = 0;

	for (offset = 0;
	     entry->buf_count < count;
	     offset += alignment, ++entry->buf_count) {
		buf          = &entry->buflist[entry->buf_count];
		buf->idx     = dma->buf_count + entry->buf_count;
		buf->total   = alignment;
		buf->order   = order;
		buf->used    = 0;
		buf->offset  = (dma->byte_count + offset);
		buf->address = (void *)(agp_offset + offset);
		buf->next    = NULL;
		buf->waiting = 0;
		buf->pending = 0;
		init_waitqueue_head(&buf->dma_wait);
		buf->pid     = 0;

		buf->dev_priv_size = sizeof(drm_radeon_buf_priv_t);
		buf->dev_private   = drm_alloc(sizeof(drm_radeon_buf_priv_t),
					       DRM_MEM_BUFS);
		memset(buf->dev_private, 0, buf->dev_priv_size);

#if DRM_DMA_HISTOGRAM
		buf->time_queued     = 0;
		buf->time_dispatched = 0;
		buf->time_completed  = 0;
		buf->time_freed      = 0;
#endif

		byte_count += PAGE_SIZE << page_order;

		DRM_DEBUG("buffer %d @ %p\n",
			  entry->buf_count, buf->address);
	}

	DRM_DEBUG("byte_count: %d\n", byte_count);

	dma->buflist = drm_realloc(dma->buflist,
				   dma->buf_count * sizeof(*dma->buflist),
				   (dma->buf_count + entry->buf_count)
				   * sizeof(*dma->buflist),
				   DRM_MEM_BUFS);
	for (i = dma->buf_count; i < dma->buf_count + entry->buf_count; i++)
		dma->buflist[i] = &entry->buflist[i - dma->buf_count];

	dma->buf_count  += entry->buf_count;
	dma->byte_count += byte_count;

	drm_freelist_create(&entry->freelist, entry->buf_count);
	for (i = 0; i < entry->buf_count; i++) {
		drm_freelist_put(dev, &entry->freelist, &entry->buflist[i]);
	}

	up(&dev->struct_sem);

	request.count = entry->buf_count;
	request.size  = size;

	DRM_OS_KRNTOUSR( (drm_buf_desc_t *)data, request, sizeof(request) );

	dma->flags = _DRM_DMA_USE_AGP;

	atomic_dec(&dev->buf_alloc);
	return 0;
}
#endif

int radeon_addbufs( DRM_OS_IOCTL )
{
	DRM_OS_DEVICE;
	drm_radeon_private_t	*dev_priv	= dev->dev_private;
	drm_buf_desc_t		request;
	DRM_OS_PRIV;

	if (!dev_priv || dev_priv->is_pci) DRM_OS_RETURN(EINVAL);

	DRM_OS_KRNFROMUSR( request, (drm_buf_desc_t *)data, sizeof(request) );

#if defined(CONFIG_AGP) || defined(CONFIG_AGP_MODULE)
	if (request.flags & _DRM_AGP_BUFFER)
		return radeon_addbufs_agp(inode, filp, cmd, arg);
	else
#endif
		DRM_OS_RETURN(EINVAL);
}

int radeon_mapbufs( DRM_OS_IOCTL )
{
	DRM_OS_DEVICE;
	drm_radeon_private_t	*dev_priv	= dev->dev_private;
	drm_device_dma_t	*dma		= dev->dma;
	int			 retcode	= 0;
	const int		 zero		= 0;
#ifdef __linux__
	unsigned long		 virtual;
	unsigned long		 address;
#endif
#ifdef __FreeBSD__
	vm_offset_t              virtual;
	vm_offset_t              address;
#endif
	drm_buf_map_t		 request;
	int			 i;
	DRM_OS_PRIV;

	if (!dma || !dev_priv || dev_priv->is_pci) DRM_OS_RETURN(EINVAL);

	DRM_DEBUG("\n");

	DRM_OS_SPINLOCK(&dev->count_lock);
	if (atomic_read(&dev->buf_alloc)) {
		DRM_OS_SPINUNLOCK(&dev->count_lock);
		DRM_OS_RETURN(EBUSY);
	}
	++dev->buf_use;		/* Can't allocate more after this call */
	DRM_OS_SPINUNLOCK(&dev->count_lock);

	DRM_OS_KRNFROMUSR( request, (drm_buf_map_t *)data, sizeof(request) );

	if (request.count >= dma->buf_count) {
		if (dma->flags & _DRM_DMA_USE_AGP) {
			drm_map_t *map;

			map = dev_priv->buffers;
			if (!map) {
				retcode = EINVAL;
				goto done;
			}

#ifdef __linux__
#if LINUX_VERSION_CODE <= 0x020402
			down( &current->mm->mmap_sem );
#else
			down_write( &current->mm->mmap_sem );
#endif

			virtual = do_mmap( filp, 0, map->size,
					   PROT_READ | PROT_WRITE,
					   MAP_SHARED,
					   (unsigned long)map->offset );
#if LINUX_VERSION_CODE <= 0x020402
			up( &current->mm->mmap_sem );
#else
			up_write( &current->mm->mmap_sem );
#endif
#endif

#ifdef __FreeBSD__
			retcode = vm_mmap(&p->p_vmspace->vm_map,
					  &virtual,
					  round_page(dma->byte_count),
					  PROT_READ|PROT_WRITE, VM_PROT_ALL,
					  MAP_SHARED,
					  SLIST_FIRST(&kdev->si_hlist),
					  (unsigned long)map->offset );
#endif
		} else {
#ifdef __linux__
#if LINUX_VERSION_CODE <= 0x020402
			down( &current->mm->mmap_sem );
#else
			down_write( &current->mm->mmap_sem );
#endif

			virtual = do_mmap( filp, 0, dma->byte_count,
					   PROT_READ | PROT_WRITE,
					   MAP_SHARED, 0 );
#if LINUX_VERSION_CODE <= 0x020402
			up( &current->mm->mmap_sem );
#else
			up_write( &current->mm->mmap_sem );
#endif
#endif
#ifdef __FreeBSD__
			retcode = vm_mmap(&p->p_vmspace->vm_map,
					  &virtual,
					  round_page(dma->byte_count),
					  PROT_READ|PROT_WRITE, VM_PROT_ALL,
					  MAP_SHARED,
					  SLIST_FIRST(&kdev->si_hlist),
					  0);
#endif
		}
#ifdef __linux__
		if ( virtual > -1024UL ) {
			/* Real error */
			retcode = (signed long)virtual;
			goto done;
		}
#endif
#ifdef __FreeBSD__
		if (retcode)
			goto done;
#endif
		request.virtual = (void *)virtual;

		for (i = 0; i < dma->buf_count; i++) {
			if (DRM_OS_COPYTOUSR(&request.list[i].idx,
					 &dma->buflist[i]->idx,
					 sizeof(request.list[0].idx))) {
				retcode = EFAULT;
				goto done;
			}
			if (DRM_OS_COPYTOUSR(&request.list[i].total,
					 &dma->buflist[i]->total,
					 sizeof(request.list[0].total))) {
				retcode = EFAULT;
				goto done;
			}
			if (DRM_OS_COPYTOUSR(&request.list[i].used,
					 &zero,
					 sizeof(zero))) {
				retcode = EFAULT;
				goto done;
			}
			address = virtual + dma->buflist[i]->offset;
			if (DRM_OS_COPYTOUSR(&request.list[i].address,
					 &address,
					 sizeof(address))) {
				retcode = EFAULT;
				goto done;
			}
		}
	}
 done:
	request.count = dma->buf_count;
	DRM_DEBUG("%d buffers, retcode = %d\n", request.count, retcode);

	DRM_OS_KRNTOUSR( (drm_buf_map_t *)data, request, sizeof(request) );

	DRM_OS_RETURN(retcode);
}
