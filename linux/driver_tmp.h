/* driver_tmp.h -- Generic driver template -*- linux-c -*-
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

/*
 * To use this template, you must at least define the following (samples
 * given for the MGA driver):
 *
 * #define DRIVER_AUTHOR	"VA Linux Systems, Inc."
 *
 * #define DRIVER_NAME		"mga"
 * #define DRIVER_DESC		"Matrox G200/G400"
 * #define DRIVER_DATE		"20001127"
 *
 * #define DRIVER_MAJOR		2
 * #define DRIVER_MINOR		0
 * #define DRIVER_PATCHLEVEL	2
 *
 * #define DRIVER_IOCTL_COUNT	DRM_ARRAY_SIZE( mga_ioctls )
 *
 * #define TAG(x)		mga_##x
 */

#ifndef HAVE_AGP
#define HAVE_AGP			0
#endif
#ifndef MUST_HAVE_AGP
#define MUST_HAVE_AGP			0
#endif
#ifndef HAVE_MTRR
#define HAVE_MTRR			0
#endif
#ifndef HAVE_CTX_BITMAP
#define HAVE_CTX_BITMAP			0
#endif
#ifndef HAVE_DMA
#define HAVE_DMA			0
#endif
#ifndef HAVE_DMA_IRQ
#define HAVE_DMA_IRQ			0
#endif
#ifndef HAVE_DMA_QUEUE
#define HAVE_DMA_QUEUE			0
#endif
#ifndef HAVE_MULTIPLE_DMA_QUEUES
#define HAVE_MULTIPLE_DMA_QUEUES	0
#endif
#ifndef HAVE_DMA_SCHEDULE
#define HAVE_DMA_SCHEDULE		0
#endif
#ifndef HAVE_DMA_FLUSH
#define HAVE_DMA_FLUSH			0
#endif
#ifndef HAVE_DMA_READY
#define HAVE_DMA_READY			0
#endif
#ifndef HAVE_DMA_QUIESCENT
#define HAVE_DMA_QUIESCENT		0
#endif
#ifndef HAVE_DRIVER_RELEASE
#define HAVE_DRIVER_RELEASE		0
#endif

#ifndef DRIVER_PREINIT
#define DRIVER_PREINIT()
#endif
#ifndef DRIVER_POSTINIT
#define DRIVER_POSTINIT()
#endif
#ifndef DRIVER_PRETAKEDOWN
#define DRIVER_PRETAKEDOWN()
#endif


static drm_device_t	TAG(device);

static struct file_operations	TAG(fops) = {
#if LINUX_VERSION_CODE >= 0x020400
				/* This started being used during 2.4.0-test */
	owner:   THIS_MODULE,
#endif
	open:	 TAG(open),
	flush:	 drm_flush,
	release: TAG(release),
	ioctl:	 TAG(ioctl),
	mmap:	 drm_mmap,
	read:	 drm_read,
	fasync:	 drm_fasync,
	poll:	 drm_poll,
};

static struct miscdevice	TAG(misc) = {
	minor: MISC_DYNAMIC_MINOR,
	name:  DRIVER_NAME,
	fops:  &TAG(fops),
};

#ifdef MODULE
static char *TAG(opts) = NULL;
#endif

MODULE_AUTHOR( DRIVER_AUTHOR );
MODULE_DESCRIPTION( DRIVER_DESC );
MODULE_PARM( TAG(opts), "s" );

#ifndef MODULE
/* r128_options is called by the kernel to parse command-line options
 * passed via the boot-loader (e.g., LILO).  It calls the insmod option
 * routine, drm_parse_drm.
 */

static int __init TAG(options)( char *str )
{
	drm_parse_options( str );
	return 1;
}

__setup( DRIVER_NAME "=", TAG(options) );
#endif

