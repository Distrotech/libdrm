/**
 * \file drmP.h 
 * Private header for Direct Rendering Manager
 * 
 * \author Rickard E. (Rik) Faith <faith@valinux.com>
 * \author Gareth Hughes <gareth@valinux.com>
 */

/*
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * All rights reserved.
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

#ifndef _DRM_P_H_
#define _DRM_P_H_


#ifdef __KERNEL__

/* If you want the memory alloc debug functionality, change define below */
/* #define DEBUG_MEMORY */

/***********************************************************************/
/** \name Begin the DRM... */
/*@{*/


#define DRM_MAX_CTXBITMAP (PAGE_SIZE * 8)
	
/*@}*/



#include "drm_core.h"

/***********************************************************************/
/** \name Macros to make printk easier */
/*@{*/


/*@}*/


/***********************************************************************/
/** \name Internal types and structures */
/*@{*/

#define DRM_ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))
#define DRM_MIN(a,b) ((a)<(b)?(a):(b))
#define DRM_MAX(a,b) ((a)>(b)?(a):(b))


#define DRM_IF_VERSION(maj, min) (maj << 16 | min)
/**
 * Get the private SAREA mapping.
 *
 * \param _dev DRM device.
 * \param _ctx context number.
 * \param _map output mapping.
 */
#define DRM_GET_PRIV_SAREA(_dev, _ctx, _map) do {	\
	(_map) = (_dev)->context_sareas[_ctx];		\
} while(0)

/**
 * Test that the hardware lock is held by the caller, returning otherwise.
 *
 * \param dev DRM device.
 * \param filp file pointer of the caller.
 */
#define LOCK_TEST_WITH_RETURN( dev, filp )				\
do {									\
	if ( !_DRM_LOCK_IS_HELD( dev->lock.hw_lock->lock ) ||		\
	     dev->lock.filp != filp ) {				\
		DRM_ERROR( "%s called without lock held\n",		\
			   __FUNCTION__ );				\
		return -EINVAL;						\
	}								\
} while (0)


extern void DRM(driver_register_fns)(struct drm_device *dev);

/******************************************************************/
/** \name Internal function definitions */
/*@{*/

				/* Misc. support (drm_init.h) */
extern int	     DRM(flags);
extern void	     DRM(parse_options)( char *s );
extern int           DRM(cpu_valid)( void );

				/* Driver support (drm_drv.h) */
extern int           DRM(version)(struct inode *inode, struct file *filp,
				  unsigned int cmd, unsigned long arg);
extern int           DRM(open)(struct inode *inode, struct file *filp);
extern int           DRM(release)(struct inode *inode, struct file *filp);
extern int           DRM(ioctl)(struct inode *inode, struct file *filp,
				unsigned int cmd, unsigned long arg);
extern int           DRM(lock)(struct inode *inode, struct file *filp,
			       unsigned int cmd, unsigned long arg);
extern int           DRM(unlock)(struct inode *inode, struct file *filp,
				 unsigned int cmd, unsigned long arg);
extern int           DRM(fb_loaded);
extern struct file_operations DRM(fops);

				/* Device support (drm_fops.h) */
extern int	     DRM(open_helper)(struct inode *inode, struct file *filp,
				      drm_device_t *dev);
extern int	     DRM(flush)(struct file *filp);
extern int	     DRM(fasync)(int fd, struct file *filp, int on);

				/* Mapping support (drm_vm.h) */
extern void	     DRM(vm_open)(struct vm_area_struct *vma);
extern void	     DRM(vm_close)(struct vm_area_struct *vma);
extern void	     DRM(vm_shm_close)(struct vm_area_struct *vma);
extern int	     DRM(mmap_dma)(struct file *filp,
				   struct vm_area_struct *vma);
extern int	     DRM(mmap)(struct file *filp, struct vm_area_struct *vma);
extern unsigned int  DRM(poll)(struct file *filp, struct poll_table_struct *wait);
extern ssize_t       DRM(read)(struct file *filp, char __user *buf, size_t count, loff_t *off);


				/* Misc. IOCTL support (drm_ioctl.h) */
extern int	     DRM(irq_by_busid)(struct inode *inode, struct file *filp,
				       unsigned int cmd, unsigned long arg);
extern int	     DRM(getunique)(struct inode *inode, struct file *filp,
				    unsigned int cmd, unsigned long arg);
extern int	     DRM(setunique)(struct inode *inode, struct file *filp,
				    unsigned int cmd, unsigned long arg);
extern int	     DRM(getmap)(struct inode *inode, struct file *filp,
				 unsigned int cmd, unsigned long arg);
extern int	     DRM(getclient)(struct inode *inode, struct file *filp,
				    unsigned int cmd, unsigned long arg);
extern int	     DRM(getstats)(struct inode *inode, struct file *filp,
				   unsigned int cmd, unsigned long arg);
extern int	     DRM(setversion)(struct inode *inode, struct file *filp,
				     unsigned int cmd, unsigned long arg);

				/* Context IOCTL support (drm_context.h) */
extern int	     DRM(resctx)( struct inode *inode, struct file *filp,
				  unsigned int cmd, unsigned long arg );
