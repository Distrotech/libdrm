/**
 * \file drm_bufs.c 
 * Buffer management template.
 * 
 * \author Rickard E. (Rik) Faith <faith@valinux.com>
 * \author Gareth Hughes <gareth@valinux.com>
 * \author José Fonseca <jrfonseca@tungstengraphics.com>
 *
 * \todo The new functions here don't use the drm_...() convention so they will 
 * break static kernel builds.  The idea is to move them to a seperate library
 * in a later time that will be linked agains all modules.
 */

/*
 * Copyright 1999, 2000 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
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
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */


#define __NO_VERSION__
#include <linux/vmalloc.h>
#include "drmP.h"


/** \name Buffer pools initialization/cleanup */
/*@{*/

/**
 * Initialize a buffer pool.
 *
 * It allocates the drm_pool::buffers but it's the caller's responsability to
 * fill those afterwards.
 *
 * \note Not mean to be called directly by drivers. Use one of the functions
 * mentioned below instead.
 *
 * \sa drm_pci_pool_alloc(), drm_agp_pool_alloc() and drm_sg_pool_alloc().
 */
int drm_pool_create(drm_pool_t *pool, size_t count, size_t size)
{
	memset(pool, 0, sizeof(drm_pool_t));
	
	pool->count = count;
	pool->size = size;
	pool->buffers = (drm_pool_buffer_t *)drm_alloc(
			count*sizeof(drm_pool_buffer_t));

	if (!pool->buffers)
		return -ENOMEM;

	return 0;
}

/**
 * Destroy a buffer pool.
 *
 * \note This function won't actually free the structure and should't be called
 * directly by the drivers. Use drm_pool_free() instead.
 */
void drm_pool_destroy(drm_pool_t *pool)
{
	drm_free(pool->buffers);
}

/**
 * Free a buffer pool.
 *
 * Calls the free callback
 */
void drm_pool_free(drm_device_t *dev, drm_pool_t *pool)
{
	if (!pool->free) {
		DRM_ERROR( "drm_pool_free called but no free callback present\n" );
		return;
	}

	pool->free(dev, pool);
}

/*@}*/


/** \name Buffer pool allocation */
/*@{*/

/** 
 * PCI consistent memory pool.
 *
 * This structure inherits from drm_pool and is used by drm_pool_pci_alloc()
 * and drm_pool_pci_free() to store PCI specific data of the pool.
 */
typedef struct drm_pool_pci {
	drm_pool_t base;	/**< base pool  */
	void *handle;		/**< opaque handle to the PCI memory pool */
} drm_pool_pci_t;

/** 
 * Free a pool of PCI consistent memory.
 *
 * This is called internally by drm_pool_free().
 */
static void drm_pool_pci_free(drm_device_t *dev, drm_pool_t *base)
{
	drm_pool_pci_t *pool = (drm_pool_pci_t *)base;
	drm_pool_buffer_t *buffer;
	unsigned i;

	for (i = 0; i < base->count; ++i) {
		buffer = &pool->base.buffers[i];
		drm_pci_pool_free(pool->handle, buffer->cpuaddr, buffer->busaddr);
	}
	
	drm_pci_pool_destroy(dev, pool->handle);
	
	drm_pool_destroy(base);
	
	drm_free(pool);
}

/** 
 * Allocate a pool of PCI consistent memory.
 *
 * Once no longer needed, the pool should be freed calling drm_pool_free().
 * 
 * Internally this function creates (and later returns) a drm_pool_pci
 * structure, allocating the PCI memory by successive calls to
 * drm_pci_pool_alloc().
 */
drm_pool_t *drm_pool_pci_alloc(drm_device_t *dev, size_t count, size_t size, size_t align)
{
	drm_pool_pci_t *pool;
	drm_pool_buffer_t *buffer;
	unsigned i, j;

	if (!(pool = (drm_pool_pci_t *)drm_alloc( sizeof(drm_pool_pci_t))))
		goto failed_alloc;
	
	if (drm_pool_create(&pool->base, count, size))
		goto failed_pool_create;

	if (!(pool->handle = drm_pci_pool_create( dev, size, align )))
		goto failed_pci_pool_create;

	for (i = 0; i < count; ++i) {
		buffer = &pool->base.buffers[i];
		if (!(buffer->cpuaddr = drm_pci_pool_alloc(pool->handle, &buffer->busaddr)))
			goto failed_pci_pool_alloc;
	}

	pool->base.free = drm_pool_pci_free;
		
	/* Success */
	return &pool->base;

	/* Failure */
failed_pci_pool_alloc:
	for (j = 0; j < i; ++j) {
		buffer = &pool->base.buffers[j];
		drm_pci_pool_free(pool->handle, buffer->cpuaddr, buffer->busaddr);
	}

	drm_pci_pool_destroy(dev, pool->handle);
	
failed_pci_pool_create:
	drm_pool_destroy(&pool->base);
	
failed_pool_create:
	drm_free(pool);

failed_alloc:
	return NULL;
}

/** 
 * Free a pool of AGP memory.
 *
 * This is called internally by drm_pool_free().
 */
static void drm_pool_agp_free(drm_device_t *dev, drm_pool_t *pool)
{
	drm_pool_destroy(pool);
	
	drm_free(pool);
}