static int TAG(setup)( drm_device_t *dev )
{
	int i;

	atomic_set( &dev->ioctl_count, 0 );
	atomic_set( &dev->vma_count, 0 );
	dev->buf_use = 0;
	atomic_set( &dev->buf_alloc, 0 );

#if HAVE_DMA
	drm_dma_setup( dev );
#endif

	atomic_set( &dev->total_open, 0 );
	atomic_set( &dev->total_close, 0 );
	atomic_set( &dev->total_ioctl, 0 );
	atomic_set( &dev->total_irq, 0 );
	atomic_set( &dev->total_ctx, 0 );
	atomic_set( &dev->total_locks, 0 );
	atomic_set( &dev->total_unlocks, 0 );
	atomic_set( &dev->total_contends, 0 );
	atomic_set( &dev->total_sleeps, 0 );

	for ( i = 0 ; i < DRM_HASH_SIZE ; i++ ) {
		dev->magiclist[i].head = NULL;
		dev->magiclist[i].tail = NULL;
	}
	dev->maplist	    = NULL;
	dev->map_count	    = 0;
	dev->vmalist	    = NULL;
	dev->lock.hw_lock   = NULL;
	init_waitqueue_head( &dev->lock.lock_queue );
	dev->queue_count    = 0;
	dev->queue_reserved = 0;
	dev->queue_slots    = 0;
	dev->queuelist	    = NULL;
	dev->irq	    = 0;
	dev->context_flag   = 0;
	dev->interrupt_flag = 0;
	dev->dma_flag	    = 0;
	dev->last_context   = 0;
	dev->last_switch    = 0;
	dev->last_checked   = 0;
	init_timer( &dev->timer );
	init_waitqueue_head( &dev->context_wait );

	dev->ctx_start	    = 0;
	dev->lck_start	    = 0;

	dev->buf_rp	    = dev->buf;
	dev->buf_wp	    = dev->buf;
	dev->buf_end	    = dev->buf + DRM_BSZ;
	dev->buf_async	    = NULL;
	init_waitqueue_head( &dev->buf_readers );
	init_waitqueue_head( &dev->buf_writers );

	DRM_DEBUG( "\n" );

	/* The kernel's context could be created here, but is now created
	   in drm_dma_enqueue.	This is more resource-efficient for
	   hardware that does not do DMA, but may mean that
	   drm_select_queue fails between the time the interrupt is
	   initialized and the time the queues are initialized. */

	return 0;
}


