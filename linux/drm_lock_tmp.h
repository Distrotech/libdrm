/**
 * \file drm_lock_tmp.h 
 * Locking IOCTL support
 * 
 * \author Rickard E. (Rik) Faith <faith@valinux.com>
 * \author Gareth Hughes <gareth@valinux.com>
 */

/*
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
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */


#define __NO_VERSION__
#include "drmP.h"

/** No-op ioctl. */
int DRM(noop)(struct inode *inode, struct file *filp, unsigned int cmd,
	       unsigned long arg)
{
	DRM_DEBUG("\n");
	return 0;
}

/**
 * Take the heavyweight lock.
 *
 * \param lock lock pointer.
 * \param context locking context.
 * \return one if the lock is held, or zero otherwise.
 *
 * Attempt to mark the lock as held by the given context, via the \p cmpxchg instruction.
 */
int DRM(lock_take)(__volatile__ unsigned int *lock, unsigned int context)
{
	unsigned int old, new, prev;

	do {
		old = *lock;
		if (old & _DRM_LOCK_HELD) new = old | _DRM_LOCK_CONT;
		else			  new = context | _DRM_LOCK_HELD;
		prev = cmpxchg(lock, old, new);
	} while (prev != old);
	if (_DRM_LOCKING_CONTEXT(old) == context) {
		if (old & _DRM_LOCK_HELD) {
			if (context != DRM_KERNEL_CONTEXT) {
				DRM_ERROR("%d holds heavyweight lock\n",
					  context);
			}
			return 0;
		}
	}
	if (new == (context | _DRM_LOCK_HELD)) {
				/* Have lock */
		return 1;
	}
	return 0;
}

/**
 * This takes a lock forcibly and hands it to context.	Should ONLY be used
 * inside *_unlock to give lock to kernel before calling *_dma_schedule. 
 * 
 * \param dev DRM device.
 * \param lock lock pointer.
 * \param context locking context.
 * \return always one.
 *
 * Resets the lock file pointer.
 * Marks the lock as held by the given context, via the \p cmpxchg instruction.
 */
int DRM(lock_transfer)(drm_device_t *dev,
		       __volatile__ unsigned int *lock, unsigned int context)
{
	unsigned int old, new, prev;

	dev->lock.filp = 0;
	do {
		old  = *lock;
		new  = context | _DRM_LOCK_HELD;
		prev = cmpxchg(lock, old, new);
	} while (prev != old);
	return 1;
}

/**
 * Free lock.
 * 
 * \param dev DRM device.
 * \param lock lock.
 * \param context context.
 * 
 * Resets the lock file pointer.
 * Marks the lock as not held, via the \p cmpxchg instruction. Wakes any task
 * waiting on the lock queue.
 */
int DRM(lock_free)(drm_device_t *dev,
		   __volatile__ unsigned int *lock, unsigned int context)
{
	unsigned int old, new, prev;

	dev->lock.filp = 0;
	do {
		old  = *lock;
		new  = 0;
		prev = cmpxchg(lock, old, new);
	} while (prev != old);
	if (_DRM_LOCK_IS_HELD(old) && _DRM_LOCKING_CONTEXT(old) != context) {
		DRM_ERROR("%d freed heavyweight lock held by %d\n",
			  context,
			  _DRM_LOCKING_CONTEXT(old));
		return 1;
	}
	wake_up_interruptible(&dev->lock.lock_queue);
	return 0;
}

/**
 * If we get here, it means that the process has called DRM_IOCTL_LOCK
 * without calling DRM_IOCTL_UNLOCK.
 *
 * If the lock is not held, then let the signal proceed as usual.  If the lock
 * is held, then set the contended flag and keep the signal blocked.
 *
 * \param priv pointer to a drm_sigdata structure.
 * \return one if the signal should be delivered normally, or zero if the
 * signal should be blocked.
 */
int DRM(notifier)(void *priv)
{
	drm_sigdata_t *s = (drm_sigdata_t *)priv;
	unsigned int  old, new, prev;


				/* Allow signal delivery if lock isn't held */
	if (!s->lock || !_DRM_LOCK_IS_HELD(s->lock->lock)
	    || _DRM_LOCKING_CONTEXT(s->lock->lock) != s->context) return 1;

				/* Otherwise, set flag to force call to
                                   drmUnlock */
	do {
		old  = s->lock->lock;
		new  = old | _DRM_LOCK_CONT;
		prev = cmpxchg(&s->lock->lock, old, new);
	} while (prev != old);
	return 0;
}


/** 
 * Lock ioctl.
 *
 * \param inode device inode.
 * \param filp file pointer.
 * \param cmd command.
 * \param arg user argument, pointing to a drm_lock structure.
 * \return zero on success or negative number on failure.
 *
 * Add the current task to the lock wait queue, and attempt to take to lock.
 */
