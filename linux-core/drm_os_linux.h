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
#if LINUX_VERSION_CODE >= 0x020100 /* KERNEL_VERSION(2,1,0) */
#include <linux/tqueue.h>
#include <linux/poll.h>
#endif
#if LINUX_VERSION_CODE < 0x020400
#include "compat-pre24.h"
#endif
#include <asm/pgalloc.h>

#define __REALLY_HAVE_AGP	(__HAVE_AGP && (defined(CONFIG_AGP) || \
						defined(CONFIG_AGP_MODULE)))
#define __REALLY_HAVE_MTRR	(__HAVE_MTRR && defined(CONFIG_MTRR))

#define DRM_OS_LOCK 	up(&dev->struct_sem)
#define DRM_OS_UNLOCK 	down(&dev->struct_sem)
#define DRM_OS_IOCTL	struct inode *inode, struct file *filp, unsigned int cmd, unsigned long data
#define DRM_OS_DEVICE	drm_file_t	*priv	= filp->private_data; \
			drm_device_t	*dev	= priv->dev
#define DRM_OS_RETURN(v)	return -v;
#define DRM_OS_COPYTO(arg1, arg2, arg3) \
	copy_to_user( arg1, arg2, arg3) 
#define DRM_OS_COPYFROM(arg1, arg2, arg3) \
	copy_from_user( arg1, arg2, arg3)