static int TAG(takedown)( drm_device_t *dev )
{
	drm_magic_entry_t *pt, *next;
	drm_map_t *map;
	drm_vma_entry_t *vma, *vma_next;
	int i;

	DRM_DEBUG( "\n" );

	/* FIXME: mga_dma_cleanup() ???
	 */
	DRIVER_PRETAKEDOWN();

#if HAVE_DMA_IRQ
	if ( dev->irq ) TAG(irq_uninstall)( dev );
#endif

	down( &dev->struct_sem );
	del_timer( &dev->timer );

	if ( dev->devname ) {
		drm_free( dev->devname, strlen( dev->devname ) + 1,
			  DRM_MEM_DRIVER );
		dev->devname = NULL;
	}

	if ( dev->unique ) {
		drm_free( dev->unique, strlen( dev->unique ) + 1,
			  DRM_MEM_DRIVER );
		dev->unique = NULL;
		dev->unique_len = 0;
	}
				/* Clear pid list */
	for ( i = 0 ; i < DRM_HASH_SIZE ; i++ ) {
		for ( pt = dev->magiclist[i].head ; pt ; pt = next ) {
			next = pt->next;
			drm_free( pt, sizeof(*pt), DRM_MEM_MAGIC );
		}
		dev->magiclist[i].head = dev->magiclist[i].tail = NULL;
	}

#if HAVE_AGP && defined(CONFIG_AGP) || defined(CONFIG_AGP_MODULE)
				/* Clear AGP information */
	if ( dev->agp ) {
		drm_agp_mem_t *entry;
		drm_agp_mem_t *nexte;

				/* Remove AGP resources, but leave dev->agp
                                   intact until drv_cleanup is called. */
		for ( entry = dev->agp->memory ; entry ; entry = nexte ) {
			nexte = entry->next;
			if ( entry->bound ) drm_unbind_agp( entry->memory );
			drm_free_agp( entry->memory, entry->pages );
			drm_free( entry, sizeof(*entry), DRM_MEM_AGPLISTS );
		}
		dev->agp->memory = NULL;

		if ( dev->agp->acquired ) _drm_agp_release();

		dev->agp->acquired = 0;
		dev->agp->enabled  = 0;
	}
#endif

				/* Clear vma list (only built for debugging) */
	if ( dev->vmalist ) {
		for ( vma = dev->vmalist ; vma ; vma = vma_next ) {
			vma_next = vma->next;
			drm_free( vma, sizeof(*vma), DRM_MEM_VMAS );
		}
		dev->vmalist = NULL;
	}

				/* Clear map area and mtrr information */
	if ( dev->maplist ) {
		for ( i = 0 ; i < dev->map_count ; i++ ) {
			map = dev->maplist[i];
			switch ( map->type ) {
			case _DRM_REGISTERS:
			case _DRM_FRAME_BUFFER:
#if HAVE_MTRR && defined(CONFIG_MTRR)
				if ( map->mtrr >= 0 ) {
					int retcode;
					retcode = mtrr_del( map->mtrr,
							    map->offset,
							    map->size );
					DRM_DEBUG( "mtrr_del=%d\n", retcode );
				}
#endif
				drm_ioremapfree( map->handle, map->size );
				break;
			case _DRM_SHM:
				drm_free_pages( (unsigned long)map->handle,
						drm_order(map->size)
						- PAGE_SHIFT,
						DRM_MEM_SAREA );
				break;
			case _DRM_AGP:
				/* Do nothing here, because this is all
                                   handled in the AGP/GART driver. */
				break;
			}
			drm_free( map, sizeof(*map), DRM_MEM_MAPS );
		}
		drm_free( dev->maplist,
			  dev->map_count * sizeof(*dev->maplist),
			  DRM_MEM_MAPS );
		dev->maplist = NULL;
		dev->map_count = 0;
	}

#if HAVE_DMA_QUEUE || HAVE_MULTIPLE_DMA_QUEUES
	if ( dev->queuelist ) {
		for ( i = 0 ; i < dev->queue_count ; i++ ) {
			drm_waitlist_destroy( &dev->queuelist[i]->waitlist );
			if ( dev->queuelist[i] ) {
				drm_free( dev->queuelist[i],
					  sizeof(*dev->queuelist[0]),
					  DRM_MEM_QUEUES );
				dev->queuelist[i] = NULL;
			}
		}
		drm_free( dev->queuelist,
			  dev->queue_slots * sizeof(*dev->queuelist),
			  DRM_MEM_QUEUES );
		dev->queuelist = NULL;
	}
#endif

#if HAVE_DMA
	drm_dma_takedown( dev );
#endif

	dev->queue_count = 0;
	if ( dev->lock.hw_lock ) {
		dev->lock.hw_lock = NULL; /* SHM removed */
		dev->lock.pid = 0;
		wake_up_interruptible( &dev->lock.lock_queue );
	}
	up( &dev->struct_sem );

	return 0;
}

/* r128_init is called via init_module at module load time, or via
 * linux/init/main.c (this is not currently supported). */

