/**************************************************************************
 *
 * This kernel module is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 **************************************************************************/
/*
 * This code provides access to unexported mm kernel features. It is necessary
 * to use the new DRM memory manager code with kernels that don't support it
 * directly.
 *
 * Authors: Thomas Hellstrom <thomas-at-tungstengraphics-dot-com>
 *          Linux kernel mm subsystem authors.
 *          (Most code taken from there).
 */

#include "drmP.h"

#if defined(CONFIG_X86) && (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,15))

/*
 * These have bad performance in the AGP module for the indicated kernel versions.
 */

int drm_map_page_into_agp(struct page *page)
{
        int i;
        i = change_page_attr(page, 1, PAGE_KERNEL_NOCACHE);
        /* Caller's responsibility to call global_flush_tlb() for
         * performance reasons */
        return i;
}

int drm_unmap_page_from_agp(struct page *page)
{
        int i;
        i = change_page_attr(page, 1, PAGE_KERNEL);
        /* Caller's responsibility to call global_flush_tlb() for
         * performance reasons */
        return i;
}
#endif


#ifdef DRM_IDR_COMPAT_FN
/* only called when idp->lock is held */
static void __free_layer(struct idr *idp, struct idr_layer *p)
{
	p->ary[0] = idp->id_free;
	idp->id_free = p;
	idp->id_free_cnt++;
}

static void free_layer(struct idr *idp, struct idr_layer *p)
{
	unsigned long flags;

	/*
	 * Depends on the return element being zeroed.
	 */
	spin_lock_irqsave(&idp->lock, flags);
	__free_layer(idp, p);
	spin_unlock_irqrestore(&idp->lock, flags);
}

/**
 * idr_for_each - iterate through all stored pointers
 * @idp: idr handle
 * @fn: function to be called for each pointer
 * @data: data passed back to callback function
 *
 * Iterate over the pointers registered with the given idr.  The
 * callback function will be called for each pointer currently
 * registered, passing the id, the pointer and the data pointer passed
 * to this function.  It is not safe to modify the idr tree while in
 * the callback, so functions such as idr_get_new and idr_remove are
 * not allowed.
 *
 * We check the return of @fn each time. If it returns anything other
 * than 0, we break out and return that value.
 *
* The caller must serialize idr_find() vs idr_get_new() and idr_remove().
 */
int idr_for_each(struct idr *idp,
		 int (*fn)(int id, void *p, void *data), void *data)
{
	int n, id, max, error = 0;
	struct idr_layer *p;
	struct idr_layer *pa[MAX_LEVEL];
	struct idr_layer **paa = &pa[0];

	n = idp->layers * IDR_BITS;
	p = idp->top;
	max = 1 << n;

	id = 0;
	while (id < max) {
		while (n > 0 && p) {
			n -= IDR_BITS;
			*paa++ = p;
			p = p->ary[(id >> n) & IDR_MASK];
		}

		if (p) {
			error = fn(id, (void *)p, data);
			if (error)
				break;
		}

		id += 1 << n;
		while (n < fls(id)) {
			n += IDR_BITS;
			p = *--paa;
		}
	}

	return error;
}
EXPORT_SYMBOL(idr_for_each);

/**
 * idr_remove_all - remove all ids from the given idr tree
 * @idp: idr handle
 *
 * idr_destroy() only frees up unused, cached idp_layers, but this
 * function will remove all id mappings and leave all idp_layers
 * unused.
 *
 * A typical clean-up sequence for objects stored in an idr tree, will
 * use idr_for_each() to free all objects, if necessay, then
 * idr_remove_all() to remove all ids, and idr_destroy() to free
 * up the cached idr_layers.
 */
