/**
 * \file drm_compat.h
 * Backward compatability definitions for Direct Rendering Manager
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

#ifndef _DRM_COMPAT_H_
#define _DRM_COMPAT_H_

#ifndef minor
#define minor(x) MINOR((x))
#endif

#ifndef MODULE_LICENSE
#define MODULE_LICENSE(x)
#endif

#ifndef preempt_disable
#define preempt_disable()
#define preempt_enable()
#endif

#ifndef module_param
#define module_param(name, type, perm)
#endif

/* older kernels had different irq args */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,19))
#undef DRM_IRQ_ARGS
#define DRM_IRQ_ARGS		int irq, void *arg, struct pt_regs *regs

typedef _Bool bool;
enum {
        false   = 0,
        true    = 1
};

#endif

#ifndef list_for_each_safe
#define list_for_each_safe(pos, n, head)				\
	for (pos = (head)->next, n = pos->next; pos != (head);		\
		pos = n, n = pos->next)
#endif

#ifndef list_for_each_entry
#define list_for_each_entry(pos, head, member)				\
       for (pos = list_entry((head)->next, typeof(*pos), member),	\
                    prefetch(pos->member.next);				\
            &pos->member != (head);					\
            pos = list_entry(pos->member.next, typeof(*pos), member),	\
                    prefetch(pos->member.next))
#endif

#ifndef list_for_each_entry_safe
#define list_for_each_entry_safe(pos, n, head, member)                  \
        for (pos = list_entry((head)->next, typeof(*pos), member),      \
                n = list_entry(pos->member.next, typeof(*pos), member); \
             &pos->member != (head);                                    \
             pos = n, n = list_entry(n->member.next, typeof(*n), member))
#endif

#ifndef __user
#define __user
#endif

#if !defined(__put_page)
#define __put_page(p)           atomic_dec(&(p)->count)
#endif

#if !defined(__GFP_COMP)
#define __GFP_COMP 0
#endif

#if !defined(IRQF_SHARED)
#define IRQF_SHARED SA_SHIRQ
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,10)
static inline int remap_pfn_range(struct vm_area_struct *vma, unsigned long from, unsigned long pfn, unsigned long size, pgprot_t pgprot)
{
  return remap_page_range(vma, from,
			  pfn << PAGE_SHIFT,
			  size,
			  pgprot);
}

static __inline__ void *kcalloc(size_t nmemb, size_t size, int flags)
{
	void *addr;

	addr = kmalloc(size * nmemb, flags);
	if (addr != NULL)
		memset((void *)addr, 0, size * nmemb);

	return addr;
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,16)
#define mutex_lock down
#define mutex_unlock up

#define mutex semaphore

#define mutex_init(a) sema_init((a), 1)

#endif

#ifndef DEFINE_SPINLOCK
#define DEFINE_SPINLOCK(x) spinlock_t x = SPIN_LOCK_UNLOCKED
#endif

/* old architectures */
#ifdef __AMD64__
#define __x86_64__
#endif

/* sysfs __ATTR macro */
#ifndef __ATTR
#define __ATTR(_name,_mode,_show,_store) { \
        .attr = {.name = __stringify(_name), .mode = _mode, .owner = THIS_MODULE },     \
        .show   = _show,                                        \
        .store  = _store,                                       \
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18)
#define vmalloc_user(_size) ({void * tmp = vmalloc(_size);   \
      if (tmp) memset(tmp, 0, _size);			     \
      (tmp);})
#endif

#ifndef list_for_each_entry_safe_reverse
#define list_for_each_entry_safe_reverse(pos, n, head, member)          \
        for (pos = list_entry((head)->prev, typeof(*pos), member),      \
                n = list_entry(pos->member.prev, typeof(*pos), member); \
             &pos->member != (head);                                    \
             pos = n, n = list_entry(n->member.prev, typeof(*n), member))
#endif


/* fixme when functions are upstreamed - upstreamed for 2.6.23 */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,23))
#define DRM_IDR_COMPAT_FN
#define DRM_NO_FAULT
extern unsigned long drm_bo_vm_nopfn(struct vm_area_struct *vma,
				     unsigned long address);
#endif
#ifdef DRM_IDR_COMPAT_FN
int idr_for_each(struct idr *idp,
		 int (*fn)(int id, void *p, void *data), void *data);
void idr_remove_all(struct idr *idp);
#endif


#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18))
void *idr_replace(struct idr *idp, void *ptr, int id);
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,19))
extern unsigned long round_jiffies_relative(unsigned long j);
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,19))
extern struct pci_dev * pci_get_bus_and_slot(unsigned int bus, unsigned int devfn);
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,19))
static inline int kobject_uevent_env(struct kobject *kobj,
                                     enum kobject_action action,
                                     char *envp[])
{
    return 0;
}
#endif

#ifndef PM_EVENT_PRETHAW 
#define PM_EVENT_PRETHAW 3
#endif


#if (defined(CONFIG_X86) && defined(CONFIG_X86_32) && defined(CONFIG_HIGHMEM))
/*
 * pgd_offset_k() is a macro that uses the symbol init_mm,
 * check that it is available.
 */
#if 0
#  if ((LINUX_VERSION_CODE < KERNEL_VERSION(2,6,25)) || \
	defined(CONFIG_UNUSED_SYMBOLS))
#define DRM_KMAP_ATOMIC_PROT_PFN
extern void *kmap_atomic_prot_pfn(unsigned long pfn, enum km_type type,
				  pgprot_t protection);
#  else
#warning "init_mm is not available on this kernel!"
static inline void *kmap_atomic_prot_pfn(unsigned long pfn, enum km_type type,
					 pgprot_t protection)
{
	/* stub */
	return NULL;
}
#  endif /* no init_mm */
#endif
#endif

#ifndef DMA_BIT_MASK
#define DMA_BIT_MASK(n) (((n) == 64) ? ~0ULL : (1ULL<<(n)) - 1)
#endif

#ifndef VM_CAN_NONLINEAR
#define DRM_VM_NOPAGE 1
#endif

#ifdef DRM_VM_NOPAGE

extern struct page *drm_vm_nopage(struct vm_area_struct *vma,
				  unsigned long address, int *type);

extern struct page *drm_vm_shm_nopage(struct vm_area_struct *vma,
				      unsigned long address, int *type);

extern struct page *drm_vm_dma_nopage(struct vm_area_struct *vma,
				      unsigned long address, int *type);

extern struct page *drm_vm_sg_nopage(struct vm_area_struct *vma,
				     unsigned long address, int *type);
#endif

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,26)
#define drm_on_each_cpu(handler, data, wait) \
	on_each_cpu(handler, data, wait)
#else
#define drm_on_each_cpu(handler, data, wait) \
	on_each_cpu(handler, data, wait, 1)
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26))
#define drm_core_ioremap_wc drm_core_ioremap
#endif

#ifndef OS_HAS_GEM
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,27))
#define OS_HAS_GEM 1
#else
#define OS_HAS_GEM 0
#endif
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,27))
#define set_page_locked SetPageLocked
#elif (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,27))
/*
 * The kernel provides __set_page_locked, which uses the non-atomic
 * __set_bit function. Let's use the atomic set_bit just in case.
 */
static inline void set_page_locked(struct page *page)
{
	set_bit(PG_locked, &page->flags);
}
#endif

#ifndef current_euid
#define current_euid() (current->euid)
#endif

#endif
