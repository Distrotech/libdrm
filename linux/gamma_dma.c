/* gamma_dma.c -- DMA support for GMX 2000 -*- linux-c -*-
 * Created: Fri Mar 19 14:30:16 1999 by faith@precisioninsight.com
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
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
 *    Rickard E. (Rik) Faith <faith@valinux.com>
 *
 */

#define __NO_VERSION__
#include "gamma.h"
#include "drmP.h"
#include "gamma_drv.h"

#include <linux/interrupt.h>	/* For task queue support */
#include <linux/delay.h>

#define OLDDMA 	1
#define QUEUED_DMA 1

static inline void gamma_dma_dispatch(drm_device_t *dev, unsigned long address,
				      unsigned long length)
{
#if !QUEUED_DMA
	GAMMA_WRITE(GAMMA_DMAADDRESS, virt_to_phys((void *)address));
	while (GAMMA_READ(GAMMA_GCOMMANDSTATUS) != 4);
	GAMMA_WRITE(GAMMA_DMACOUNT, length / 4);
#else
	mb();
	while ( GAMMA_READ(GAMMA_INFIFOSPACE) < 6);
	GAMMA_WRITE(GAMMA_OUTPUTFIFO, GAMMA_DMAADDRTAG);
	if (!dev->agp) {
		GAMMA_WRITE(GAMMA_OUTPUTFIFO+4, virt_to_phys((void*)address));
	} else {
		GAMMA_WRITE(GAMMA_OUTPUTFIFO+4, address);
	}
	GAMMA_WRITE(GAMMA_OUTPUTFIFO+8, GAMMA_DMACOUNTTAG);
	GAMMA_WRITE(GAMMA_OUTPUTFIFO+12, length / 4);
	GAMMA_WRITE(GAMMA_OUTPUTFIFO+16, GAMMA_COMMANDINTTAG);
	GAMMA_WRITE(GAMMA_OUTPUTFIFO+20, 1); /* PAUSE DMA UNTIL COMPLETE */
#endif
}

void gamma_dma_quiescent_single(drm_device_t *dev)
{
	while (GAMMA_READ(GAMMA_DMACOUNT));

	while (GAMMA_READ(GAMMA_INFIFOSPACE) < 2);

	GAMMA_WRITE(GAMMA_FILTERMODE, 1 << 10);
	GAMMA_WRITE(GAMMA_SYNC, 0);

	do {
		while (!GAMMA_READ(GAMMA_OUTFIFOWORDS))
			;
	} while (GAMMA_READ(GAMMA_OUTPUTFIFO) != GAMMA_SYNC_TAG);
}

void gamma_dma_quiescent_dual(drm_device_t *dev)
{
	while (GAMMA_READ(GAMMA_DMACOUNT));

	while (GAMMA_READ(GAMMA_INFIFOSPACE) < 3);

	GAMMA_WRITE(GAMMA_BROADCASTMASK, 3);
	GAMMA_WRITE(GAMMA_FILTERMODE, 1 << 10);
	GAMMA_WRITE(GAMMA_SYNC, 0);

	/* Read from first MX */
	do {
		while (!GAMMA_READ(GAMMA_OUTFIFOWORDS));
	} while (GAMMA_READ(GAMMA_OUTPUTFIFO) != GAMMA_SYNC_TAG);

	/* Read from second MX */
	do {
		while (!GAMMA_READ(GAMMA_OUTFIFOWORDS + 0x10000));
	} while (GAMMA_READ(GAMMA_OUTPUTFIFO + 0x10000) != GAMMA_SYNC_TAG);
}

void gamma_dma_ready(drm_device_t *dev)
{
#if !QUEUED_DMA
	while (GAMMA_READ(GAMMA_DMACOUNT));
#endif
}

static inline int gamma_dma_is_ready(drm_device_t *dev)
{
#if !QUEUED_DMA
	return(!GAMMA_READ(GAMMA_DMACOUNT));
#else
	return(GAMMA_READ(GAMMA_GCOMMANDSTATUS) & 0x04);
#endif
}

