/**
 * \file drm_lock.h 
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

#ifndef _DRM_LOCK_H_
#define _DRM_LOCK_H_


/**
 * Hardware lock.
 *
 * The lock structure is a simple cache-line aligned integer.  To avoid
 * processor bus contention on a multiprocessor system, there should not be any
 * other data stored in the same cache line.
 */
typedef struct drm_hw_lock {
	__volatile__ unsigned int lock;		/**< lock variable */
	char			  padding[60];	/**< Pad to cache line */
} drm_hw_lock_t;


/**
 * Lock data.
 */
typedef struct drm_lock_data {
	drm_hw_lock_t	  *hw_lock;	/**< Hardware lock */
	struct file       *filp;	/**< File descriptor of lock holder (NULL means kernel) */
	wait_queue_head_t lock_queue;	/**< Queue of blocked processes */
	unsigned long	  lock_time;	/**< Time of last lock in jiffies */
} drm_lock_data_t;



/** \name Prototypes */
/*@{*/

extern int DRM(lock_take)(__volatile__ unsigned int *lock, unsigned int context);
extern int DRM(lock_transfer)(drm_device_t *dev, __volatile__ unsigned int *lock, unsigned int context);
extern int DRM(lock_free)(drm_device_t *dev, __volatile__ unsigned int *lock, unsigned int context);
extern int DRM(notifier)(void *priv);

extern int DRM(lock_ioctl)(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg);
extern int DRM(unlock_ioctl)(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg);

/*@}*/


#endif