static int __init TAG(init)( void )
{
	int retcode;
	drm_device_t *dev = &TAG(device);

	DRM_DEBUG( "\n" );

	memset( (void *)dev, 0, sizeof(*dev) );
	dev->count_lock = SPIN_LOCK_UNLOCKED;
	sema_init( &dev->struct_sem, 1 );

#ifdef MODULE
	drm_parse_options( TAG(opts) );
#endif
	DRIVER_PREINIT();

	retcode = misc_register( &TAG(misc) );
	if ( retcode ) {
		DRM_ERROR( "Cannot register \"%s\"\n", DRIVER_NAME );
		return retcode;
	}
	dev->device = MKDEV( MISC_MAJOR, TAG(misc).minor );
	dev->name = DRIVER_NAME;

	drm_mem_init();
	drm_proc_init( dev );

#if HAVE_AGP && defined(CONFIG_AGP) || defined(CONFIG_AGP_MODULE)
	dev->agp = drm_agp_init();
#if MUST_HAVE_AGP
	if ( dev->agp == NULL ) {
		DRM_INFO("The mga drm module requires the agpgart module"
		         " to function correctly\nPlease load the agpgart"
		         " module before you load the mga module\n");
		DRM_ERROR( "Cannot initialize agpgart module.\n" );
		drm_proc_cleanup();
		misc_deregister( &TAG(misc) );
		TAG(takedown)( dev );
		return -ENOMEM;
	}
#endif

#if HAVE_MTRR && defined(CONFIG_MTRR)
	dev->agp->agp_mtrr = mtrr_add( dev->agp->agp_info.aper_base,
				       dev->agp->agp_info.aper_size*1024*1024,
				       MTRR_TYPE_WRCOMB,
				       1 );
#endif
#endif

#if HAVE_CTX_BITMAP
	retcode = drm_ctxbitmap_init( dev );
	if( retcode ) {
		DRM_ERROR( "Cannot allocate memory for context bitmap.\n" );
		drm_proc_cleanup();
		misc_deregister( &TAG(misc) );
		TAG(takedown)( dev );
		return retcode;
	}
#endif

	DRIVER_POSTINIT();

	DRM_INFO( "Initialized %s %d.%d.%d %s on minor %d\n",
		  DRIVER_NAME,
		  DRIVER_MAJOR,
		  DRIVER_MINOR,
		  DRIVER_PATCHLEVEL,
		  DRIVER_DATE,
		  TAG(misc).minor );

	return 0;
}

/* r128_cleanup is called via cleanup_module at module unload time. */

static void __exit TAG(cleanup)( void )
{
	drm_device_t *dev = &TAG(device);

	DRM_DEBUG( "\n" );

	drm_proc_cleanup();
	if ( misc_deregister( &TAG(misc) ) ) {
		DRM_ERROR( "Cannot unload module\n" );
	} else {
		DRM_INFO( "Module unloaded\n" );
	}
#if HAVE_CTX_BITMAP
	drm_ctxbitmap_cleanup( dev );
#endif

#if HAVE_MTRR && defined(CONFIG_MTRR)
	if ( dev->agp && dev->agp->agp_mtrr ) {
		int retval;
		retval = mtrr_del( dev->agp->agp_mtrr,
				   dev->agp->agp_info.aper_base,
				   dev->agp->agp_info.aper_size*1024*1024 );
		DRM_DEBUG( "mtrr_del=%d\n", retval );
	}
#endif

	TAG(takedown)( dev );

#if HAVE_AGP && defined(CONFIG_AGP) || defined(CONFIG_AGP_MODULE)
	if ( dev->agp ) {
		drm_agp_uninit();
		drm_free( dev->agp, sizeof(*dev->agp), DRM_MEM_AGPLISTS );
		dev->agp = NULL;
	}
#endif
}

module_init( TAG(init) );
module_exit( TAG(cleanup) );


