/**
 * \file drm_fops.c 
 * File operations for the DRM device.
 * 
 * \author Rickard E. (Rik) Faith <faith@valinux.com>
 * \author Daryll Strauss <daryll@valinux.com>
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
#include <linux/poll.h>


struct file_operations DRM(fops) = {
	.owner   = THIS_MODULE,
	.open	 = DRM(open),
	.flush	 = DRM(flush),
	.release = DRM(release),
	.ioctl	 = DRM(ioctl),
	.mmap	 = DRM(mmap),
	.fasync  = DRM(fasync),
	.poll	 = DRM(poll),
	.read	 = DRM(read),
}

/**
 * Called whenever a process opens /dev/drm. 
 *
 * \param inode device inode.
 * \param filp file pointer.
 * \param dev device.
 * \return zero on success or a negative number on failure.
 * 
 * Creates and initializes a drm_file structure for the file private data in \p
 * filp and add it into the double linked list in \p dev.
 */
int DRM(open_helper)(struct inode *inode, struct file *filp, drm_device_t *dev)
{
	int	     minor = minor(inode->i_rdev);
	drm_file_t   *priv;

	if (filp->f_flags & O_EXCL)   return -EBUSY; /* No exclusive opens */
	if (!DRM(cpu_valid)())        return -EINVAL;

	DRM_DEBUG("pid = %d, minor = %d\n", current->pid, minor);

	priv		    = DRM(alloc)(sizeof(*priv), DRM_MEM_FILES);
	if(!priv) return -ENOMEM;

	memset(priv, 0, sizeof(*priv));
	filp->private_data  = priv;
	priv->uid	    = current->euid;
	priv->pid	    = current->pid;
	priv->minor	    = minor;
	priv->dev	    = dev;
	priv->ioctl_count   = 0;
	priv->authenticated = capable(CAP_SYS_ADMIN);
	priv->lock_count    = 0;

	down(&dev->struct_sem);
	if (!dev->file_last) {
		priv->next	= NULL;
		priv->prev	= NULL;
		dev->file_first = priv;
		dev->file_last	= priv;
	} else {
		priv->next	     = NULL;
		priv->prev	     = dev->file_last;
		dev->file_last->next = priv;
		dev->file_last	     = priv;
	}
	up(&dev->struct_sem);

#ifdef __alpha__
	/*
	 * Default the hose
	 */
	if (!dev->hose) {
		struct pci_dev *pci_dev;
		pci_dev = pci_find_class(PCI_CLASS_DISPLAY_VGA << 8, NULL);
		if (pci_dev) dev->hose = pci_dev->sysdata;
		if (!dev->hose) {
			struct pci_bus *b = pci_bus_b(pci_root_buses.next);
			if (b) dev->hose = b->sysdata;
		}
	}
#endif

	return 0;
}

/**
 * Open file.
 * 
 * \param inode device inode
 * \param filp file pointer.
 * \return zero on success or a negative number on failure.
 *
 * Searches the DRM device with the same minor number, calls open_helper(), and
 * increments the device open count. If the open count was previous at zero,
 * i.e., it's the first that the device is open, then calls setup().
 */
int DRM(open)( struct inode *inode, struct file *filp )
{
	drm_device_t *dev = NULL;
	int retcode = 0;
	int i;

	for (i = 0; i < DRM(numdevs); i++) {
		if (minor(inode->i_rdev) == DRM(minor)[i]) {
			dev = &(DRM(device)[i]);
			break;
		}
	}
	if (!dev) {
		return -ENODEV;
	}

	retcode = DRM(open_helper)( inode, filp, dev );
	if ( !retcode ) {
		atomic_inc( &dev->counts[_DRM_STAT_OPENS] );
		spin_lock( &dev->count_lock );
		if ( !dev->open_count++ ) {
			spin_unlock( &dev->count_lock );
			return DRM(setup)( dev );
		}
		spin_unlock( &dev->count_lock );
	}

	return retcode;
}

/** No-op. */
int DRM(flush)(struct file *filp)
{
	drm_file_t    *priv   = filp->private_data;
	drm_device_t  *dev    = priv->dev;

	DRM_DEBUG("pid = %d, device = 0x%lx, open_count = %d\n",
		  current->pid, (long)dev->device, dev->open_count);
	return 0;
}

/**
 * Release file.
 *
 * \param inode device inode
 * \param filp file pointer.
 * \return zero on success or a negative number on failure.
 *
 * If the hardware lock is held then free it, and take it again for the kernel
 * context since it's necessary to reclaim buffers. Unlink the file private
 * data from its list and free it. Decreases the open count and if it reaches
 * zero calls takedown().
 */
int DRM(release)( struct inode *inode, struct file *filp )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev;
	int retcode = 0;

	lock_kernel();
	dev = priv->dev;

	DRM_DEBUG( "open_count = %d\n", dev->open_count );

	DRIVER_PRERELEASE();

	/* ========================================================
	 * Begin inline drm_release
	 */

	DRM_DEBUG( "pid = %d, device = 0x%lx, open_count = %d\n",
		   current->pid, (long)dev->device, dev->open_count );

	if ( priv->lock_count && dev->lock.hw_lock &&
	     _DRM_LOCK_IS_HELD(dev->lock.hw_lock->lock) &&
	     dev->lock.filp == filp ) {
		DRM_DEBUG( "File %p released, freeing lock for context %d\n",
			filp,
			_DRM_LOCKING_CONTEXT(dev->lock.hw_lock->lock) );
#if __HAVE_RELEASE
		DRIVER_RELEASE();
#endif
		DRM(lock_free)( dev, &dev->lock.hw_lock->lock,
				_DRM_LOCKING_CONTEXT(dev->lock.hw_lock->lock) );

				/* FIXME: may require heavy-handed reset of
                                   hardware at this point, possibly
                                   processed via a callback to the X
                                   server. */
	}