void gamma_dma_service(int irq, void *device, struct pt_regs *regs)
{
	drm_device_t	 *dev = (drm_device_t *)device;
	drm_device_dma_t *dma = dev->dma;

	atomic_inc(&dev->counts[6]); /* _DRM_STAT_IRQ */

#if !QUEUED_DMA
	GAMMA_WRITE(GAMMA_GDELAYTIMER, 0xc350/2); /* 0x05S */
	GAMMA_WRITE(GAMMA_GCOMMANDINTFLAGS, 8);
	GAMMA_WRITE(GAMMA_GINTFLAGS, 0x2001);
#else
	GAMMA_WRITE(GAMMA_GCOMMANDINTFLAGS, 4);
	GAMMA_WRITE(GAMMA_GINTFLAGS, 0x2000);
#endif
	if (gamma_dma_is_ready(dev)) {
				/* Free previous buffer */
		if (test_and_set_bit(0, &dev->dma_flag)) return;
		if (dma->this_buffer) {
			gamma_free_buffer(dev, dma->this_buffer);
			dma->this_buffer = NULL;
		}
		clear_bit(0, &dev->dma_flag);

				/* Dispatch new buffer */
		queue_task(&dev->tq, &tq_immediate);
		mark_bh(IMMEDIATE_BH);
	}
}

/* Only called by gamma_dma_schedule. */
static int gamma_do_dma(drm_device_t *dev, int locked)
{
	unsigned long	 address;
	unsigned long	 length;
	drm_buf_t	 *buf;
	int		 retcode = 0;
	drm_device_dma_t *dma = dev->dma;
#if DRM_DMA_HISTOGRAM
	cycles_t	 dma_start, dma_stop;
#endif

	if (test_and_set_bit(0, &dev->dma_flag)) return -EBUSY;

#if DRM_DMA_HISTOGRAM
	dma_start = get_cycles();
#endif

	if (!dma->next_buffer) {
		DRM_ERROR("No next_buffer\n");
		clear_bit(0, &dev->dma_flag);
		return -EINVAL;
	}

	buf	= dma->next_buffer;
	address = (unsigned long)buf->address;
	length	= buf->used;

	DRM_DEBUG("context %d, buffer %d (%ld bytes)\n",
		  buf->context, buf->idx, length);

	if (buf->list == DRM_LIST_RECLAIM) {
		gamma_clear_next_buffer(dev);
		gamma_free_buffer(dev, buf);
		clear_bit(0, &dev->dma_flag);
		return -EINVAL;
	}

	if (!length) {
		DRM_ERROR("0 length buffer\n");
		gamma_clear_next_buffer(dev);
		gamma_free_buffer(dev, buf);
		clear_bit(0, &dev->dma_flag);
		return 0;
	}

	if (!gamma_dma_is_ready(dev)) {
		clear_bit(0, &dev->dma_flag);
		return -EBUSY;
	}

	if (buf->while_locked) {
		if (!_DRM_LOCK_IS_HELD(dev->lock.hw_lock->lock)) {
			DRM_ERROR("Dispatching buffer %d from pid %d"
				  " \"while locked\", but no lock held\n",
				  buf->idx, buf->pid);
		}
	} else {
		if (!locked && !gamma_lock_take(&dev->lock.hw_lock->lock,
					      DRM_KERNEL_CONTEXT)) {
			clear_bit(0, &dev->dma_flag);
			return -EBUSY;
		}
	}

	if (dev->last_context != buf->context
	    && !(dev->queuelist[buf->context]->flags
		 & _DRM_CONTEXT_PRESERVED)) {
				/* PRE: dev->last_context != buf->context */
		if (DRM(context_switch)(dev, dev->last_context,
					buf->context)) {
			DRM(clear_next_buffer)(dev);
			DRM(free_buffer)(dev, buf);
		}
		retcode = -EBUSY;
		goto cleanup;

				/* POST: we will wait for the context
				   switch and will dispatch on a later call
				   when dev->last_context == buf->context.
				   NOTE WE HOLD THE LOCK THROUGHOUT THIS
				   TIME! */
	}

	gamma_clear_next_buffer(dev);
	buf->pending	 = 1;
	buf->waiting	 = 0;
	buf->list	 = DRM_LIST_PEND;
#if DRM_DMA_HISTOGRAM
	buf->time_dispatched = get_cycles();
#endif

	gamma_dma_dispatch(dev, address, length);
	gamma_free_buffer(dev, dma->this_buffer);
	dma->this_buffer = buf;

	atomic_inc(&dev->counts[7]); /* _DRM_STAT_DMA */
	atomic_add(length, &dev->counts[8]); /* _DRM_STAT_PRIMARY */

	if (!buf->while_locked && !dev->context_flag && !locked) {
		if (gamma_lock_free(dev, &dev->lock.hw_lock->lock,
				  DRM_KERNEL_CONTEXT)) {
			DRM_ERROR("\n");
		}
	}
cleanup:

	clear_bit(0, &dev->dma_flag);

#if DRM_DMA_HISTOGRAM
	dma_stop = get_cycles();
	atomic_inc(&dev->histo.dma[gamma_histogram_slot(dma_stop - dma_start)]);
#endif

	return retcode;
}