/** 
 * Allocate a pool of AGP memory.
 *
 * Once no longer needed, the pool should be freed calling drm_pool_free().
 */
drm_pool_t *drm_pool_agp_alloc(drm_device_t *dev, unsigned offset, size_t count, size_t size, size_t align)
{
	drm_pool_t *pool;
	drm_pool_buffer_t *buffer;
	unsigned i;

	if (!dev->agp)
		return NULL;
	
	if (!(pool = (drm_pool_t *)drm_alloc( sizeof(drm_pool_t))))
		return NULL;
	
	if (drm_pool_create(pool, count, size)) {
		drm_free(pool);
		return NULL;
	}

	for (i = 0; i < count; ++i) {
		buffer = &pool->buffers[i];
		buffer->cpuaddr = (void *)(dev->agp->base + offset);
		buffer->busaddr = dev->agp->base + offset;
		offset += align;
	}

	pool->free = drm_pool_agp_free;
		
	return pool;
}

/** 
 * Free a pool of scatter/gather memory.
 *
 * This is called internally by drm_pool_free().
 */
static void drm_pool_sg_free(drm_device_t *dev, drm_pool_t *pool)
{
	drm_pool_destroy(pool);
	
	drm_free(pool);
}

/** 
 * Allocate a pool of AGP memory.
 *
 * Once no longer needed, the pool should be freed calling drm_pool_free().
 */
drm_pool_t *drm_pool_sg_alloc(drm_device_t *dev, unsigned offset, size_t count, size_t size, size_t align)
{
	drm_pool_t *pool;
	drm_pool_buffer_t *buffer;
	unsigned i;

	if (!dev->sg)
		return NULL;
	
	if (!(pool = (drm_pool_t *)drm_alloc( sizeof(drm_pool_t))))
		return NULL;
	
	if (drm_pool_create(pool, count, size)) {
		drm_free(pool);
		return NULL;
	}

	for (i = 0; i < count; ++i) {
		buffer = &pool->buffers[i];
		buffer->cpuaddr = (void *)(dev->sg->handle + offset);
		buffer->busaddr = offset;
		offset += align;
	}

	pool->free = drm_pool_sg_free;
		
	return pool;
}

/*@}*/


/** \name Free-list management */
/*@{*/

/**
 * Initialize a free-list.
 *
 * \param freelist pointer to the free-list structure to initialize.
 * \param pool DMA buffer pool associated with the free-list.
 * \param stride stride of the list entries. This can be used to extend the
 * information stored in each buffer.
 * \code
 * struct my_freelist_entry {
 *   struct drm_freelist2_entry base;
 *   ...
 * } ;
 * 
 * ...
 * drm_freelist2_create(my_freelist, my_pool, sizeof(my_freelist_entry));
 * ...
 * \endcode
 * 
 * \return zero on success or a negative number on failure.
 *
 */
int drm_freelist2_create(drm_freelist2_t *freelist, drm_pool_t *pool, size_t stride)
{
	unsigned i;
	drm_freelist2_entry_t *entry;

	memset(freelist, 0, sizeof(drm_freelist2_t));
	
	freelist->pool = pool;
	freelist->stride = stride;
	freelist->entries = drm_alloc( pool->count*stride);

	if (!freelist->entries)
		return -ENOMEM;

	memset(freelist->entries, 0, pool->count*stride);
	
	/* Add each buffer to the free list */
	for(i = 0; i < pool->count; ++i) {
		entry = (drm_freelist2_entry_t *)((unsigned char *)freelist->entry + i*stride);
		entry->buffer = &pool->buffers[i];
		list_add_tail(&entry->list, freelist->free);
	}
	
	return 0;
}

/**
 * Free the resources associated with a free-list.
 */
void drm_freelist2_destroy(drm_freelist2_t *freelist)
{
	drm_free(freelist->entries);
}

/**
 * Reset the buffers.
 *
 * Iterates the pending list and move all discardable buffers into the
 * free list.
 *
 * \warning This function should only be called when the engine is idle or
 * locked up, as it assumes all buffers in the pending list have been completed
 * by the hardware.
 */
void drm_freelist2_reset(drm_freelist2_t *freelist)
{
	struct list_head *ptr, *tmp;
	drm_freelist2_entry_t *entry;

	if ( list_empty(&freelist->pending) )
		return;

	list_for_each_safe(ptr, tmp, &dev_priv->pending)
	{
		entry = list_entry(ptr, drm_freelist2_entry_t, list);
		if (entry->discard) {
			list_del(ptr);
			list_add_tail(ptr, &freelist->free);
		}
	}
}

/**
 * Walks through the pending list freeing the processed buffers by comparing
 * their stamps.
 *
 * \param freelist pointer to the free-list structure.
 * \param bail_out whether to bail out on after so-many freed buffers.
 *
 * \return number of freed entries.
 */
int drm_freelist2_update(drm_freelist2_t *freelist, int bail_out)
{
	struct list_head *ptr, *tmp;
	drm_freelist2_entry_t *entry;
	int count = 0;

	list_for_each_safe(ptr, tmp, &freelist->pending) {
		entry = list_entry(ptr, drm_freelist2_entry_t, list);

		if ( entry->reuse )
			continue;

		delta = freelist->last_stamp - entry->stamp;
		if ( delta >= 0 || delta <= -0x4000000 ) {
			/* found a processed buffer */
			list_del(ptr);
			list_add_tail(ptr, &freelist->free);
			++count;
			if (bail_out && count >= bail_out)
				break;
		}
	}

	return count;
}

