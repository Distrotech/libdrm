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

				/* Wait queue declarations changed in 2.3.1 */
#ifndef DECLARE_WAITQUEUE
#define DECLARE_WAITQUEUE(w,c) struct wait_queue w = { c, NULL }
typedef struct wait_queue *wait_queue_head_t;
#define init_waitqueue_head(q) *q = NULL;
#endif

				/* _PAGE_4M changed to _PAGE_PSE in 2.3.23 */
#ifndef _PAGE_PSE
#define _PAGE_PSE _PAGE_4M
#endif

				/* vm_offset changed to vm_pgoff in 2.3.25 */
#if LINUX_VERSION_CODE < 0x020319
#define VM_OFFSET(vma) ((vma)->vm_offset)
#else
#define VM_OFFSET(vma) ((vma)->vm_pgoff << PAGE_SHIFT)
#endif

				/* *_nopage return values defined in 2.3.26 */
#ifndef NOPAGE_SIGBUS
#define NOPAGE_SIGBUS 0
#endif
#ifndef NOPAGE_OOM
#define NOPAGE_OOM 0
#endif

				/* module_init/module_exit added in 2.3.13 */
#ifndef module_init
#define module_init(x)  int init_module(void) { return x(); }
#endif
#ifndef module_exit
#define module_exit(x)  void cleanup_module(void) { x(); }
#endif

				/* Generic cmpxchg added in 2.3.x */
#ifndef __HAVE_ARCH_CMPXCHG
				/* Include this here so that driver can be
                                   used with older kernels. */
#if defined(__alpha__)
static __inline__ unsigned long
__cmpxchg_u32(volatile int *m, int old, int new)
{
	unsigned long prev, cmp;

	__asm__ __volatile__(
	"1:	ldl_l %0,%5\n"
	"	cmpeq %0,%3,%1\n"
	"	beq %1,2f\n"
	"	mov %4,%1\n"
	"	stl_c %1,%2\n"
	"	beq %1,3f\n"
	"2:	mb\n"
	".subsection 2\n"
	"3:	br 1b\n"
	".previous"
	: "=&r"(prev), "=&r"(cmp), "=m"(*m)
	: "r"((long) old), "r"(new), "m"(*m)
	: "memory" );

	return prev;
}

static __inline__ unsigned long
__cmpxchg_u64(volatile long *m, unsigned long old, unsigned long new)
{
	unsigned long prev, cmp;

	__asm__ __volatile__(
	"1:	ldq_l %0,%5\n"
	"	cmpeq %0,%3,%1\n"
	"	beq %1,2f\n"
	"	mov %4,%1\n"
	"	stq_c %1,%2\n"
	"	beq %1,3f\n"
	"2:	mb\n"
	".subsection 2\n"
	"3:	br 1b\n"
	".previous"
	: "=&r"(prev), "=&r"(cmp), "=m"(*m)
	: "r"((long) old), "r"(new), "m"(*m)
	: "memory" );

	return prev;
}

static __inline__ unsigned long
__cmpxchg(volatile void *ptr, unsigned long old, unsigned long new, int size)
{
	switch (size) {
		case 4:
			return __cmpxchg_u32(ptr, old, new);
		case 8:
			return __cmpxchg_u64(ptr, old, new);
	}
	return old;
}
#define cmpxchg(ptr,o,n)						 \
  ({									 \
     __typeof__(*(ptr)) _o_ = (o);					 \
     __typeof__(*(ptr)) _n_ = (n);					 \
     (__typeof__(*(ptr))) __cmpxchg((ptr), (unsigned long)_o_,		 \
				    (unsigned long)_n_, sizeof(*(ptr))); \
  })

#elif __i386__
static inline unsigned long __cmpxchg(volatile void *ptr, unsigned long old,
				      unsigned long new, int size)
{
	unsigned long prev;
	switch (size) {
	case 1:
		__asm__ __volatile__(LOCK_PREFIX "cmpxchgb %b1,%2"
				     : "=a"(prev)
				     : "q"(new), "m"(*__xg(ptr)), "0"(old)
				     : "memory");
		return prev;
	case 2:
		__asm__ __volatile__(LOCK_PREFIX "cmpxchgw %w1,%2"
				     : "=a"(prev)
				     : "q"(new), "m"(*__xg(ptr)), "0"(old)
				     : "memory");
		return prev;
	case 4:
		__asm__ __volatile__(LOCK_PREFIX "cmpxchgl %1,%2"
				     : "=a"(prev)
				     : "q"(new), "m"(*__xg(ptr)), "0"(old)
				     : "memory");
		return prev;
	}
	return old;
}

#elif defined(__powerpc__)
extern void __cmpxchg_called_with_bad_pointer(void);
static inline unsigned long __cmpxchg(volatile void *ptr, unsigned long old,
                                      unsigned long new, int size)
{
	unsigned long prev;

	switch (size) {
	case 4:
		__asm__ __volatile__(
			"sync;"
			"0:    lwarx %0,0,%1 ;"
			"      cmpl 0,%0,%3;"
			"      bne 1f;"
			"      stwcx. %2,0,%1;"
			"      bne- 0b;"
			"1:    "
			"sync;"
			: "=&r"(prev)
			: "r"(ptr), "r"(new), "r"(old)
			: "cr0", "memory");
		return prev;
	}
	__cmpxchg_called_with_bad_pointer();
	return old;
}

#endif /* i386, powerpc & alpha */

#ifndef __alpha__
#define cmpxchg(ptr,o,n)						\
  ((__typeof__(*(ptr)))__cmpxchg((ptr),(unsigned long)(o),		\
				 (unsigned long)(n),sizeof(*(ptr))))
#endif

#endif /* !__HAVE_ARCH_CMPXCHG */

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

