#include <sys/param.h>
#include <sys/queue.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/stat.h>
#include <sys/proc.h>
#include <sys/lock.h>
#include <sys/fcntl.h>
#include <sys/uio.h>
#include <sys/filio.h>
#include <sys/sysctl.h>
#include <sys/select.h>
#include <sys/bus.h>
#if __FreeBSD_version >= 400005
#include <sys/taskqueue.h>
#endif

#if __FreeBSD_version >= 400006
#define __REALLY_HAVE_AGP	1
#endif

#define __REALLY_HAVE_MTRR	0

#if __REALLY_HAVE_AGP
#include <pci/agpvar.h>
#endif

#define DRM_DEV_MODE	(S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP)
#define DRM_DEV_UID	0
#define DRM_DEV_GID	0

#define DRM_OS_LOCK	lockmgr(&dev->dev_lock, LK_EXCLUSIVE, 0, curproc)
#define DRM_OS_UNLOCK 	lockmgr(&dev->dev_lock, LK_RELEASE, 0, curproc)
#define DRM_OS_IOCTL	dev_t kdev, u_long cmd, caddr_t data, int flags, struct proc *p
#define DRM_OS_OPEN	/*dev_t kdev, int flags, int fmt, struct proc *p*/
#define DRM_OS_DEVICE	drm_file_t	*priv; \
			drm_device_t	*dev	= kdev->si_drv1
#define DRM_OS_RETURN(v)	return v;
/* NOTE: To keep the same format as linux, we reverse arg1 and arg2 */
#define DRM_OS_COPYTO(arg1, arg2, arg3) \
	copyout( arg2, arg1, arg3 )
#define DRM_OS_COPYFROM(arg1, arg2, arg3) \
	copyin( arg2, arg1, arg3 )

#define DRM_PROT_IOCTL	d_ioctl_t

typedef u_int32_t atomic_t;
typedef u_int32_t cycles_t;
typedef u_int32_t spinlock_t;
typedef u_int32_t u32;
#define atomic_set(p, v)	(*(p) = (v))
#define atomic_read(p)		(*(p))
#define atomic_inc(p)		atomic_add_long(p, 1)
#define atomic_dec(p)		atomic_subtract_long(p, 1)
#define atomic_add(n, p)	atomic_add_long(p, n)
#define atomic_sub(n, p)	atomic_subtract_long(p, n)

/* Fake this */
static __inline unsigned long
test_and_set_bit(int b, volatile unsigned long *p)
{
	int s = splhigh();
	unsigned long m = 1<<b;
	unsigned long r = *p & m;
	*p |= m;
	splx(s);
	return r;
}

static __inline void
clear_bit(int b, volatile unsigned long *p)
{
    atomic_clear_long(p + (b >> 5), 1 << (b & 0x1f));
}

static __inline void
set_bit(int b, volatile unsigned long *p)
{
    atomic_set_long(p + (b >> 5), 1 << (b & 0x1f));
}

static __inline int
test_bit(int b, volatile unsigned long *p)
{
    return p[b >> 5] & (1 << (b & 0x1f));
}

static __inline int
find_first_zero_bit(volatile unsigned long *p, int max)
{
    int b;

    for (b = 0; b < max; b += 32) {
	if (p[b >> 5] != ~0) {
	    for (;;) {
		if ((p[b >> 5] & (1 << (b & 0x1f))) == 0)
		    return b;
		b++;
	    }
	}
    }
    return max;
}

#define spldrm()		spltty()

#define memset(p, v, s)		bzero(p, s)

/*
 * Fake out the module macros for versions of FreeBSD where they don't
 * exist.
 */
#if (__FreeBSD_version < 500002 && __FreeBSD_version > 500000) || __FreeBSD_version < 420000
/* FIXME: again, what's the exact date? */
#define MODULE_VERSION(a,b)		struct __hack
#define MODULE_DEPEND(a,b,c,d,e)	struct __hack

#endif

#define __drm_dummy_lock(lock) (*(__volatile__ unsigned int *)lock)
#define _DRM_CAS(lock,old,new,__ret)				       \
	do {							       \
		int __dummy;	/* Can't mark eax as clobbered */      \
		__asm__ __volatile__(				       \
			"lock ; cmpxchg %4,%1\n\t"		       \
			"setnz %0"				       \
			: "=d" (__ret),				       \
			  "=m" (__drm_dummy_lock(lock)),	       \
			  "=a" (__dummy)			       \
			: "2" (old),				       \
			  "r" (new));				       \
	} while (0)

/* Redefinitions to make templating easy */
#define wait_queue_head_t	int

				/* Macros to make printf easier */
#define DRM_ERROR(fmt, arg...) \
	printf("error: " "[" DRM_NAME ":" __FUNCTION__ "] *ERROR* " fmt , ##arg)
#define DRM_MEM_ERROR(area, fmt, arg...) \
	printf("error: " "[" DRM_NAME ":" __FUNCTION__ ":%s] *ERROR* " fmt , \
	       DRM(mem_stats)[area].name , ##arg)
#define DRM_INFO(fmt, arg...)  printf("info: " "[" DRM_NAME "] " fmt , ##arg)

#if DRM_DEBUG_CODE
#define DRM_DEBUG(fmt, arg...)						  \
	do {								  \
		if (DRM(flags) & DRM_FLAG_DEBUG)				  \
			printf("[" DRM_NAME ":" __FUNCTION__ "] " fmt ,	  \
			       ##arg);					  \
	} while (0)
#else
#define DRM_DEBUG(fmt, arg...)		 do { } while (0)
#endif

#define DRM_PROC_LIMIT (PAGE_SIZE-80)

#define DRM_SYSCTL_PRINT(fmt, arg...)		\
  snprintf(buf, sizeof(buf), fmt, ##arg);	\
  error = SYSCTL_OUT(req, buf, strlen(buf));	\
  if (error) return error;

#define DRM_SYSCTL_PRINT_RET(ret, fmt, arg...)	\
  snprintf(buf, sizeof(buf), fmt, ##arg);	\
  error = SYSCTL_OUT(req, buf, strlen(buf));	\
  if (error) { ret; return error; }


#define DRM_FIND_MAP(dest, o)						\
	do {								\
		drm_map_list_entry_t *listentry;			\
		TAILQ_FOREACH(listentry, dev->maplist, link) {		\
			if ( listentry->map->offset == o ) {		\
				dest = listentry->map;			\
				break;					\
			}						\
		}							\
	} while (0)