int DRM(lock_ioctl)( struct inode *inode, struct file *filp,
	       unsigned int cmd, unsigned long arg )
{
        drm_file_t *priv = filp->private_data;
        drm_device_t *dev = priv->dev;
        DECLARE_WAITQUEUE( entry, current );
        drm_lock_t lock;
        int ret = 0;
#if __HAVE_MULTIPLE_DMA_QUEUES
	drm_queue_t *q;
#endif

	++priv->lock_count;

        if ( copy_from_user( &lock, (drm_lock_t *)arg, sizeof(lock) ) )
		return -EFAULT;

        if ( lock.context == DRM_KERNEL_CONTEXT ) {
                DRM_ERROR( "Process %d using kernel context %d\n",
			   current->pid, lock.context );
                return -EINVAL;
        }

        DRM_DEBUG( "%d (pid %d) requests lock (0x%08x), flags = 0x%08x\n",
		   lock.context, current->pid,
		   dev->lock.hw_lock->lock, lock.flags );

#if __HAVE_DMA_QUEUE
        if ( lock.context < 0 )
                return -EINVAL;
#elif __HAVE_MULTIPLE_DMA_QUEUES
        if ( lock.context < 0 || lock.context >= dev->queue_count )
                return -EINVAL;
	q = dev->queuelist[lock.context];
#endif

#if __HAVE_DMA_FLUSH
	ret = DRM(flush_block_and_flush)( dev, lock.context, lock.flags );
#endif
        if ( !ret ) {
                add_wait_queue( &dev->lock.lock_queue, &entry );
                for (;;) {
                        current->state = TASK_INTERRUPTIBLE;
                        if ( !dev->lock.hw_lock ) {
                                /* Device has been unregistered */
                                ret = -EINTR;
                                break;
                        }
                        if ( DRM(lock_take)( &dev->lock.hw_lock->lock,
					     lock.context ) ) {
                                dev->lock.filp      = filp;
                                dev->lock.lock_time = jiffies;
                                atomic_inc( &dev->counts[_DRM_STAT_LOCKS] );
                                break;  /* Got lock */
                        }

                                /* Contention */
                        schedule();
                        if ( signal_pending( current ) ) {
                                ret = -ERESTARTSYS;
                                break;
                        }
                }
                current->state = TASK_RUNNING;
                remove_wait_queue( &dev->lock.lock_queue, &entry );
        }

#if __HAVE_DMA_FLUSH
	DRM(flush_unblock)( dev, lock.context, lock.flags ); /* cleanup phase */
#endif

        if ( !ret ) {
		sigemptyset( &dev->sigmask );
		sigaddset( &dev->sigmask, SIGSTOP );
		sigaddset( &dev->sigmask, SIGTSTP );
		sigaddset( &dev->sigmask, SIGTTIN );
		sigaddset( &dev->sigmask, SIGTTOU );
		dev->sigdata.context = lock.context;
		dev->sigdata.lock    = dev->lock.hw_lock;
		block_all_signals( DRM(notifier),
				   &dev->sigdata, &dev->sigmask );

#if __HAVE_DMA_READY
                if ( lock.flags & _DRM_LOCK_READY ) {
			DRIVER_DMA_READY();
		}
#endif
#if __HAVE_DMA_QUIESCENT
                if ( lock.flags & _DRM_LOCK_QUIESCENT ) {
			DRIVER_DMA_QUIESCENT();
		}
#endif
#if __HAVE_KERNEL_CTX_SWITCH
		if ( dev->last_context != lock.context ) {
			DRM(context_switch)(dev, dev->last_context,
					    lock.context);
		}
#endif
        }

        DRM_DEBUG( "%d %s\n", lock.context, ret ? "interrupted" : "has lock" );

        return ret;
}

/** 
 * Unlock ioctl.
 *
 * \param inode device inode.
 * \param filp file pointer.
 * \param cmd command.
 * \param arg user argument, pointing to a drm_lock structure.
 * \return zero on success or negative number on failure.
 *
 * Transfer and free the lock.
 */
int DRM(unlock_ioctl)( struct inode *inode, struct file *filp,
		 unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_lock_t lock;

	if ( copy_from_user( &lock, (drm_lock_t *)arg, sizeof(lock) ) )
		return -EFAULT;

	if ( lock.context == DRM_KERNEL_CONTEXT ) {
		DRM_ERROR( "Process %d using kernel context %d\n",
			   current->pid, lock.context );
		return -EINVAL;
	}

	atomic_inc( &dev->counts[_DRM_STAT_UNLOCKS] );

#if __HAVE_KERNEL_CTX_SWITCH
	/* We no longer really hold it, but if we are the next
	 * agent to request it then we should just be able to
	 * take it immediately and not eat the ioctl.
	 */
	dev->lock.filp = 0;
	{
		__volatile__ unsigned int *plock = &dev->lock.hw_lock->lock;
		unsigned int old, new, prev, ctx;

		ctx = lock.context;
		do {
			old  = *plock;
			new  = ctx;
			prev = cmpxchg(plock, old, new);
		} while (prev != old);
	}
	wake_up_interruptible(&dev->lock.lock_queue);
#else
	DRM(lock_transfer)( dev, &dev->lock.hw_lock->lock,
			    DRM_KERNEL_CONTEXT );
#if __HAVE_DMA_SCHEDULE
	DRM(dma_schedule)( dev, 1 );
#endif

	/* FIXME: Do we ever really need to check this???
	 */
	if ( 1 /* !dev->context_flag */ ) {
		if ( DRM(lock_free)( dev, &dev->lock.hw_lock->lock,
				     DRM_KERNEL_CONTEXT ) ) {
			DRM_ERROR( "\n" );
		}
	}
#endif /* !__HAVE_KERNEL_CTX_SWITCH */

	unblock_all_signals();
	return 0;
}
