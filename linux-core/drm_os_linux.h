#ifdef __alpha__
/* add include of current.h so that "current" is defined
 * before static inline funcs in wait.h. Doing this so we
 * can build the DRM (part of PI DRI). 4/21/2000 S + B */
#include <asm/current.h>
#endif /* __alpha__ */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <linux/file.h>
#include <linux/pci.h>
#include <linux/wrapper.h>
#include <linux/version.h>
#include <linux/sched.h>
#include <linux/smp_lock.h>	/* For (un)lock_kernel */
#include <linux/mm.h>
#if defined(__alpha__) || defined(__powerpc__)
#include <asm/pgtable.h> /* For pte_wrprotect */
#endif
#include <asm/io.h>
#include <asm/mman.h>
#include <asm/uaccess.h>
#ifdef CONFIG_MTRR
#include <asm/mtrr.h>
#endif
#if defined(CONFIG_AGP) || defined(CONFIG_AGP_MODULE)
#include <linux/types.h>
#include <linux/agp_backend.h>
#endif
#include <linux/tqueue.h>
#include <linux/poll.h>
#include <asm/pgalloc.h>

#define DRM_TIME_SLICE	      (HZ/20)  /* Time slice for GLXContexts	  */

#define VM_OFFSET(vma) ((vma)->vm_pgoff << PAGE_SHIFT)

				/* Macros to make printk easier */