#if __HAVE_RELEASE
	else if ( priv->lock_count && dev->lock.hw_lock ) {
		/* The lock is required to reclaim buffers */
		DECLARE_WAITQUEUE( entry, current );

		add_wait_queue( &dev->lock.lock_queue, &entry );
		for (;;) {
			current->state = TASK_INTERRUPTIBLE;
			if ( !dev->lock.hw_lock ) {
				/* Device has been unregistered */
				retcode = -EINTR;
				break;
			}
			if ( DRM(lock_take)( &dev->lock.hw_lock->lock,
					     DRM_KERNEL_CONTEXT ) ) {
				dev->lock.filp	    = filp;
				dev->lock.lock_time = jiffies;
                                atomic_inc( &dev->counts[_DRM_STAT_LOCKS] );
				break;	/* Got lock */
			}
				/* Contention */
			schedule();
			if ( signal_pending( current ) ) {
				retcode = -ERESTARTSYS;
				break;
			}
		}
		current->state = TASK_RUNNING;
		remove_wait_queue( &dev->lock.lock_queue, &entry );
		if( !retcode ) {
			DRIVER_RELEASE();
			DRM(lock_free)( dev, &dev->lock.hw_lock->lock,
					DRM_KERNEL_CONTEXT );
		}
	}
#elif __HAVE_DMA
	DRM(reclaim_buffers)( filp );
#endif

	DRM(fasync)( -1, filp, 0 );

	down( &dev->struct_sem );
	if ( priv->remove_auth_on_close == 1 ) {
		drm_file_t *temp = dev->file_first;
		while ( temp ) {
			temp->authenticated = 0;
			temp = temp->next;
		}
	}
	if ( priv->prev ) {
		priv->prev->next = priv->next;
	} else {
		dev->file_first	 = priv->next;
	}
	if ( priv->next ) {
		priv->next->prev = priv->prev;
	} else {
		dev->file_last	 = priv->prev;
	}
	up( &dev->struct_sem );
	
	DRM(free)( priv, sizeof(*priv), DRM_MEM_FILES );

	/* ========================================================
	 * End inline drm_release
	 */

	atomic_inc( &dev->counts[_DRM_STAT_CLOSES] );
	spin_lock( &dev->count_lock );
	if ( !--dev->open_count ) {
		if ( atomic_read( &dev->ioctl_count ) || dev->blocked ) {
			DRM_ERROR( "Device busy: %d %d\n",
				   atomic_read( &dev->ioctl_count ),
				   dev->blocked );
			spin_unlock( &dev->count_lock );
			unlock_kernel();
			return -EBUSY;
		}
		spin_unlock( &dev->count_lock );
		unlock_kernel();
		return DRM(takedown)( dev );
	}
	spin_unlock( &dev->count_lock );

	unlock_kernel();

	return retcode;
}

/** 
 * Called whenever a process performs an ioctl on /dev/drm.
 *
 * \param inode device inode.
 * \param filp file pointer.
 * \param cmd command.
 * \param arg user argument.
 * \return zero on success or negative number on failure.
 *
 * Looks up the ioctl function in the ::ioctls table, checking for root
 * previleges if so required, and dispatches to the respective function.
 */
int DRM(ioctl)( struct inode *inode, struct file *filp,
		unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_ioctl_desc_t *ioctl;
	drm_ioctl_t *func;
	int nr = DRM_IOCTL_NR(cmd);
	int retcode = 0;

	atomic_inc( &dev->ioctl_count );
	atomic_inc( &dev->counts[_DRM_STAT_IOCTLS] );
	++priv->ioctl_count;

	DRM_DEBUG( "pid=%d, cmd=0x%02x, nr=0x%02x, dev 0x%lx, auth=%d\n",
		   current->pid, cmd, nr, (long)dev->device, 
		   priv->authenticated );

	if ( nr >= DRIVER_IOCTL_COUNT ) {
		retcode = -EINVAL;
	} else {
		ioctl = &DRM(ioctls)[nr];
		func = ioctl->func;

		if ( !func ) {
			DRM_DEBUG( "no function\n" );
			retcode = -EINVAL;
		} else if ( ( ioctl->root_only && !capable( CAP_SYS_ADMIN ) )||
			    ( ioctl->auth_needed && !priv->authenticated ) ) {
			retcode = -EACCES;
		} else {
			retcode = func( inode, filp, cmd, arg );
		}
	}

	atomic_dec( &dev->ioctl_count );
	return retcode;
}

/** No-op. */
int DRM(fasync)(int fd, struct file *filp, int on)
{
	drm_file_t    *priv   = filp->private_data;
	drm_device_t  *dev    = priv->dev;
	int	      retcode;

	DRM_DEBUG("fd = %d, device = 0x%lx\n", fd, (long)dev->device);
	retcode = fasync_helper(fd, filp, on, &dev->buf_async);
	if (retcode < 0) return retcode;
	return 0;
}

#if !__HAVE_DRIVER_FOPS_POLL
/** No-op. */
unsigned int DRM(poll)(struct file *filp, struct poll_table_struct *wait)
{
	return 0;
}
#endif

#if !__HAVE_DRIVER_FOPS_READ
/** No-op. */
ssize_t DRM(read)(struct file *filp, char *buf, size_t count, loff_t *off)
{
		return 0;
}
#endif