/**
 * Get a free buffer from the free-list.
 *
 * \return pointer to the buffer entry on success, or NULL on failure.
 */
drm_freelist2_entry_t *drm_freelist2_get(drm_freelist2_t *freelist)
{
	struct list_head *ptr, *tmp;
	drm_freelist2_entry_t *entry;

	if ( list_empty(&freelist->free) ) {

		if ( list_empty( &freelist->pending ) ) {
			DRM_ERROR( "Couldn't get buffer - pending and free lists empty\n" );
			return NULL;
		}

		if (freelist->wait(freelist))
			return NULL;
	}

	ptr = freelist->free.next;
	list_del(ptr);
	entry = list_entry(ptr, drm_freelist2_entry_t, list);
	entry->used = 0;
	return entry->buf;
}

/**
 * Helper for the drm_freelist2::wait callbacks.
 * 
 */
int drm_freelist2_wait_helper(
		drm_freelist2_t *freelist, 
		void (*update_stamp)(drm_freelist2_t *freelist),
		unsigned long timeout,
		int bail_out)
{
	struct list_head *ptr, *tmp;
	drm_freelist2_entry_t *entry;
	unsigned long t;

	for ( t = 0 ; t < timeout ; t++ ) {
		update_stamp(freelist);

		if (drm_freelist2_update(freelist, bail_out))
			return 0;

		DRM_UDELAY( 1 );
	}

	return -1;
}

/*@}*/








/** \name Backwards compatability ioctl's */
/*@{*/

/**
 * Compute size order.  Returns the exponent of the smaller power of two which
 * is greater or equal to given number.
 * 
 * \param size size.
 * \return order.
 *
 * \todo Can be made faster.
 */
int drm_order( unsigned long size )
{
	int order;
	unsigned long tmp;

	for ( order = 0, tmp = size ; tmp >>= 1 ; ++order );

	if ( size & ~(1 << order) )
		++order;

	return order;
}

/**
 * Add and allocate buffers in PCI memory.
 *
 * \note This ioctl is (and always has been) the \e only way to allocate PCI
 * buffers from user space.
 */
int drm_addbufs_pci_ioctl( struct inode *inode, struct file *filp,
		      unsigned int cmd, unsigned long arg )
{
   	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_device_dma_t *dma = dev->dma;
	drm_buf_desc_t request;
	int count;
	int order;
	int size;
	int total;
	int page_order;
	drm_buf_entry_t *entry;
	unsigned long page;
	int alignment;
	unsigned long offset;
	int i;
	int byte_count;
	int page_count;
	unsigned long *temp_pagelist;
	drm_buf_t **temp_buflist;

	if ( !dma ) return -EINVAL;

	if ( copy_from_user( &request, (drm_buf_desc_t *)arg,
			     sizeof(request) ) )
		return -EFAULT;

	count = request.count;
	order = drm_order( request.size );
	size = 1 << order;

	DRM_DEBUG( "count=%d, size=%d (%d), order=%d, queue_count=%d\n",
		   request.count, request.size, size,
		   order, dev->queue_count );

	if ( order < DRM_MIN_ORDER || order > DRM_MAX_ORDER ) return -EINVAL;
	if ( dev->queue_count ) return -EBUSY; /* Not while in use */

	alignment = (request.flags & _DRM_PAGE_ALIGN)
		? PAGE_ALIGN(size) : size;
	page_order = order - PAGE_SHIFT > 0 ? order - PAGE_SHIFT : 0;
	total = PAGE_SIZE << page_order;

	spin_lock( &dev->count_lock );
	if ( dev->buf_use ) {
		spin_unlock( &dev->count_lock );
		return -EBUSY;
	}
	atomic_inc( &dev->buf_alloc );
	spin_unlock( &dev->count_lock );

	down( &dev->struct_sem );
	entry = &dma->bufs[order];
	if ( entry->pool ) {
		up( &dev->struct_sem );
		atomic_dec( &dev->buf_alloc );
		return -ENOMEM;	/* May only call once for each order */
	}

	if (count < 0 || count > 4096) {
		up( &dev->struct_sem );
		atomic_dec( &dev->buf_alloc );
		return -EINVAL;
	}

	entry->pool = drm_pool_pci_alloc(dev, count, size, alignment);
	if ( !entry->pool ) {
		up( &dev->struct_sem );
		atomic_dec( &dev->buf_alloc );
		return -ENOMEM;
	}

	entry->seglist = drm_alloc( count * sizeof(*entry->seglist) );
	if ( !entry->seglist ) {
		drm_free( entry->buflist );
		up( &dev->struct_sem );
		atomic_dec( &dev->buf_alloc );
		return -ENOMEM;
	}
	memset( entry->seglist, 0, count * sizeof(*entry->seglist) );

	/* Keep the original pagelist until we know all the allocations
	 * have succeeded
	 */
	temp_pagelist = drm_alloc( (dma->page_count + (count << page_order))
				    * sizeof(*dma->pagelist) );
	if (!temp_pagelist) {
		drm_pool_free( entry->pool );
		drm_free( entry->seglist );
		up( &dev->struct_sem );
		atomic_dec( &dev->buf_alloc );
		return -ENOMEM;
	}
	memcpy(temp_pagelist,
	       dma->pagelist,
	       dma->page_count * sizeof(*dma->pagelist));
	DRM_DEBUG( "pagelist: %d entries\n",
		   dma->page_count + (count << page_order) );

	entry->buf_size	= size;
	entry->page_order = page_order;
	byte_count = 0;
	page_count = 0;

	while ( entry->buf_count < count ) {
		/* FIXME: What to put in here? */
		page = drm_alloc_pages( page_order );
		entry->seglist[entry->seg_count++] = page;
		for ( i = 0 ; i < (1 << page_order) ; i++ ) {
			DRM_DEBUG( "page %d @ 0x%08lx\n",
				   dma->page_count + page_count,
				   page + PAGE_SIZE * i );
			temp_pagelist[dma->page_count + page_count++]
				= page + PAGE_SIZE * i;
		}
		byte_count += PAGE_SIZE << page_order;
	}

	/* No allocations failed, so now we can replace the orginal pagelist
	 * with the new one.
	 */
	if (dma->page_count) {
		drm_free(dma->pagelist);
	}
	dma->pagelist = temp_pagelist;

	dma->buf_count += entry->buf_count;
	dma->seg_count += entry->seg_count;
	dma->page_count += entry->seg_count << page_order;
	dma->byte_count += PAGE_SIZE * (entry->seg_count << page_order);

	up( &dev->struct_sem );

	request.count = entry->buf_count;
	request.size = size;

	if ( copy_to_user( (drm_buf_desc_t *)arg, &request, sizeof(request) ) )
		return -EFAULT;

	atomic_dec( &dev->buf_alloc );
	return 0;

}

