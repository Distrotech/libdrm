/**
 * \file drm_dma.h 
 * DMA ioctl and function support
 *
 * \author Rickard E. (Rik) Faith <faith@valinux.com>
 * \author Gareth Hughes <gareth@valinux.com>
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

#ifndef _DRM_DMA_H_
#define _DRM_DMA_H_

/** Wait queue */
typedef struct drm_queue {
	atomic_t	  use_count;	/**< Outstanding uses (+1) */
	atomic_t	  finalization;	/**< Finalization in progress */
	atomic_t	  block_count;	/**< Count of processes waiting */
	atomic_t	  block_read;	/**< Queue blocked for reads */
	wait_queue_head_t read_queue;	/**< Processes waiting on block_read */
	atomic_t	  block_write;	/**< Queue blocked for writes */
	wait_queue_head_t write_queue;	/**< Processes waiting on block_write */
#if 1
	atomic_t	  total_queued;	/**< Total queued statistic */
	atomic_t	  total_flushed;/**< Total flushes statistic */
	atomic_t	  total_locks;	/**< Total locks statistics */
#endif
	drm_ctx_flags_t	  flags;	/**< Context preserving and 2D-only */
	drm_waitlist_t	  waitlist;	/**< Pending buffers */
	wait_queue_head_t flush_queue;	/**< Processes waiting until flush */
} drm_queue_t;

/**
 * DMA data.
 */
typedef struct drm_device_dma {

	drm_buf_entry_t	  bufs[DRM_MAX_ORDER+1];	/**< buffers, grouped by their size order */
	int		  buf_count;	/**< total number of buffers */
	drm_buf_t	  **buflist;	/**< Vector of pointers into drm_device_dma::bufs */
	int		  seg_count;
	int		  page_count;	/**< number of pages */
	unsigned long	  *pagelist;	/**< page list */
	unsigned long	  byte_count;
	enum {
		_DRM_DMA_USE_AGP = 0x01,
		_DRM_DMA_USE_SG  = 0x02
	} flags;

	/** \name DMA support */
	/*@{*/
	drm_buf_t	  *this_buffer;	/**< Buffer being sent */
	drm_buf_t	  *next_buffer; /**< Selected buffer to send */
	drm_queue_t	  *next_queue;	/**< Queue from which buffer selected*/
	wait_queue_head_t waiting;	/**< Processes waiting on free bufs */
	/*@}*/
} drm_device_dma_t;


#if __HAVE_VBL_IRQ

typedef struct drm_vbl_sig {
	struct list_head	head;
	unsigned int		sequence;
	struct siginfo		info;
	struct task_struct	*task;
} drm_vbl_sig_t;

/**
 * VBLANK IRQ data 
 */
typedef struct drm_vbl_data {
	wait_queue_head_t queue;	/**< VBLANK wait queue */
	atomic_t          received;
	spinlock_t        lock;
	drm_vbl_sig_t     sigs;		/**< signal list to send on VBLANK */
	unsigned int      pending;
} drm_vbl_data_t;

#endif


/** \name Prototypes */
/*@{*/

#if __HAVE_DMA
extern int DRM(dma_setup)(drm_device_t *dev);
extern void DRM(dma_takedown)(drm_device_t *dev);
extern void DRM(free_buffer)(drm_device_t *dev, drm_buf_t *buf);
extern void DRM(reclaim_buffers)( struct file *filp );
#if __HAVE_DMA_IRQ
extern int DRM(control_ioctl)( struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg );
extern int DRM(irq_install)( drm_device_t *dev, int irq );
extern int DRM(irq_uninstall)( drm_device_t *dev );
extern void DRM(dma_service)( int irq, void *device, struct pt_regs *regs );
extern void DRM(driver_irq_preinstall)( drm_device_t *dev );
extern void DRM(driver_irq_postinstall)( drm_device_t *dev );
extern void DRM(driver_irq_uninstall)( drm_device_t *dev );
#if __HAVE_VBL_IRQ
extern int DRM(wait_vblank_ioctl)(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg);
extern int DRM(vblank_wait)(drm_device_t *dev, unsigned int *vbl_seq);
extern void DRM(vbl_send_signals)( drm_device_t *dev );
#endif
#if __HAVE_DMA_IRQ_BH
extern void DRM(dma_immediate_bh)( void *dev );
#endif
#endif
#endif

/*@}*/


#endif