int TAG(version)( struct inode *inode, struct file *filp,
		  unsigned int cmd, unsigned long arg )
{
	drm_version_t version;
	int len;

	if ( copy_from_user( &version,
			     (drm_version_t *)arg,
			     sizeof(version) ) )
		return -EFAULT;

#define DRM_COPY( name, value )						\
	len = strlen( value );						\
	if ( len > name##_len ) len = name##_len;			\
	name##_len = strlen( value );					\
	if ( len && name ) {						\
		if ( copy_to_user( name, value, len ) )			\
			return -EFAULT;					\
	}

	version.version_major	   = DRIVER_MAJOR;
	version.version_minor	   = DRIVER_MINOR;
	version.version_patchlevel = DRIVER_PATCHLEVEL;

	DRM_COPY( version.name, DRIVER_NAME );
	DRM_COPY( version.date, DRIVER_DATE );
	DRM_COPY( version.desc, DRIVER_DESC );

	if ( copy_to_user( (drm_version_t *)arg,
			   &version,
			   sizeof(version) ) )
		return -EFAULT;
	return 0;
}

int TAG(open)( struct inode *inode, struct file *filp )
{
	drm_device_t *dev = &TAG(device);
	int retcode = 0;

	DRM_DEBUG( "open_count = %d\n", dev->open_count );

	retcode = drm_open_helper( inode, filp, dev );
	if ( !retcode ) {
#if LINUX_VERSION_CODE < 0x020333
		MOD_INC_USE_COUNT; /* Needed before Linux 2.3.51 */
#endif
		atomic_inc( &dev->total_open );
		spin_lock( &dev->count_lock );
		if ( !dev->open_count++ ) {
			spin_unlock( &dev->count_lock );
			return TAG(setup)( dev );
		}
		spin_unlock( &dev->count_lock );
	}

	return retcode;
}

int TAG(release)( struct inode *inode, struct file *filp )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev;
	int retcode = 0;

	lock_kernel();
	dev = priv->dev;

	DRM_DEBUG( "open_count = %d\n", dev->open_count );

	/* ========================================================
	 * Begin inline drm_release
	 */

	DRM_DEBUG( "pid = %d, device = 0x%x, open_count = %d\n",
		   current->pid, dev->device, dev->open_count );

	if ( dev->lock.hw_lock &&
	     _DRM_LOCK_IS_HELD(dev->lock.hw_lock->lock) &&
	     dev->lock.pid == current->pid ) {
		DRM_ERROR( "Process %d dead, freeing lock for context %d\n",
			   current->pid,
			   _DRM_LOCKING_CONTEXT(dev->lock.hw_lock->lock) );
#if HAVE_DRIVER_RELEASE
		DRIVER_RELEASE();
#endif
		drm_lock_free( dev, &dev->lock.hw_lock->lock,
			       _DRM_LOCKING_CONTEXT(dev->lock.hw_lock->lock) );

				/* FIXME: may require heavy-handed reset of
                                   hardware at this point, possibly
                                   processed via a callback to the X
                                   server. */
	}
#if HAVE_DRIVER_RELEASE
	else if ( dev->lock.hw_lock ) {
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
			if ( drm_lock_take( &dev->lock.hw_lock->lock,
					    DRM_KERNEL_CONTEXT ) ) {
				dev->lock.pid	    = priv->pid;
				dev->lock.lock_time = jiffies;
				atomic_inc( &dev->total_locks );
				break;	/* Got lock */
			}
				/* Contention */
			atomic_inc( &dev->total_sleeps );
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
			drm_lock_free( dev, &dev->lock.hw_lock->lock,
				       DRM_KERNEL_CONTEXT );
		}
	}
#else
	drm_reclaim_buffers( dev, priv->pid );
#endif

	drm_fasync( -1, filp, 0 );

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

	drm_free( priv, sizeof(*priv), DRM_MEM_FILES );

	/* ========================================================
	 * End inline drm_release
	 */

#if LINUX_VERSION_CODE < 0x020333
	MOD_DEC_USE_COUNT; /* Needed before Linux 2.3.51 */
#endif
	atomic_inc( &dev->total_close );
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
		return TAG(takedown)( dev );
	}
	spin_unlock( &dev->count_lock );

	unlock_kernel();
	return retcode;
}

/* r128_ioctl is called whenever a process performs an ioctl on /dev/drm. */