static void gamma_dma_timer_bh(unsigned long dev)
{
	gamma_dma_schedule((drm_device_t *)dev, 0);
}

void gamma_dma_immediate_bh(void *dev)
{
	gamma_dma_schedule(dev, 0);
}

int gamma_dma_schedule(drm_device_t *dev, int locked)
{
	int		 next;
	drm_queue_t	 *q;
	drm_buf_t	 *buf;
	int		 retcode   = 0;
	int		 processed = 0;
	int		 missed;
	int		 expire	   = 20;
	drm_device_dma_t *dma	   = dev->dma;
#if DRM_DMA_HISTOGRAM
	cycles_t	 schedule_start;
#endif

#if OLDDMA
	if (test_and_set_bit(0, &dev->interrupt_flag)) {
				/* Not reentrant */
		atomic_inc(&dev->counts[10]); /* _DRM_STAT_MISSED */
		return -EBUSY;
	}
	missed = atomic_read(&dev->counts[10]);
#endif

#if DRM_DMA_HISTOGRAM
	schedule_start = get_cycles();
#endif

again:
	if (dev->context_flag) {
		clear_bit(0, &dev->interrupt_flag);
		return -EBUSY;
	}
	if (dma->next_buffer) {
				/* Unsent buffer that was previously
				   selected, but that couldn't be sent
				   because the lock could not be obtained
				   or the DMA engine wasn't ready.  Try
				   again. */
		if (!(retcode = gamma_do_dma(dev, locked))) ++processed;
	} else {
		do {
			next = gamma_select_queue(dev, gamma_dma_timer_bh);
			if (next >= 0) {
				q   = dev->queuelist[next];
				buf = gamma_waitlist_get(&q->waitlist);
				dma->next_buffer = buf;
				dma->next_queue	 = q;
				if (buf && buf->list == DRM_LIST_RECLAIM) {
					gamma_clear_next_buffer(dev);
					gamma_free_buffer(dev, buf);
				}
			}
		} while (next >= 0 && !dma->next_buffer);
		if (dma->next_buffer) {
			if (!(retcode = gamma_do_dma(dev, locked))) {
				++processed;
			}
		}
	}

	if (--expire) {
		if (missed != atomic_read(&dev->counts[10])) {
			if (gamma_dma_is_ready(dev)) goto again;
		}
		if (processed && gamma_dma_is_ready(dev)) {
			processed = 0;
			goto again;
		}
	}

	clear_bit(0, &dev->interrupt_flag);

#if DRM_DMA_HISTOGRAM
	atomic_inc(&dev->histo.schedule[gamma_histogram_slot(get_cycles()
							   - schedule_start)]);
#endif
	return retcode;
}

