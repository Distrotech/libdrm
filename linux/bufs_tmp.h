/* bufs_tmp.h -- Generic buffer template -*- linux-c -*-
 * Created: Thu Nov 23 03:10:50 2000 by gareth@valinux.com
 *
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
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *	Rickard E. (Rik) Faith <faith@valinux.com>
 *	Gareth Hughes <gareth@valinux.com>
 *
 */

#ifndef HAVE_AGP
#define HAVE_AGP			0
#endif
#ifndef HAVE_PCI_DMA
#define HAVE_PCI_DMA			0
#endif

#ifndef DRIVER_BUFFER_PRIVATE
#define DRIVER_BUFFER_PRIVATE		u32
#endif
#ifndef DRIVER_AGP_BUFFER_MAP
#define DRIVER_AGP_BUFFER_MAP		NULL
#endif


#if HAVE_AGP && defined(CONFIG_AGP) || defined(CONFIG_AGP_MODULE)
int TAG(addbufs_agp)( struct inode *inode, struct file *filp,
		      unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_device_dma_t *dma = dev->dma;
	drm_buf_desc_t request;
	drm_buf_entry_t *entry;
	drm_buf_t *buf;
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

	if ( !dma ) return -EINVAL;

	if ( copy_from_user( &request, (drm_buf_desc_t *)arg,
			     sizeof(request) ) )
		return -EFAULT;

	count = request.count;
	order = drm_order(request.size);
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
	DRM_DEBUG( "agp_offset: %ld\n", agp_offset );
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

	entry->buflist = drm_alloc( count * sizeof(*entry->buflist),
				    DRM_MEM_BUFS );
	if ( !entry->buflist ) {
		up( &dev->struct_sem );
		atomic_dec( &dev->buf_alloc );
		return -ENOMEM;
	}
	memset( entry->buflist, 0, count * sizeof(*entry->buflist) );

	entry->buf_size = size;
	entry->page_order = page_order;

	offset = 0;

	while ( entry->buf_count < count ) {
		buf          = &entry->buflist[entry->buf_count];
		buf->idx     = dma->buf_count + entry->buf_count;
		buf->total   = alignment;
		buf->order   = order;
		buf->used    = 0;

		buf->offset  = (dma->byte_count + offset); /* ******** */
		buf->address = (void *)(agp_offset + offset);
		buf->next    = NULL;
		buf->waiting = 0;
		buf->pending = 0;
		init_waitqueue_head( &buf->dma_wait );
		buf->pid     = 0;

		buf->dev_priv_size = sizeof(DRIVER_BUFFER_PRIVATE);
		buf->dev_private = drm_alloc( sizeof(DRIVER_BUFFER_PRIVATE),
					      DRM_MEM_BUFS );
		memset( buf->dev_private, 0, buf->dev_priv_size );

#if DRM_DMA_HISTOGRAM
		buf->time_queued = 0;
		buf->time_dispatched = 0;
		buf->time_completed = 0;
		buf->time_freed = 0;
#endif
		DRM_DEBUG( "buffer %d @ %p\n",
			   entry->buf_count, buf->address );

		offset += alignment;
		entry->buf_count++;
		byte_count += PAGE_SIZE << page_order;
	}

	DRM_DEBUG( "byte_count: %d\n", byte_count );

	dma->buflist = drm_realloc( dma->buflist,
				    dma->buf_count * sizeof(*dma->buflist),
				    (dma->buf_count + entry->buf_count)
				    * sizeof(*dma->buflist),
				    DRM_MEM_BUFS );
	for ( i = 0 ; i < entry->buf_count ; i++ ) {
		dma->buflist[i + dma->buf_count] = &entry->buflist[i];
	}

	dma->buf_count += entry->buf_count;
	dma->byte_count += byte_count;

	DRM_DEBUG( "dma->buf_count : %d\n", dma->buf_count );
	DRM_DEBUG( "entry->buf_count : %d\n", entry->buf_count );

	drm_freelist_create( &entry->freelist, entry->buf_count );
	for ( i = 0 ; i < entry->buf_count ; i++ ) {
		drm_freelist_put( dev, &entry->freelist, &entry->buflist[i] );
	}

	up( &dev->struct_sem );

	request.count = entry->buf_count;
	request.size = size;

	if ( copy_to_user( (drm_buf_desc_t *)arg, &request, sizeof(request) ) )
		return -EFAULT;

	dma->flags = _DRM_DMA_USE_AGP;

	atomic_dec( &dev->buf_alloc );
	return 0;
}
#endif

