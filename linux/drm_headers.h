
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
#include <linux/version.h>
#include <linux/sched.h>
#include <linux/smp_lock.h>	/* For (un)lock_kernel */
#include <linux/mm.h>
#include <linux/pagemap.h>
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
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,41)
#define HAS_WORKQUEUE 0
#else
#define HAS_WORKQUEUE 1
#endif
#if !HAS_WORKQUEUE
#include <linux/tqueue.h>
#else
#include <linux/workqueue.h>
#endif
#include <linux/poll.h>
#include <asm/pgalloc.h>
#include "drm.h"

#include "drm_os_linux.h"

#include "drm_core.h"
#include "drm_core_memory.h"