#if OLDDMA
static int gamma_dma_priority(drm_device_t *dev, drm_dma_t *d)
{
	unsigned long	  address;
	unsigned long	  length;
	int		  must_free = 0;
	int		  retcode   = 0;
	int		  i;
	int		  idx;
	drm_buf_t	  *buf;
	drm_buf_t	  *last_buf = NULL;
	drm_device_dma_t  *dma	    = dev->dma;
	DECLARE_WAITQUEUE(entry, current);

				/* Turn off interrupt handling */
	while (test_and_set_bit(0, &dev->interrupt_flag)) {
		schedule();
		if (signal_pending(current)) return -EINTR;
	}
	if (!(d->flags & _DRM_DMA_WHILE_LOCKED)) {
		while (!gamma_lock_take(&dev->lock.hw_lock->lock,
				      DRM_KERNEL_CONTEXT)) {
			schedule();
			if (signal_pending(current)) {
				clear_bit(0, &dev->interrupt_flag);
				return -EINTR;
			}
		}
		++must_free;
	}

	for (i = 0; i < d->send_count; i++) {
		idx = d->send_indices[i];
		if (idx < 0 || idx >= dma->buf_count) {
			DRM_ERROR("Index %d (of %d max)\n",
				  d->send_indices[i], dma->buf_count - 1);
			continue;
		}
		buf = dma->buflist[ idx ];
		if (buf->pid != current->pid) {
			DRM_ERROR("Process %d using buffer owned by %d\n",
				  current->pid, buf->pid);
			retcode = -EINVAL;
			goto cleanup;
		}
		if (buf->list != DRM_LIST_NONE) {
			DRM_ERROR("Process %d using %d's buffer on list %d\n",
				  current->pid, buf->pid, buf->list);
			retcode = -EINVAL;
			goto cleanup;
		}
				/* This isn't a race condition on
				   buf->list, since our concern is the
				   buffer reclaim during the time the
				   process closes the /dev/drm? handle, so
				   it can't also be doing DMA. */
		buf->list	  = DRM_LIST_PRIO;
		buf->used	  = d->send_sizes[i];
		buf->context	  = d->context;
		buf->while_locked = d->flags & _DRM_DMA_WHILE_LOCKED;
		address		  = (unsigned long)buf->address;
		length		  = buf->used;
		if (!length) {
			DRM_ERROR("0 length buffer\n");
		}
		if (buf->pending) {
			DRM_ERROR("Sending pending buffer:"
				  " buffer %d, offset %d\n",
				  d->send_indices[i], i);
			retcode = -EINVAL;
			goto cleanup;
		}
		if (buf->waiting) {
			DRM_ERROR("Sending waiting buffer:"
				  " buffer %d, offset %d\n",
				  d->send_indices[i], i);
			retcode = -EINVAL;
			goto cleanup;
		}
		buf->pending = 1;

		if (dev->last_context != buf->context
		    && !(dev->queuelist[buf->context]->flags
			 & _DRM_CONTEXT_PRESERVED)) {
			add_wait_queue(&dev->context_wait, &entry);
			current->state = TASK_INTERRUPTIBLE;
				/* PRE: dev->last_context != buf->context */
			DRM(context_switch)(dev, dev->last_context,
					    buf->context);
				/* POST: we will wait for the context
				   switch and will dispatch on a later call
				   when dev->last_context == buf->context.
				   NOTE WE HOLD THE LOCK THROUGHOUT THIS
				   TIME! */
			schedule();
			current->state = TASK_RUNNING;
			remove_wait_queue(&dev->context_wait, &entry);
			if (signal_pending(current)) {
				retcode = -EINTR;
				goto cleanup;
			}
			if (dev->last_context != buf->context) {
				DRM_ERROR("Context mismatch: %d %d\n",
					  dev->last_context,
					  buf->context);
			}
		}

#if DRM_DMA_HISTOGRAM
		buf->time_queued     = get_cycles();
		buf->time_dispatched = buf->time_queued;
#endif
		gamma_dma_dispatch(dev, address, length);
		atomic_inc(&dev->counts[9]); /* _DRM_STAT_SPECIAL */
		atomic_add(length, &dev->counts[8]); /* _DRM_STAT_PRIMARY */

		if (last_buf) {
			gamma_free_buffer(dev, last_buf);
		}
		last_buf = buf;
	}


cleanup:
	if (last_buf) {
		gamma_dma_ready(dev);
		gamma_free_buffer(dev, last_buf);
	}

	if (must_free && !dev->context_flag) {
		if (gamma_lock_free(dev, &dev->lock.hw_lock->lock,
				  DRM_KERNEL_CONTEXT)) {
			DRM_ERROR("\n");
		}
	}
	clear_bit(0, &dev->interrupt_flag);
	return retcode;
}
#endif