#define DRM_ERROR(fmt, arg...) \
	printk(KERN_ERR "[" DRM_NAME ":" __FUNCTION__ "] *ERROR* " fmt , ##arg)
#define DRM_MEM_ERROR(area, fmt, arg...) \
	printk(KERN_ERR "[" DRM_NAME ":" __FUNCTION__ ":%s] *ERROR* " fmt , \
	       DRM(mem_stats)[area].name , ##arg)
#define DRM_INFO(fmt, arg...)  printk(KERN_INFO "[" DRM_NAME "] " fmt , ##arg)

#if DRM_DEBUG_CODE
#define DRM_DEBUG(fmt, arg...)						\
	do {								\
		if ( DRM(flags) & DRM_FLAG_DEBUG )			\
			printk(KERN_DEBUG				\
			       "[" DRM_NAME ":" __FUNCTION__ "] " fmt ,	\
			       ##arg);					\
	} while (0)
#else
#define DRM_DEBUG(fmt, arg...)		 do { } while (0)
#endif

#define DRM_PROC_LIMIT (PAGE_SIZE-80)

#define DRM_PROC_PRINT(fmt, arg...)					\
   len += sprintf(&buf[len], fmt , ##arg);				\
   if (len > DRM_PROC_LIMIT) { *eof = 1; return len - offset; }

#define DRM_PROC_PRINT_RET(ret, fmt, arg...)				\
   len += sprintf(&buf[len], fmt , ##arg);				\
   if (len > DRM_PROC_LIMIT) { ret; *eof = 1; return len - offset; }

#define DRM_FIND_MAP(_map, _o)						\
do {									\
	struct list_head *_list;					\
	list_for_each( _list, &dev->maplist->head ) {			\
		drm_map_list_t *_entry = (drm_map_list_t *)_list;	\
		if ( _entry->map &&					\
		     _entry->map->offset == (_o) ) {			\
			(_map) = _entry->map;				\
			break;						\
 		}							\
	}								\
} while(0)

#define __REALLY_HAVE_AGP	(__HAVE_AGP && (defined(CONFIG_AGP) || \
						defined(CONFIG_AGP_MODULE)))
#define __REALLY_HAVE_SG	(__HAVE_SG)
#define __REALLY_HAVE_MTRR	(__HAVE_MTRR && defined(CONFIG_MTRR))

#define DRM_OS_LOCK 	up(&dev->struct_sem)
#define DRM_OS_UNLOCK 	down(&dev->struct_sem)
#define DRM_OS_SPINTYPE spinlock_t
#define DRM_OS_SPININIT(l,name)	
#define DRM_OS_SPINLOCK(l)	spin_lock(l)
#define DRM_OS_SPINUNLOCK(u)	spin_unlock(u)
#define DRM_OS_IOCTL	struct inode *inode, struct file *filp, unsigned int cmd, unsigned long data
#define DRM_OS_TASKQUEUE_ARGS	void *dev
#define DRM_OS_IRQ_ARGS	int irq, void *device, struct pt_regs *regs
#define DRM_OS_DEVICE	drm_file_t	*priv	= filp->private_data; \
			drm_device_t	*dev	= priv->dev
#define DRM_OS_PRIV
#define DRM_OS_DELAY(d)		udelay(d)
#define DRM_OS_RETURN(v)	return -v;
#define DRM_OS_CURRENTPID	current->pid
#define DRM_OS_CHECKSUSER	!capable( CAP_SYS_ADMIN )
#define DRM_OS_KRNTOUSR(arg1, arg2, arg3) \
	if ( copy_to_user(arg1, &arg2, arg3) ) \
		return -EFAULT
#define DRM_OS_KRNFROMUSR(arg1, arg2, arg3) \
	if ( copy_from_user(&arg1, arg2, arg3) ) \
		return -EFAULT
#define DRM_OS_COPYTOUSR(arg1, arg2, arg3) \
	copy_to_user(arg1, arg2, arg3)
#define DRM_OS_COPYFROMUSR(arg1, arg2, arg3) \
	copy_from_user(arg1, arg2, arg3)
#define DRM_OS_MALLOC(x) kmalloc(x, 0)
#define DRM_OS_FREE(x) kfree(x)
#define DRM_OS_VTOPHYS(addr) virt_to_phys(addr)

#define DRM_OS_READMEMORYBARRIER mb()
#define DRM_OS_WRITEMEMORYBARRIER wmb()

#define DRM_OS_WAKEUP(w) wake_up(w)
#define DRM_OS_WAKEUP_INT(w) wake_up_interruptible(w)

/* Internal functions */

/* drm_drv.h */
extern int	DRM(ioctl)( DRM_OS_IOCTL );
extern int	DRM(lock)( DRM_OS_IOCTL );
extern int	DRM(unlock)( DRM_OS_IOCTL );
extern int	DRM(open)(struct inode *inode, struct file *filp);
extern int	DRM(release)(struct inode *inode, struct file *filp);
extern int	DRM(open_helper)(struct inode *inode, struct file *filp,
							drm_device_t *dev);

/* Misc. IOCTL support (drm_ioctl.h) */
extern int	DRM(irq_busid)( DRM_OS_IOCTL );
extern int	DRM(getunique)( DRM_OS_IOCTL );
extern int	DRM(setunique)( DRM_OS_IOCTL );
extern int	DRM(getmap)( DRM_OS_IOCTL );
extern int	DRM(getclient)( DRM_OS_IOCTL );
extern int	DRM(getstats)( DRM_OS_IOCTL );

/* Context IOCTL support (drm_context.h) */
extern int	DRM(resctx)( DRM_OS_IOCTL );
extern int	DRM(addctx)( DRM_OS_IOCTL );
extern int	DRM(modctx)( DRM_OS_IOCTL );
extern int	DRM(getctx)( DRM_OS_IOCTL );
extern int	DRM(switchctx)( DRM_OS_IOCTL );
extern int	DRM(newctx)( DRM_OS_IOCTL );
extern int	DRM(rmctx)( DRM_OS_IOCTL );
extern int	DRM(setsareactx)( DRM_OS_IOCTL );
extern int	DRM(getsareactx)( DRM_OS_IOCTL );

/* Drawable IOCTL support (drm_drawable.h) */
extern int	DRM(adddraw)( DRM_OS_IOCTL );
extern int	DRM(rmdraw)( DRM_OS_IOCTL );

/* Authentication IOCTL support (drm_auth.h) */
extern int	DRM(getmagic)( DRM_OS_IOCTL );
extern int	DRM(authmagic)( DRM_OS_IOCTL );

/* Device support (drm_fops.h) */
extern int	DRM(flush)(struct file *filp);
extern int	DRM(release_fuck)(struct inode *inode, struct file *filp);
extern int	DRM(fasync)(int fd, struct file *filp, int on);
extern ssize_t	DRM(read)(struct file *filp, char *buf, size_t count,
			       loff_t *off);
extern unsigned int  DRM(poll)(struct file *filp,
			       struct poll_table_struct *wait);

/* Memory management support (drm_memory.h) */
extern int	DRM(mem_info)(char *buf, char **start, off_t offset,
				   int request, int *eof, void *data);

/* Locking IOCTL support (drm_lock.h) */
extern int	DRM(block)( DRM_OS_IOCTL );
extern int	DRM(unblock)( DRM_OS_IOCTL );
extern int	DRM(finish)( DRM_OS_IOCTL );

/* Buffer management support (drm_bufs.h) */
extern int	DRM(addmap)( DRM_OS_IOCTL );
extern int	DRM(rmmap)( DRM_OS_IOCTL );
#if __HAVE_DMA
extern int	DRM(addbufs)( DRM_OS_IOCTL );
extern int	DRM(infobufs)( DRM_OS_IOCTL );
extern int	DRM(markbufs)( DRM_OS_IOCTL );
extern int	DRM(freebufs)( DRM_OS_IOCTL );
extern int	DRM(mapbufs)( DRM_OS_IOCTL );
#endif

/* Mapping support (drm_vm.h) */
extern struct page *DRM(vm_nopage)(struct vm_area_struct *vma,
				   unsigned long address,
				   int write_access);
extern struct page *DRM(vm_shm_nopage)(struct vm_area_struct *vma,
				       unsigned long address,
				       int write_access);
extern struct page *DRM(vm_dma_nopage)(struct vm_area_struct *vma,
				       unsigned long address,
				       int write_access);
extern struct page *DRM(vm_sg_nopage)(struct vm_area_struct *vma,
				      unsigned long address,
				      int write_access);
extern void	     DRM(vm_open)(struct vm_area_struct *vma);
extern void	     DRM(vm_close)(struct vm_area_struct *vma);
extern void	     DRM(vm_shm_close)(struct vm_area_struct *vma);
extern int	     DRM(mmap_dma)(struct file *filp,
				   struct vm_area_struct *vma);

/* DMA support (drm_dma.h) */
#if __HAVE_DMA_IRQ
extern int	DRM(control)( DRM_OS_IOCTL );
#endif

/* AGP/GART support (drm_agpsupport.h) */
#if __REALLY_HAVE_AGP
extern int	DRM(agp_acquire)( DRM_OS_IOCTL );
extern int	DRM(agp_release)( DRM_OS_IOCTL );
extern int	DRM(agp_enable)( DRM_OS_IOCTL );
extern int	DRM(agp_info)( DRM_OS_IOCTL );
extern int	DRM(agp_alloc)( DRM_OS_IOCTL );
extern int	DRM(agp_free)( DRM_OS_IOCTL );
extern int	DRM(agp_unbind)( DRM_OS_IOCTL );
extern int	DRM(agp_bind)( DRM_OS_IOCTL );
#endif

/* Scatter Gather Support (drm_scatter.h) */
#if __HAVE_SG
extern int	DRM(sg_alloc)( DRM_OS_IOCTL );
extern int	DRM(sg_free)( DRM_OS_IOCTL );
#endif

/* Stub support (drm_stub.h) */
extern int	DRM(stub_register)(const char *name,
				 struct file_operations *fops,
				 drm_device_t *dev);
extern int	DRM(stub_unregister)(int minor);

/* Mapping support (drm_vm.h) */
extern int	DRM(mmap)(struct file *filp, struct vm_area_struct *vma);
