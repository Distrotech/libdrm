/**
 * \file drm_bufs.h 
 * Buffer management.
 * 
 * \author Rickard E. (Rik) Faith <faith@valinux.com>
 * \author Gareth Hughes <gareth@valinux.com>
 *
 * \todo The current buffer management system assumes too much and is severily
 * limited:
 * - It expects to all buffers to be used by clients, and has little provisions
 *   for private buffers (such as ring buffers, primary DMA buffers, etc.) This
 *   is currently overcomed by allocating these buffers directly in AGP memory
 *   when available, or by allocating a pool with a single buffer and with a
 *   size different of the regular client buffers.
 * - It doesn't allow to be two different pools with the same log2 of the
 *   buffers size making impossible, e.g., to have to pools of buffers: one
 *   private and one public.
 * - The high/low water mark is hardly used and the freelist should be
 *   redesigned to be more useful for all drivers.
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


#ifndef _DRM_BUFS_H
#define _DRM_BUFS_H


/**
 * DMA buffer.
 */
typedef struct drm_buf {
	int		  idx;	         /**< Index into master buflist */
	int		  total;         /**< Buffer size */
	int		  order;         /**< log-base-2(total) */
	int		  used;	         /**< Amount of buffer in use (for DMA) */
	unsigned long	  offset;        /**< Byte offset (used internally) */
	void		  *address;      /**< Address of buffer */
	unsigned long	  bus_address;   /**< Bus address of buffer */
	struct drm_buf	  *next;         /**< Kernel-only: used for free list */
	__volatile__ int  waiting;       /**< On kernel DMA queue */
	__volatile__ int  pending;       /**< On hardware DMA queue */
	wait_queue_head_t dma_wait;      /**< Processes waiting */
	struct file       *filp;         /**< Pointer to holding file descr */
	int		  context;       /**< Kernel queue for this buffer */
	int		  while_locked;  /**< Dispatch this buffer while locked */
	enum {
		DRM_LIST_NONE	 = 0,
		DRM_LIST_FREE	 = 1,
		DRM_LIST_WAIT	 = 2,
		DRM_LIST_PEND	 = 3,
		DRM_LIST_PRIO	 = 4,
		DRM_LIST_RECLAIM = 5
	}		  list;	         /**< Which list we're on */

	int		  dev_priv_size; /**< Size of buffer private storage */
	void		  *dev_private;  /**< Per-buffer private storage */
} drm_buf_t;

/**
 * Wait buffer list.
 *
 * \note Not used except for the Gamma driver.
 */
typedef struct drm_waitlist {
	int		  count;	/**< Number of possible buffers */
	drm_buf_t	  **bufs;	/**< List of pointers to buffers.
	                                 * Is one longer than it has to be */
	drm_buf_t	  **rp;		/**< Read pointer */
	drm_buf_t	  **wp;		/**< Write pointer */
	drm_buf_t	  **end;	/**< End pointer */
	spinlock_t	  read_lock;
	spinlock_t	  write_lock;
} drm_waitlist_t;

/**
 * Free buffer list.
 *
 * \note Not used except for the Gamma driver.
 */
typedef struct drm_freelist {
	int		  initialized; /**< Freelist in use */
	atomic_t	  count;       /**< Number of free buffers */
	drm_buf_t	  *next;       /**< End pointer */

	wait_queue_head_t waiting;     /**< Processes waiting on free bufs */
	int		  low_mark;    /**< Low water mark */
	int		  high_mark;   /**< High water mark */
	atomic_t	  wfh;	       /**< If waiting for high mark */
	spinlock_t        lock;
} drm_freelist_t;


/**
 * DMA buffer pool.  There is one of these for each buffer size in log2.
 *
 * \todo 
 * - There can't be two different pools with the same log2 of the buffers size.
 */
typedef struct drm_buf_entry {
	int		  buf_size;	/**< size */
	int		  buf_count;	/**< number of buffers */
	drm_buf_t	  *buflist;	/**< buffer list */
	int		  seg_count;
	int		  page_order;
	unsigned long	  *seglist;
	drm_freelist_t	  freelist;
} drm_buf_entry_t;


/** \name Prototypes */
/*@{*/

extern int DRM(order)( unsigned long size );
extern int DRM(addmap_ioctl)( struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg );
extern int DRM(rmmap_ioctl)( struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg );
extern int DRM(addbufs_ioctl)( struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg );
extern int DRM(infobufs_ioctl)( struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg );
extern int DRM(markbufs_ioctl)( struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg );
extern int DRM(freebufs_ioctl)( struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg );
extern int DRM(mapbufs_ioctl)( struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg );

/*@}*/


#endif