static int gamma_dma_send_buffers(drm_device_t *dev, drm_dma_t *d)
{
	DECLARE_WAITQUEUE(entry, current);
	drm_buf_t	  *last_buf = NULL;
	int		  retcode   = 0;
	drm_device_dma_t  *dma	    = dev->dma;

#if OLDDMA
	if (d->flags & _DRM_DMA_BLOCK) {
		last_buf = dma->buflist[d->send_indices[d->send_count-1]];
		add_wait_queue(&last_buf->dma_wait, &entry);
	}

	if ((retcode = gamma_dma_enqueue(dev, d))) {
		if (d->flags & _DRM_DMA_BLOCK)
			remove_wait_queue(&last_buf->dma_wait, &entry);
		return retcode;
	}

	gamma_dma_schedule(dev, 0);

	if (d->flags & _DRM_DMA_BLOCK) {
		DRM_DEBUG("%d waiting\n", current->pid);
		for (;;) {
			current->state = TASK_INTERRUPTIBLE;
			if (!last_buf->waiting && !last_buf->pending)
				break; /* finished */
			schedule();
			if (signal_pending(current)) {
				retcode = -EINTR; /* Can't restart */
				break;
			}
		}
		current->state = TASK_RUNNING;
		DRM_DEBUG("%d running\n", current->pid);
		remove_wait_queue(&last_buf->dma_wait, &entry);
		if (!retcode
		    || (last_buf->list==DRM_LIST_PEND && !last_buf->pending)) {
			if (!waitqueue_active(&last_buf->dma_wait)) {
				gamma_free_buffer(dev, last_buf);
			}
		}
		if (retcode) {
			DRM_ERROR("ctx%d w%d p%d c%d i%d l%d %d/%d\n",
				  d->context,
				  last_buf->waiting,
				  last_buf->pending,
				  DRM_WAITCOUNT(dev, d->context),
				  last_buf->idx,
				  last_buf->list,
				  last_buf->pid,
				  current->pid);
		}
	}
#endif
	return retcode;
}

int gamma_dma(struct inode *inode, struct file *filp, unsigned int cmd,
	      unsigned long arg)
{
	drm_file_t	  *priv	    = filp->private_data;
	drm_device_t	  *dev	    = priv->dev;
	drm_device_dma_t  *dma	    = dev->dma;
	int		  retcode   = 0;
	drm_dma_t	  d;

	if (copy_from_user(&d, (drm_dma_t *)arg, sizeof(d)))
		return -EFAULT;

	if (d.send_count < 0 || d.send_count > dma->buf_count) {
		DRM_ERROR("Process %d trying to send %d buffers (of %d max)\n",
			  current->pid, d.send_count, dma->buf_count);
		return -EINVAL;
	}

	if (d.request_count < 0 || d.request_count > dma->buf_count) {
		DRM_ERROR("Process %d trying to get %d buffers (of %d max)\n",
			  current->pid, d.request_count, dma->buf_count);
		return -EINVAL;
	}

	if (d.send_count) {
#if OLDDMA 
		if (d.flags & _DRM_DMA_PRIORITY)
			retcode = gamma_dma_priority(dev, &d);
		else
#endif
			retcode = gamma_dma_send_buffers(dev, &d);
	}

	d.granted_count = 0;

	if (!retcode && d.request_count) {
		retcode = gamma_dma_get_buffers(dev, &d);
	}

	DRM_DEBUG("%d returning, granted = %d\n",
		  current->pid, d.granted_count);
	if (copy_to_user((drm_dma_t *)arg, &d, sizeof(d)))
		return -EFAULT;

	return retcode;
}

/* ================================================================
 * DMA initialization, cleanup
 */