extern int	     DRM(addctx)( struct inode *inode, struct file *filp,
				  unsigned int cmd, unsigned long arg );
extern int	     DRM(modctx)( struct inode *inode, struct file *filp,
				  unsigned int cmd, unsigned long arg );
extern int	     DRM(getctx)( struct inode *inode, struct file *filp,
				  unsigned int cmd, unsigned long arg );
extern int	     DRM(switchctx)( struct inode *inode, struct file *filp,
				     unsigned int cmd, unsigned long arg );
extern int	     DRM(newctx)( struct inode *inode, struct file *filp,
				  unsigned int cmd, unsigned long arg );
extern int	     DRM(rmctx)( struct inode *inode, struct file *filp,
				 unsigned int cmd, unsigned long arg );

extern int	     DRM(context_switch)(drm_device_t *dev, int old, int new);
extern int	     DRM(context_switch_complete)(drm_device_t *dev, int new);

extern int	     DRM(ctxbitmap_init)( drm_device_t *dev );
extern void	     DRM(ctxbitmap_cleanup)( drm_device_t *dev );

extern int	     DRM(setsareactx)( struct inode *inode, struct file *filp,
				       unsigned int cmd, unsigned long arg );
extern int	     DRM(getsareactx)( struct inode *inode, struct file *filp,
				       unsigned int cmd, unsigned long arg );

				/* Drawable IOCTL support (drm_drawable.h) */
extern int	     DRM(adddraw)(struct inode *inode, struct file *filp,
				  unsigned int cmd, unsigned long arg);
extern int	     DRM(rmdraw)(struct inode *inode, struct file *filp,
				 unsigned int cmd, unsigned long arg);


				/* Authentication IOCTL support (drm_auth.h) */
extern int	     DRM(add_magic)(drm_device_t *dev, drm_file_t *priv,
				    drm_magic_t magic);
extern int	     DRM(remove_magic)(drm_device_t *dev, drm_magic_t magic);
extern int	     DRM(getmagic)(struct inode *inode, struct file *filp,
				   unsigned int cmd, unsigned long arg);
extern int	     DRM(authmagic)(struct inode *inode, struct file *filp,
				    unsigned int cmd, unsigned long arg);

                                /* Placeholder for ioctls past */
extern int	     DRM(noop)(struct inode *inode, struct file *filp,
				  unsigned int cmd, unsigned long arg);

				/* Locking IOCTL support (drm_lock.h) */
extern int	     DRM(lock_take)(__volatile__ unsigned int *lock,
				    unsigned int context);
extern int	     DRM(lock_transfer)(drm_device_t *dev,
					__volatile__ unsigned int *lock,
					unsigned int context);
extern int	     DRM(lock_free)(drm_device_t *dev,
				    __volatile__ unsigned int *lock,
				    unsigned int context);
extern int           DRM(notifier)(void *priv);

				/* IRQ support (drm_irq.h) */
extern int           DRM(control)( struct inode *inode, struct file *filp,
				   unsigned int cmd, unsigned long arg );
extern int           DRM(irq_install)( drm_device_t *dev );
extern int           DRM(irq_uninstall)( drm_device_t *dev );
extern irqreturn_t   DRM(irq_handler)( DRM_IRQ_ARGS );
extern void          DRM(driver_irq_preinstall)( drm_device_t *dev );
extern void          DRM(driver_irq_postinstall)( drm_device_t *dev );
extern void          DRM(driver_irq_uninstall)( drm_device_t *dev );

extern int           DRM(wait_vblank)(struct inode *inode, struct file *filp,
				      unsigned int cmd, unsigned long arg);
extern int           DRM(vblank_wait)(drm_device_t *dev, unsigned int *vbl_seq);
extern void          DRM(vbl_send_signals)( drm_device_t *dev );




				/* Stub support (drm_stub.h) */
int                   DRM(stub_register)(const char *name,
					 struct file_operations *fops,
					 drm_device_t *dev);
int                   DRM(stub_unregister)(int minor);

				/* Proc support (drm_proc.h) */
extern int 	      drm_proc_init(drm_device_t *dev,
					int minor,
					struct proc_dir_entry *root,
					struct proc_dir_entry **dev_root);
extern int            drm_proc_cleanup(int minor,
					struct proc_dir_entry *root,
					struct proc_dir_entry *dev_root);

				/* Scatter Gather Support (drm_scatter.h) */
extern void           DRM(sg_cleanup)(drm_sg_mem_t *entry);
extern int            DRM(sg_alloc)(struct inode *inode, struct file *filp,
				    unsigned int cmd, unsigned long arg);
extern int            DRM(sg_free)(struct inode *inode, struct file *filp,
				   unsigned int cmd, unsigned long arg);

                             

extern void	      *DRM(pci_alloc)(drm_device_t *dev, size_t size, 
					size_t align, dma_addr_t maxaddr,
					dma_addr_t *busaddr);
extern void	      DRM(pci_free)(drm_device_t *dev, size_t size, 
					void *vaddr, dma_addr_t busaddr);






/*@}*/

extern unsigned long DRM(core_get_map_ofs)(drm_map_t *map);
extern unsigned long DRM(core_get_reg_ofs)(struct drm_device *dev);
#endif /* __KERNEL__ */
#endif