#if __REALLY_HAVE_AGP
/**
 * Add AGP buffers for DMA transfers.
 *
 * \param inode device inode.
 * \param filp file pointer.
 * \param cmd command.
 * \param arg pointer to a drm_buf_desc_t request.
 * \return zero on success or a negative number on failure.
 * 
 * After some sanity checks creates a drm_buf structure for each buffer and
 * reallocates the buffer list of the same size order to accommodate the new
 * buffers.
 */
int drm_addbufs_agp_ioctl( struct inode *inode, struct file *filp,
		      unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_device_dma_t *dma = dev->dma;
	drm_buf_desc_t request;
	drm_buf_entry_t *entry;
	unsigned long offset;
	unsigned long agp_offset;
	int count;
	int order;
	int size;
	int alignment;
	int page_order;
	int total;
	int byte_count;
	int i;
	drm_buf_t **temp_buflist;

	if ( !dma ) return -EINVAL;

	if ( copy_from_user( &request, (drm_buf_desc_t *)arg,
			     sizeof(request) ) )
		return -EFAULT;

	count = request.count;
	order = drm_order( request.size );
	size = 1 << order;

	alignment  = (request.flags & _DRM_PAGE_ALIGN)
		? PAGE_ALIGN(size) : size;
	page_order = order - PAGE_SHIFT > 0 ? order - PAGE_SHIFT : 0;
	total = PAGE_SIZE << page_order;

	byte_count = 0;
	agp_offset = dev->agp->base + request.agp_start;

	DRM_DEBUG( "count:      %d\n",  count );
	DRM_DEBUG( "order:      %d\n",  order );
	DRM_DEBUG( "size:       %d\n",  size );
	DRM_DEBUG( "agp_offset: %lu\n", agp_offset );
	DRM_DEBUG( "alignment:  %d\n",  alignment );
	DRM_DEBUG( "page_order: %d\n",  page_order );
	DRM_DEBUG( "total:      %d\n",  total );

	if ( order < DRM_MIN_ORDER || order > DRM_MAX_ORDER ) return -EINVAL;
	if ( dev->queue_count ) return -EBUSY; /* Not while in use */

	spin_lock( &dev->count_lock );
	if ( dev->buf_use ) {
		spin_unlock( &dev->count_lock );
		return -EBUSY;
	}
	atomic_inc( &dev->buf_alloc );
	spin_unlock( &dev->count_lock );

	down( &dev->struct_sem );
	entry = &dma->bufs[order];
	if ( entry->buf_count ) {
		up( &dev->struct_sem );
		atomic_dec( &dev->buf_alloc );
		return -ENOMEM; /* May only call once for each order */
	}

	if (count < 0 || count > 4096) {
		up( &dev->struct_sem );
		atomic_dec( &dev->buf_alloc );
		return -EINVAL;
	}

	entry->pool = drm_pool_pci_alloc(dev, request.agp_start, count, size, alignment);
	if ( !entry->pool ) {
		up( &dev->struct_sem );
		atomic_dec( &dev->buf_alloc );
		return -ENOMEM;
	}

	for ( i = 0 ; i < entry->buf_count ; i++ ) {
		dma->buflist[i + dma->buf_count] = &entry->buflist[i];
	}

	dma->buf_count += entry->buf_count;
	dma->byte_count += byte_count;

	DRM_DEBUG( "dma->buf_count : %d\n", dma->buf_count );
	DRM_DEBUG( "entry->buf_count : %d\n", entry->buf_count );

	up( &dev->struct_sem );

	request.count = entry->buf_count;
	request.size = size;

	if ( copy_to_user( (drm_buf_desc_t *)arg, &request, sizeof(request) ) )
		return -EFAULT;

	dma->flags = _DRM_DMA_USE_AGP;

	atomic_dec( &dev->buf_alloc );
	return 0;
}
#endif /* __REALLY_HAVE_AGP */