void idr_remove_all(struct idr *idp)
{
       int n, id, max, error = 0;
       struct idr_layer *p;
       struct idr_layer *pa[MAX_LEVEL];
       struct idr_layer **paa = &pa[0];

       n = idp->layers * IDR_BITS;
       p = idp->top;
       max = 1 << n;

       id = 0;
       while (id < max && !error) {
               while (n > IDR_BITS && p) {
                       n -= IDR_BITS;
                       *paa++ = p;
                       p = p->ary[(id >> n) & IDR_MASK];
               }

               id += 1 << n;
               while (n < fls(id)) {
                       if (p) {
                               memset(p, 0, sizeof *p);
                               free_layer(idp, p);
                       }
                       n += IDR_BITS;
                       p = *--paa;
               }
       }
       idp->top = NULL;
       idp->layers = 0;
}
EXPORT_SYMBOL(idr_remove_all);

#endif /* DRM_IDR_COMPAT_FN */



#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18))
/**
 * idr_replace - replace pointer for given id
 * @idp: idr handle
 * @ptr: pointer you want associated with the id
 * @id: lookup key
 *
 * Replace the pointer registered with an id and return the old value.
 * A -ENOENT return indicates that @id was not found.
 * A -EINVAL return indicates that @id was not within valid constraints.
 *
 * The caller must serialize vs idr_find(), idr_get_new(), and idr_remove().
 */
void *idr_replace(struct idr *idp, void *ptr, int id)
{
	int n;
	struct idr_layer *p, *old_p;

	n = idp->layers * IDR_BITS;
	p = idp->top;

	id &= MAX_ID_MASK;

	if (id >= (1 << n))
		return ERR_PTR(-EINVAL);

	n -= IDR_BITS;
	while ((n > 0) && p) {
		p = p->ary[(id >> n) & IDR_MASK];
		n -= IDR_BITS;
	}

	n = id & IDR_MASK;
	if (unlikely(p == NULL || !test_bit(n, &p->bitmap)))
		return ERR_PTR(-ENOENT);

	old_p = p->ary[n];
	p->ary[n] = ptr;

	return (void *)old_p;
}
EXPORT_SYMBOL(idr_replace);
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,19))
static __inline__ unsigned long __round_jiffies(unsigned long j, int cpu)
{
	int rem;
	unsigned long original = j;

	j += cpu * 3;

	rem = j % HZ;

	if (rem < HZ/4) /* round down */
		j = j - rem;
	else /* round up */
		j = j - rem + HZ;

	/* now that we have rounded, subtract the extra skew again */
	j -= cpu * 3;

	if (j <= jiffies) /* rounding ate our timeout entirely; */
		return original;
	return j;
}

static __inline__ unsigned long __round_jiffies_relative(unsigned long j, int cpu)
{
	return  __round_jiffies(j + jiffies, cpu) - jiffies;
}

unsigned long round_jiffies_relative(unsigned long j)
{
	return __round_jiffies_relative(j, raw_smp_processor_id());
}
EXPORT_SYMBOL(round_jiffies_relative);
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,19))
struct pci_dev * pci_get_bus_and_slot(unsigned int bus, unsigned int devfn)
{
    struct pci_dev *dev = NULL;

    while ((dev = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, dev)) != NULL) {
        if (pci_domain_nr(dev->bus) == 0 &&
           (dev->bus->number == bus && dev->devfn == devfn))
            return dev;
   }
   return NULL;
}
EXPORT_SYMBOL(pci_get_bus_and_slot);
#endif

#if defined(DRM_KMAP_ATOMIC_PROT_PFN)
void *kmap_atomic_prot_pfn(unsigned long pfn, enum km_type type,
			   pgprot_t protection)
{
	enum fixed_addresses idx;
	unsigned long vaddr;
	static pte_t *km_pte;
	int level;
	static int initialized = 0;

	if (unlikely(!initialized)) {
		km_pte = lookup_address(__fix_to_virt(FIX_KMAP_BEGIN), &level);
		initialized = 1;
	}

	pagefault_disable();
	idx = type + KM_TYPE_NR*smp_processor_id();
	vaddr = __fix_to_virt(FIX_KMAP_BEGIN + idx);
	set_pte(km_pte-idx, pfn_pte(pfn, protection));

	return (void*) vaddr;
}

EXPORT_SYMBOL(kmap_atomic_prot_pfn);
#endif