#if HAVE_PCI_DMA
int TAG(addbufs_pci)( struct inode *inode, struct file *filp,
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
	drm_buf_t *buf;
	int alignment;
	unsigned long offset;
	int i;
	int byte_count;
	int page_count;

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
	if ( entry->buf_count ) {
		up( &dev->struct_sem );
		atomic_dec( &dev->buf_alloc );
		return -ENOMEM;	/* May only call once for each order */
	}

	entry->buflist = drm_alloc( count * sizeof(*entry->buflist),
				    DRM_MEM_BUFS );
	if ( !entry->buflist ) {
		up( &dev->struct_sem );
		atomic_dec( &dev->buf_alloc );
		return -ENOMEM;
	}
	memset( entry->buflist, 0, count * sizeof(*entry->buflist) );

	entry->seglist = drm_alloc( count * sizeof(*entry->seglist),
				    DRM_MEM_SEGS );
	if ( !entry->seglist ) {
		drm_free( entry->buflist,
			  count * sizeof(*entry->buflist),
			  DRM_MEM_BUFS );
		up( &dev->struct_sem );
		atomic_dec( &dev->buf_alloc );
		return -ENOMEM;
	}
	memset( entry->seglist, 0, count * sizeof(*entry->seglist) );

	dma->pagelist = drm_realloc( dma->pagelist,
				     dma->page_count * sizeof(*dma->pagelist),
				     (dma->page_count + (count << page_order))
				     * sizeof(*dma->pagelist),
				     DRM_MEM_PAGES );
	DRM_DEBUG( "pagelist: %d entries\n",
		   dma->page_count + (count << page_order) );

	entry->buf_size	= size;
	entry->page_order = page_order;
	byte_count = 0;
	page_count = 0;

	while ( entry->buf_count < count ) {
		page = drm_alloc_pages( page_order, DRM_MEM_DMA );
		if ( !page ) break;
		entry->seglist[entry->seg_count++] = page;
		for ( i = 0 ; i < (1 << page_order) ; i++ ) {
			DRM_DEBUG( "page %d @ 0x%08lx\n",
				   dma->page_count + page_count,
				   page + PAGE_SIZE * i );
			dma->pagelist[dma->page_count + page_count++]
				= page + PAGE_SIZE * i;
		}
		for ( offset = 0 ;
		      offset + size <= total && entry->buf_count < count ;
		      offset += alignment, ++entry->buf_count ) {
			buf	     = &entry->buflist[entry->buf_count];
			buf->idx     = dma->buf_count + entry->buf_count;
			buf->total   = alignment;
			buf->order   = order;
			buf->used    = 0;
			buf->offset  = (dma->byte_count + byte_count + offset);
			buf->address = (void *)(page + offset);
			buf->next    = NULL;
			buf->waiting = 0;
			buf->pending = 0;
			init_waitqueue_head( &buf->dma_wait );
			buf->pid     = 0;
#if DRM_DMA_HISTOGRAM
			buf->time_queued     = 0;
			buf->time_dispatched = 0;
			buf->time_completed  = 0;
			buf->time_freed	     = 0;
#endif
			DRM_DEBUG( "buffer %d @ %p\n",
				   entry->buf_count, buf->address );
		}
		byte_count += PAGE_SIZE << page_order;
	}

	dma->buflist = drm_realloc( dma->buflist,
				    dma->buf_count * sizeof(*dma->buflist),
				    (dma->buf_count + entry->buf_count)
				    * sizeof(*dma->buflist),
				    DRM_MEM_BUFS );
	for ( i = 0 ; i < entry->buf_count ; i++ ) {
		dma->buflist[i + dma->buf_count] = &entry->buflist[i];
	}

	dma->buf_count += entry->buf_count;
	dma->seg_count += entry->seg_count;
	dma->page_count += entry->seg_count << page_order;
	dma->byte_count += PAGE_SIZE * (entry->seg_count << page_order);

	drm_freelist_create( &entry->freelist, entry->buf_count );
	for ( i = 0 ; i < entry->buf_count ; i++ ) {
		drm_freelist_put( dev, &entry->freelist, &entry->buflist[i] );
	}

	up( &dev->struct_sem );

	request.count = entry->buf_count;
	request.size = size;

	if ( copy_to_user( (drm_buf_desc_t *)arg, &request, sizeof(request) ) )
		return -EFAULT;

	atomic_dec( &dev->buf_alloc );
	return 0;
}
#endif

int TAG(addbufs)( struct inode *inode, struct file *filp,
		  unsigned int cmd, unsigned long arg )
{
	drm_buf_desc_t request;

	if ( copy_from_user( &request, (drm_buf_desc_t *)arg,
			     sizeof(request) ) )
		return -EFAULT;

#if HAVE_AGP && defined(CONFIG_AGP) || defined(CONFIG_AGP_MODULE)
	if ( request.flags & _DRM_AGP_BUFFER )
		return TAG(addbufs_agp)( inode, filp, cmd, arg );
	else
#endif
#if HAVE_PCI_DMA
		return TAG(addbufs_pci)( inode, filp, cmd, arg );
#else
		return -EINVAL;
#endif
}

int TAG(mapbufs)( struct inode *inode, struct file *filp,
		  unsigned int cmd, unsigned long arg )
{
	drm_file_t		*priv		= filp->private_data;
	drm_device_t		*dev		= priv->dev;
	DRIVER_DEVICE_PRIVATE	*dev_priv	= dev->dev_private;
	drm_device_dma_t	*dma		= dev->dma;
	int			 retcode	= 0;
	const int		 zero		= 0;
	unsigned long		 virtual;
	unsigned long		 address;
	drm_buf_map_t		 request;
	int			 i;

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
#if HAVE_AGP
		if ( dma->flags & _DRM_DMA_USE_AGP ) {
			drm_map_t *map;

			map = DRIVER_AGP_BUFFER_MAP;
			if ( !map ) {
				retcode = -EINVAL;
				goto done;
			}

			down( &current->mm->mmap_sem );
			virtual = do_mmap( filp, 0, map->size,
					   PROT_READ | PROT_WRITE,
					   MAP_SHARED,
					   (unsigned long)map->offset );
			up( &current->mm->mmap_sem );
		} else
#endif
		{
			down( &current->mm->mmap_sem );
			virtual = do_mmap( filp, 0, dma->byte_count,
					   PROT_READ | PROT_WRITE,
					   MAP_SHARED, 0 );
			up( &current->mm->mmap_sem );
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