/**
 * Add scatter/gather buffers.
 */
int drm_addbufs_sg_ioctl( struct inode *inode, struct file *filp,
                     unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_device_dma_t *dma = dev->dma;
	drm_buf_desc_t request;
	drm_buf_entry_t *entry;
	unsigned long offset;
	unsigned long agp_offset;
	int count;
	int order;
	int size;
	int alignment;
	int page_order;
	int total;
	int byte_count;
	int i;
	drm_buf_t **temp_buflist;

	if ( !dma ) return -EINVAL;

	if ( copy_from_user( &request, (drm_buf_desc_t *)arg,
                             sizeof(request) ) )
		return -EFAULT;

	count = request.count;
	order = drm_order( request.size );
	size = 1 << order;

	alignment  = (request.flags & _DRM_PAGE_ALIGN)
			? PAGE_ALIGN(size) : size;
	page_order = order - PAGE_SHIFT > 0 ? order - PAGE_SHIFT : 0;
	total = PAGE_SIZE << page_order;

	byte_count = 0;
	agp_offset = request.agp_start;

	DRM_DEBUG( "count:      %d\n",  count );
	DRM_DEBUG( "order:      %d\n",  order );
	DRM_DEBUG( "size:       %d\n",  size );
	DRM_DEBUG( "agp_offset: %lu\n", agp_offset );
	DRM_DEBUG( "alignment:  %d\n",  alignment );
	DRM_DEBUG( "page_order: %d\n",  page_order );
	DRM_DEBUG( "total:      %d\n",  total );

	if ( order < DRM_MIN_ORDER || order > DRM_MAX_ORDER ) return -EINVAL;
	if ( dev->queue_count ) return -EBUSY; /* Not while in use */

	spin_lock( &dev->count_lock );
	if ( dev->buf_use ) {
		spin_unlock( &dev->count_lock );
		return -EBUSY;
	}
	atomic_inc( &dev->buf_alloc );
	spin_unlock( &dev->count_lock );

	down( &dev->struct_sem );
	entry = &dma->bufs[order];
	if ( entry->buf_count ) {
		up( &dev->struct_sem );
		atomic_dec( &dev->buf_alloc );
		return -ENOMEM; /* May only call once for each order */
	}

	if (count < 0 || count > 4096) {
		up( &dev->struct_sem );
		atomic_dec( &dev->buf_alloc );
		return -EINVAL;
	}

	entry->pool = drm_pool_sg_alloc(dev, request.agp_start, count, size, alignment);
	if ( !entry->pool ) {
		up( &dev->struct_sem );
		atomic_dec( &dev->buf_alloc );
		return -ENOMEM;
	}

	entry->buf_size = size;
	entry->page_order = page_order;

	for ( i = 0 ; i < entry->buf_count ; i++ ) {
		dma->buflist[i + dma->buf_count] = &entry->buflist[i];
	}

	dma->buf_count += entry->buf_count;
	dma->byte_count += byte_count;

	DRM_DEBUG( "dma->buf_count : %d\n", dma->buf_count );
	DRM_DEBUG( "entry->buf_count : %d\n", entry->buf_count );

	up( &dev->struct_sem );

	request.count = entry->buf_count;
	request.size = size;

	if ( copy_to_user( (drm_buf_desc_t *)arg, &request, sizeof(request) ) )
		return -EFAULT;

	dma->flags = _DRM_DMA_USE_SG;

	atomic_dec( &dev->buf_alloc );
	return 0;
}

/**
 * Add buffers for DMA transfers.
 *
 * \param inode device inode.
 * \param filp file pointer.
 * \param cmd command.
 * \param arg pointer to a drm_buf_desc_t request.
 * \return zero on success or a negative number on failure.
 *
 * According with the memory type specified in drm_buf_desc::flags and the
 * build options, it dispatches the call either to addbufs_agp(), addbufs_sg()
 * or addbufs_pci() for AGP, scatter/gather or consistent PCI memory
 * respectively.
 */
int drm_addbufs_ioctl( struct inode *inode, struct file *filp,
		  unsigned int cmd, unsigned long arg )
{
	drm_buf_desc_t request;

	if ( copy_from_user( &request, (drm_buf_desc_t *)arg,
			     sizeof(request) ) )
		return -EFAULT;

#if __REALLY_HAVE_AGP
	if ( request.flags & _DRM_AGP_BUFFER )
		return drm_addbufs_agp( inode, filp, cmd, arg );
	else
#endif
#if __HAVE_SG
	if ( request.flags & _DRM_SG_BUFFER )
		return drm_addbufs_sg( inode, filp, cmd, arg );
	else
#endif
#if __HAVE_PCI_DMA
		return drm_addbufs_pci( inode, filp, cmd, arg );
#else
		return -EINVAL;
#endif
}