int TAG(ioctl)( struct inode *inode, struct file *filp,
		unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_ioctl_desc_t *ioctl;
	drm_ioctl_t *func;
	int nr = DRM_IOCTL_NR(cmd);
	int retcode = 0;

	atomic_inc( &dev->ioctl_count );
	atomic_inc( &dev->total_ioctl );
	++priv->ioctl_count;

	DRM_DEBUG( "pid=%d, cmd=0x%02x, nr=0x%02x, dev 0x%x, auth=%d\n",
		   current->pid, cmd, nr, dev->device, priv->authenticated );

	if ( nr >= DRIVER_IOCTL_COUNT ) {
		retcode = -EINVAL;
	} else {
		ioctl = &TAG(ioctls)[nr];
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

int TAG(lock)( struct inode *inode, struct file *filp,
	       unsigned int cmd, unsigned long arg )
{
        drm_file_t *priv = filp->private_data;
        drm_device_t *dev = priv->dev;
        DECLARE_WAITQUEUE( entry, current );
        drm_lock_t lock;
        int ret = 0;
#if HAVE_MULTIPLE_DMA_QUEUES
	drm_queue_t *q;
#endif
#if DRM_DMA_HISTOGRAM
        cycles_t start;

        dev->lck_start = start = get_cycles();
#endif

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

#if HAVE_DMA_QUEUE
        if ( lock.context < 0 )
                return -EINVAL;
#elsif HAVE_MULTIPLE_DMA_QUEUES
        if ( lock.context < 0 || lock.context >= dev->queue_count )
                return -EINVAL;
	q = dev->queuelist[lock.context];
#endif

#if HAVE_DMA_FLUSH
	ret = drm_flush_block_and_flush( dev, lock.context, lock.flags );
#endif
        if ( !ret ) {
		/* FIXME: do gamma stuff???
		 */

                add_wait_queue( &dev->lock.lock_queue, &entry );
                for (;;) {
                        current->state = TASK_INTERRUPTIBLE;
                        if ( !dev->lock.hw_lock ) {
                                /* Device has been unregistered */
                                ret = -EINTR;
                                break;
                        }
                        if ( drm_lock_take( &dev->lock.hw_lock->lock,
					    lock.context ) ) {
                                dev->lock.pid       = current->pid;
                                dev->lock.lock_time = jiffies;
                                atomic_inc( &dev->total_locks );
#if HAVE_MULTIPLE_DMA_QUEUES
				atomic_inc( &q->total_locks );
#endif
                                break;  /* Got lock */
                        }

                                /* Contention */
                        atomic_inc( &dev->total_sleeps );
                        schedule();
                        if ( signal_pending( current ) ) {
                                ret = -ERESTARTSYS;
                                break;
                        }
                }
                current->state = TASK_RUNNING;
                remove_wait_queue( &dev->lock.lock_queue, &entry );
        }

#if HAVE_DMA_FLUSH
	drm_flush_unblock( dev, lock.context, lock.flags ); /* cleanup phase */
#endif

        if ( !ret ) {
		sigemptyset( &dev->sigmask );
		sigaddset( &dev->sigmask, SIGSTOP );
		sigaddset( &dev->sigmask, SIGTSTP );
		sigaddset( &dev->sigmask, SIGTTIN );
		sigaddset( &dev->sigmask, SIGTTOU );
		dev->sigdata.context = lock.context;
		dev->sigdata.lock    = dev->lock.hw_lock;
		block_all_signals( drm_notifier,
				   &dev->sigdata, &dev->sigmask );

#if HAVE_DMA_READY
                if ( lock.flags & _DRM_LOCK_READY ) {
			DRIVER_DMA_READY();
		}
#endif
#if HAVE_DMA_QUIESCENT
                if ( lock.flags & _DRM_LOCK_QUIESCENT ) {
			DRIVER_DMA_QUIESCENT();
		}
#endif
        }

        DRM_DEBUG( "%d %s\n", lock.context, ret ? "interrupted" : "has lock" );

#if DRM_DMA_HISTOGRAM
        atomic_inc( &dev->histo.lacq[drm_histogram_slot(get_cycles()-start)] );
#endif
        return ret;
}


int TAG(unlock)( struct inode *inode, struct file *filp,
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

	atomic_inc( &dev->total_unlocks );
	if ( _DRM_LOCK_IS_CONT( dev->lock.hw_lock->lock ) )
		atomic_inc( &dev->total_contends );

	drm_lock_transfer( dev, &dev->lock.hw_lock->lock, DRM_KERNEL_CONTEXT );
#if HAVE_DMA_SCHEDULE
	TAG(dma_schedule)( dev, 1 );
#endif

	/* FIXME: mga has no context_flag check...
	 */
	if ( !dev->context_flag ) {
		if ( drm_lock_free( dev, &dev->lock.hw_lock->lock,
				    DRM_KERNEL_CONTEXT ) ) {
			DRM_ERROR( "\n" );
		}
	}

	unblock_all_signals();
	return 0;
}