static int gamma_do_init_dma( drm_device_t *dev, drm_gamma_init_t *init )
{
	drm_gamma_private_t *dev_priv;
	int ret;
	DRM_DEBUG( "%s\n", __FUNCTION__ );

	dev_priv = DRM(alloc)( sizeof(drm_gamma_private_t), DRM_MEM_DRIVER );
	if ( !dev_priv )
		return -ENOMEM;
	dev->dev_private = (void *)dev_priv;

	memset( dev_priv, 0, sizeof(drm_gamma_private_t) );

#if 0
	dev_priv->chipset = init->chipset;

	dev_priv->usec_timeout = MGA_DEFAULT_USEC_TIMEOUT;

	if ( init->sgram ) {
		dev_priv->clear_cmd = MGA_DWGCTL_CLEAR | MGA_ATYPE_BLK;
	} else {
		dev_priv->clear_cmd = MGA_DWGCTL_CLEAR | MGA_ATYPE_RSTR;
	}
	dev_priv->maccess	= init->maccess;

	dev_priv->fb_cpp	= init->fb_cpp;
	dev_priv->front_offset	= init->front_offset;
	dev_priv->front_pitch	= init->front_pitch;
	dev_priv->back_offset	= init->back_offset;
	dev_priv->back_pitch	= init->back_pitch;

	dev_priv->depth_cpp	= init->depth_cpp;
	dev_priv->depth_offset	= init->depth_offset;
	dev_priv->depth_pitch	= init->depth_pitch;
#endif

	dev_priv->sarea = dev->maplist[0];

#if 0
	DRM_FIND_MAP( dev_priv->fb, init->fb_offset );
	DRM_FIND_MAP( dev_priv->mmio, init->mmio_offset );
	DRM_FIND_MAP( dev_priv->status, init->status_offset );

	DRM_FIND_MAP( dev_priv->warp, init->warp_offset );
	DRM_FIND_MAP( dev_priv->primary, init->primary_offset );
#endif
	DRM_FIND_MAP( dev_priv->buffers, init->buffers_offset );

	dev_priv->sarea_priv =
		(drm_gamma_sarea_t *)((u8 *)dev_priv->sarea->handle +
				    init->sarea_priv_offset);

#if 0
	DRM_IOREMAP( dev_priv->warp );
	DRM_IOREMAP( dev_priv->primary );
#endif
	DRM_IOREMAP( dev_priv->buffers );

	GAMMA_WRITE( GAMMA_GDMACONTROL, GAMMA_USE_AGP );
#if 0
	dev_priv->prim.status = (u32 *)dev_priv->status->handle;

	mga_do_wait_for_idle( dev_priv );

	/* Init the primary DMA registers.
	 */
	MGA_WRITE( MGA_PRIMADDRESS,
		   dev_priv->primary->offset | MGA_DMA_GENERAL );

	MGA_WRITE( MGA_PRIMPTR,
		   virt_to_bus((void *)dev_priv->prim.status) |
		   MGA_PRIMPTREN0 |	/* Soft trap, SECEND, SETUPEND */
		   MGA_PRIMPTREN1 );	/* DWGSYNC */

	dev_priv->prim.start = (u8 *)dev_priv->primary->handle;
	dev_priv->prim.end = ((u8 *)dev_priv->primary->handle
			      + dev_priv->primary->size);
	dev_priv->prim.size = dev_priv->primary->size;

	dev_priv->prim.head = &dev_priv->prim.status[0];
	dev_priv->prim.tail = 0;
	dev_priv->prim.space = dev_priv->prim.size;

	dev_priv->prim.last_flush = 0;
	dev_priv->prim.last_wrap = 0;

	dev_priv->prim.high_mark = 256 * DMA_BLOCK_SIZE;

	spin_lock_init( &dev_priv->prim.list_lock );

	dev_priv->prim.status[0] = dev_priv->primary->offset;
	dev_priv->prim.status[1] = 0;

	dev_priv->sarea_priv->last_wrap = 0;
	dev_priv->sarea_priv->last_frame.head = 0;
	dev_priv->sarea_priv->last_frame.wrap = 0;

	if ( mga_freelist_init( dev ) < 0 ) {
		DRM_ERROR( "could not initialize freelist\n" );
		mga_do_cleanup_dma( dev );
		return -ENOMEM;
	}
#endif

	return 0;
}

int gamma_do_cleanup_dma( drm_device_t *dev )
{
	DRM_DEBUG( "%s\n", __FUNCTION__ );

	if ( dev->dev_private ) {
		drm_gamma_private_t *dev_priv = dev->dev_private;

#if 0
		DRM_IOREMAPFREE( dev_priv->warp );
		DRM_IOREMAPFREE( dev_priv->primary );
#endif
		DRM_IOREMAPFREE( dev_priv->buffers );

#if 0
		if ( dev_priv->head != NULL ) {
			mga_freelist_cleanup( dev );
		}
#endif

		DRM(free)( dev->dev_private, sizeof(drm_gamma_private_t),
			   DRM_MEM_DRIVER );
		dev->dev_private = NULL;
	}

	return 0;
}

int gamma_dma_init( struct inode *inode, struct file *filp,
		  unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_gamma_init_t init;

	if ( copy_from_user( &init, (drm_gamma_init_t *)arg, sizeof(init) ) )
		return -EFAULT;

	switch ( init.func ) {
	case GAMMA_INIT_DMA:
		return gamma_do_init_dma( dev, &init );
	case GAMMA_CLEANUP_DMA:
		return gamma_do_cleanup_dma( dev );
	}

	return -EINVAL;
}