/**
 * Get information about the buffer mappings.
 *
 * This was originally mean for debugging purposes, or by a sophisticated
 * client library to determine how best to use the available buffers (e.g.,
 * large buffers can be used for image transfer).
 *
 * \param inode device inode.
 * \param filp file pointer.
 * \param cmd command.
 * \param arg pointer to a drm_buf_info structure.
 * \return zero on success or a negative number on failure.
 *
 * Increments drm_device::buf_use while holding the drm_device::count_lock
 * lock, preventing of allocating more buffers after this call. Information
 * about each requested buffer is then copied into user space.
 */
int drm_infobufs_ioctl( struct inode *inode, struct file *filp,
		   unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_device_dma_t *dma = dev->dma;
	drm_buf_info_t request;
	int i;
	int count;

	if ( !dma ) return -EINVAL;

	spin_lock( &dev->count_lock );
	if ( atomic_read( &dev->buf_alloc ) ) {
		spin_unlock( &dev->count_lock );
		return -EBUSY;
	}
	++dev->buf_use;		/* Can't allocate more after this call */
	spin_unlock( &dev->count_lock );

	if ( copy_from_user( &request,
			     (drm_buf_info_t *)arg,
			     sizeof(request) ) )
		return -EFAULT;

	for ( i = 0, count = 0 ; i < DRM_MAX_ORDER + 1 ; i++ ) {
		if ( dma->bufs[i].buf_count ) ++count;
	}

	DRM_DEBUG( "count = %d\n", count );

	if ( request.count >= count ) {
		for ( i = 0, count = 0 ; i < DRM_MAX_ORDER + 1 ; i++ ) {
			if ( dma->bufs[i].buf_count ) {
				drm_buf_desc_t *to = &request.list[count];
				drm_buf_entry_t *from = &dma->bufs[i];
				drm_freelist_t *list = &dma->bufs[i].freelist;
				if ( copy_to_user( &to->count,
						   &from->buf_count,
						   sizeof(from->buf_count) ) ||
				     copy_to_user( &to->size,
						   &from->buf_size,
						   sizeof(from->buf_size) ) ||
				     copy_to_user( &to->low_mark,
						   &list->low_mark,
						   sizeof(list->low_mark) ) ||
				     copy_to_user( &to->high_mark,
						   &list->high_mark,
						   sizeof(list->high_mark) ) )
					return -EFAULT;

				DRM_DEBUG( "%d %d %d %d %d\n",
					   i,
					   dma->bufs[i].buf_count,
					   dma->bufs[i].buf_size,
					   dma->bufs[i].freelist.low_mark,
					   dma->bufs[i].freelist.high_mark );
				++count;
			}
		}
	}
	request.count = count;

	if ( copy_to_user( (drm_buf_info_t *)arg,
			   &request,
			   sizeof(request) ) )
		return -EFAULT;

	return 0;
}

/**
 * Specifies a low and high water mark for buffer allocation
 *
 * \param inode device inode.
 * \param filp file pointer.
 * \param cmd command.
 * \param arg a pointer to a drm_buf_desc structure.
 * \return zero on success or a negative number on failure.
 *
 * Verifies that the size order is bounded between the admissible orders and
 * updates the respective drm_device_dma::bufs entry low and high water mark.
 *
 * \note This ioctl is deprecated and mostly never used.
 */
int drm_markbufs_ioctl( struct inode *inode, struct file *filp,
		   unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_device_dma_t *dma = dev->dma;
	drm_buf_desc_t request;
	int order;
	drm_buf_entry_t *entry;

	if ( !dma ) return -EINVAL;

	if ( copy_from_user( &request,
			     (drm_buf_desc_t *)arg,
			     sizeof(request) ) )
		return -EFAULT;

	DRM_DEBUG( "%d, %d, %d\n",
		   request.size, request.low_mark, request.high_mark );
	order = drm_order( request.size );
	if ( order < DRM_MIN_ORDER || order > DRM_MAX_ORDER ) return -EINVAL;
	entry = &dma->bufs[order];

	if ( request.low_mark < 0 || request.low_mark > entry->buf_count )
		return -EINVAL;
	if ( request.high_mark < 0 || request.high_mark > entry->buf_count )
		return -EINVAL;

	entry->freelist.low_mark  = request.low_mark;
	entry->freelist.high_mark = request.high_mark;

	return 0;
}

/**
 * Unreserve the buffers in list, previously reserved using drmDMA. 
 *
 * \param inode device inode.
 * \param filp file pointer.
 * \param cmd command.
 * \param arg pointer to a drm_buf_free structure.
 * \return zero on success or a negative number on failure.
 * 
 * Calls free_buffer() for each used buffer.
 * This function is primarily used for debugging.
 */
