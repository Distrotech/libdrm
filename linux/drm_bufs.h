/**
 * \file drm_bufs.h 
 * Buffer management.
 * 
 * \author Rickard E. (Rik) Faith <faith@valinux.com>
 * \author Gareth Hughes <gareth@valinux.com>
 * \author José Fonseca <jrfonseca@tungstengraphics.com>
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


/** \name DMA buffer pool */
/*@{*/

typedef struct drm_pool_buffer drm_pool_buffer_t;
typedef struct drm_pool drm_pool_t;

/**
 * A buffer of the DMA buffer pool.
 *
 * \sa drm_pool.
 */
struct drm_pool_buffer {
	void *			cpuaddr;	/**< kernel virtual address */
	dma_addr_t		busaddr;	/**< associated bus address */
};

/**
 * DMA buffer pool.
 *
 * \sa drm_pool_buffer.
 */
struct drm_pool {
	size_t			count;		/**< number of buffers */
	size_t			size;		/**< size of a buffer */
	drm_pool_buffer_t *	buffers;	/**< buffers */

	/** Callback to free the memory associated with this pool */
	void (*free)(drm_device_t *dev, drm_pool_t *pool);
};

/*@}*/


/** \name Free-list management */
/*@{*/

typedef struct drm_freelist2_entrys drm_freelist2_entrys_t;
typedef struct drm_freelist2 drm_freelist2_t;

/**
 * An entrys in a freelist.
 *
 * This structure can be extended by passing to drm_freelist2_init a stride
 * value greater than the size of this structure.
 * 
 * \sa drm_freelist2.
 *
 * \author Based on Leif Delgass's original freelist code for the Mach64
 * driver.
 */
struct drm_freelist2_entrys {
	struct list_head	list;		/**< Linux list */
	drm_pool_buffer_t *	buffer;		/**< referred DMA buffer */
	
	/**
	 * Used bytes of the buffer.
	 *
	 * This is here for convenience to the drivers since this isn't
	 * used by the free-list management code.
	 */
	size_t			used;
	
	/**
	 * Stamp of this buffer.
	 *
	 * Whenever drm_freelist2::last_stamp is greater or equal to this value
	 * then the buffer is considered free.
	 *
	 * The actualy quantity used for the stamp and its granularity does not
	 * matter, but it must be a monotonicaly increasing one. Also
	 * differences greater than 0x7ffffff are considered the result of
	 * arithmetic wrap-around and the buffer is freed.
	 * 
	 * \sa drm_freelist2::last_stamp.
	 */
	unsigned long stamp;

	/**
	 * Will it be reused?
	 *
	 * If set this flag will override the stamp mechanism above preventing
	 * the buffer from being considered free, so that its contents can be
	 * later reused without copying.
	 */
	int reuse;
} ;

/**
 * A free-list management for DMA buffer pools.
 *
 * \sa drm_pool_buffer.
 */
struct drm_freelist2 {
	drm_pool_t *		pool;		/**< the pool managed by this free-list */
	
	size_t			stride;		/**< stride of the entries */
	void *			entries;	/**< array with the freelist entries */
	
	struct list_head	free;		/**< free buffers list */
	struct list_head	pending;	/**< Buffers pending completion */

	/**
	 * Stamp of the last processed buffer.
	 *
	 * \sa drm_free_list2_entrys::stamp.
	 */
	unsigned long		last_stamp;
	
	/**
	 * Callback to wait for a free buffer.
	 */
	int (*wait)(drm_device_t *dev, drm_freelist2_t *freelist);
};


/*@}*/


/** \name Deprecated structure */
/*@{*/

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

/*@}*/


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