int drm_freebufs_ioctl( struct inode *inode, struct file *filp,
		   unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_device_dma_t *dma = dev->dma;
	drm_buf_free_t request;
	int i;
	int idx;
	drm_buf_t *buf;

	if ( !dma ) return -EINVAL;

	if ( copy_from_user( &request,
			     (drm_buf_free_t *)arg,
			     sizeof(request) ) )
		return -EFAULT;

	DRM_DEBUG( "%d\n", request.count );
	for ( i = 0 ; i < request.count ; i++ ) {
		if ( copy_from_user( &idx,
				     &request.list[i],
				     sizeof(idx) ) )
			return -EFAULT;
		if ( idx < 0 || idx >= dma->buf_count ) {
			DRM_ERROR( "Index %d (of %d max)\n",
				   idx, dma->buf_count - 1 );
			return -EINVAL;
		}
		buf = dma->buflist[idx];
		if ( buf->filp != filp ) {
			DRM_ERROR( "Process %d freeing buffer not owned\n",
				   current->pid );
			return -EINVAL;
		}
		drm_free_buffer( dev, buf );
	}

	return 0;
}

/**
 * Maps all of the DMA buffers into client-virtual space.
 *
 * \param inode device inode.
 * \param filp file pointer.
 * \param cmd command.
 * \param arg pointer to a drm_buf_map structure.
 * \return zero on success or a negative number on failure.
 *
 * Maps the AGP or SG buffer region with do_mmap(), and copies information
 * about each buffer into user space. The PCI buffers are already mapped on the
 * addbufs_pci() call.
 */
int drm_mapbufs_ioctl( struct inode *inode, struct file *filp,
		  unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_device_dma_t *dma = dev->dma;
	int retcode = 0;
	const int zero = 0;
	unsigned long virtual;
	unsigned long address;
	drm_buf_map_t request;
	int i;

	if ( !dma ) return -EINVAL;

	spin_lock( &dev->count_lock );
	if ( atomic_read( &dev->buf_alloc ) ) {
		spin_unlock( &dev->count_lock );
		return -EBUSY;
	}
	dev->buf_use++;		/* Can't allocate more after this call */
	spin_unlock( &dev->count_lock );

	if ( copy_from_user( &request, (drm_buf_map_t *)arg,
			     sizeof(request) ) )
		return -EFAULT;

	if ( request.count >= dma->buf_count ) {
		if ( (__HAVE_AGP && (dma->flags & _DRM_DMA_USE_AGP)) ||
		     (__HAVE_SG && (dma->flags & _DRM_DMA_USE_SG)) ) {
			drm_map_t *map = DRIVER_AGP_BUFFERS_MAP( dev );

			if ( !map ) {
				retcode = -EINVAL;
				goto done;
			}

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
		} else {
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
		}
		if ( virtual > -1024UL ) {
			/* Real error */
			retcode = (signed long)virtual;
			goto done;
		}
		request.virtual = (void *)virtual;

		for ( i = 0 ; i < dma->buf_count ; i++ ) {
			if ( copy_to_user( &request.list[i].idx,
					   &dma->buflist[i]->idx,
					   sizeof(request.list[0].idx) ) ) {
				retcode = -EFAULT;
				goto done;
			}
			if ( copy_to_user( &request.list[i].total,
					   &dma->buflist[i]->total,
					   sizeof(request.list[0].total) ) ) {
				retcode = -EFAULT;
				goto done;
			}
			if ( copy_to_user( &request.list[i].used,
					   &zero,
					   sizeof(zero) ) ) {
				retcode = -EFAULT;
				goto done;
			}
			address = virtual + dma->buflist[i]->offset; /* *** */
			if ( copy_to_user( &request.list[i].address,
					   &address,
					   sizeof(address) ) ) {
				retcode = -EFAULT;
				goto done;
			}
		}
	}
 done:
	request.count = dma->buf_count;
	DRM_DEBUG( "%d buffers, retcode = %d\n", request.count, retcode );

	if ( copy_to_user( (drm_buf_map_t *)arg, &request, sizeof(request) ) )
		return -EFAULT;

	return retcode;
}

/**
 * Ioctl to specify a range of memory that is available for mapping by a non-root process.
 *
 * \param inode device inode.
 * \param filp file pointer.
 * \param cmd command.
 * \param arg pointer to a drm_map structure.
 * \return zero on success or a negative value on error.
 *
 * Adjusts the memory offset to its absolute value according to the mapping
 * type.  Adds the map to the map list drm_device::maplist. Adds MTRR's where
 * applicable and if supported by the kernel.
 */
int drm_addmap_ioctl( struct inode *inode, struct file *filp,
		 unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_map_t *map;
	drm_map_list_t *list;

	if ( !(filp->f_mode & 3) ) return -EACCES; /* Require read/write */

	map = drm_alloc( sizeof(*map) );
	if ( !map )
		return -ENOMEM;

	if ( copy_from_user( map, (drm_map_t *)arg, sizeof(*map) ) ) {
		drm_free( map );
		return -EFAULT;
	}

	/* Only allow shared memory to be removable since we only keep enough
	 * book keeping information about shared memory to allow for removal
	 * when processes fork.
	 */
	if ( (map->flags & _DRM_REMOVABLE) && map->type != _DRM_SHM ) {
		drm_free( map );
		return -EINVAL;
	}
	DRM_DEBUG( "offset = 0x%08lx, size = 0x%08lx, type = %d\n",
		   map->offset, map->size, map->type );
	if ( (map->offset & (~PAGE_MASK)) || (map->size & (~PAGE_MASK)) ) {
		drm_free( map );
		return -EINVAL;
	}
	map->mtrr   = -1;
	map->handle = 0;

	switch ( map->type ) {
	case _DRM_REGISTERS:
	case _DRM_FRAME_BUFFER:
#if !defined(__sparc__) && !defined(__alpha__)
		if ( map->offset + map->size < map->offset ||
		     map->offset < virt_to_phys(high_memory) ) {
			drm_free( map );
			return -EINVAL;
		}
#endif
#ifdef __alpha__
		map->offset += dev->hose->mem_space->start;
#endif
#if __REALLY_HAVE_MTRR
		if ( map->type == _DRM_FRAME_BUFFER ||
		     (map->flags & _DRM_WRITE_COMBINING) ) {
			map->mtrr = mtrr_add( map->offset, map->size,
					      MTRR_TYPE_WRCOMB, 1 );
		}
#endif
		map->handle = drm_ioremap( map->offset, map->size, dev );
		break;

	case _DRM_SHM:
		map->handle = vmalloc_32(map->size);
		DRM_DEBUG( "%lu %d %p\n",
			   map->size, drm_order( map->size ), map->handle );
		if ( !map->handle ) {
			drm_free( map );
			return -ENOMEM;
		}
		map->offset = (unsigned long)map->handle;
		if ( map->flags & _DRM_CONTAINS_LOCK ) {
			dev->sigdata.lock =
			dev->lock.hw_lock = map->handle; /* Pointer to lock */
		}
		break;
#if __REALLY_HAVE_AGP
	case _DRM_AGP:
#ifdef __alpha__
		map->offset += dev->hose->mem_space->start;
#endif
		map->offset += dev->agp->base;
		map->mtrr   = dev->agp->agp_mtrr; /* for getmap */
		break;
#endif
		
#if __HAVE_SG
	case _DRM_SCATTER_GATHER:
		if (!dev->sg) {
			drm_free(map);
			return -EINVAL;
		}
		map->offset += dev->sg->handle;
		break;
#endif

	default:
		drm_free( map );
		return -EINVAL;
	}

	list = drm_alloc(sizeof(*list));
	if(!list) {
		drm_free(map );
		return -EINVAL;
	}
	memset(list, 0, sizeof(*list));
	list->map = map;

	down(&dev->struct_sem);
	list_add(&list->head, &dev->maplist->head);
 	up(&dev->struct_sem);

	if ( copy_to_user( (drm_map_t *)arg, map, sizeof(*map) ) )
		return -EFAULT;
	if ( map->type != _DRM_SHM ) {
		if ( copy_to_user( &((drm_map_t *)arg)->handle,
				   &map->offset,
				   sizeof(map->offset) ) )
			return -EFAULT;
	}
	return 0;
}


/**
 * Remove a map private from list and deallocate resources if the mapping
 * isn't in use.
 *
 * \param inode device inode.
 * \param filp file pointer.
 * \param cmd command.
 * \param arg pointer to a drm_map_t structure.
 * \return zero on success or a negative value on error.
 *
 * Searches the map on drm_device::maplist, removes it from the list, see if
 * its being used, and free any associate resource (such as MTRR's) if it's not
 * being on use.
 *
 * \sa addmap().
 */
int drm_rmmap_ioctl(struct inode *inode, struct file *filp,
	       unsigned int cmd, unsigned long arg)
{
	drm_file_t	*priv	= filp->private_data;
	drm_device_t	*dev	= priv->dev;
	struct list_head *list;
	drm_map_list_t *r_list = NULL;
	drm_vma_entry_t *pt, *prev;
	drm_map_t *map;
	drm_map_t request;
	int found_maps = 0;

	if (copy_from_user(&request, (drm_map_t *)arg,
			   sizeof(request))) {
		return -EFAULT;
	}

	down(&dev->struct_sem);
	list = &dev->maplist->head;
	list_for_each(list, &dev->maplist->head) {
		r_list = list_entry(list, drm_map_list_t, head);

		if(r_list->map &&
		   r_list->map->handle == request.handle &&
		   r_list->map->flags & _DRM_REMOVABLE) break;
	}

	/* List has wrapped around to the head pointer, or its empty we didn't
	 * find anything.
	 */
	if(list == (&dev->maplist->head)) {
		up(&dev->struct_sem);
		return -EINVAL;
	}
	map = r_list->map;
	list_del(list);
	drm_free(list);

	for (pt = dev->vmalist, prev = NULL; pt; prev = pt, pt = pt->next) {
		if (pt->vma->vm_private_data == map) found_maps++;
	}

	if(!found_maps) {
		switch (map->type) {
		case _DRM_REGISTERS:
		case _DRM_FRAME_BUFFER:
#if __REALLY_HAVE_MTRR
			if (map->mtrr >= 0) {
				int retcode;
				retcode = mtrr_del(map->mtrr,
						   map->offset,
						   map->size);
				DRM_DEBUG("mtrr_del = %d\n", retcode);
			}
#endif
			drm_ioremapfree(map->handle, map->size, dev);
			break;
		case _DRM_SHM:
			vfree(map->handle);
			break;
		case _DRM_AGP:
		case _DRM_SCATTER_GATHER:
			break;
		}
		drm_free(map);
	}
	up(&dev->struct_sem);
	return 0;
}


/*@}*/


/** \name Device initialization and cleanup */
/*@{*/

/*@}*/
