#ifndef DRM_CORE_MEMORY_H
#define DRM_CORE_MEMORY_H

#if __OS_HAS_AGP && defined(VMAP_4_ARGS)

#ifdef HAVE_PAGE_AGP
#include <asm/agp.h>
#else
#ifdef __powerpc__
#  define PAGE_AGP	__pgprot(_PAGE_KERNEL | _PAGE_NO_CACHE)
#else
#  define PAGE_AGP	PAGE_KERNEL
#endif
#endif

void drm_memory_free_pages(unsigned long address, int order, int area);
void *drm_memory_realloc(void *oldpt, size_t oldsize, size_t size, int area);
unsigned long drm_memory_alloc_pages(int order, int area);
void drm_memory_mem_init(void);
/*
 * Find the drm_map that covers the range [offset, offset+size).
 */
static inline drm_map_t *
drm_lookup_map (unsigned long offset, unsigned long size, drm_device_t *dev)
{
	struct list_head *list;
	drm_map_list_t *r_list;
	drm_map_t *map;

	list_for_each(list, &dev->maplist->head) {
		r_list = (drm_map_list_t *) list;
		map = r_list->map;
		if (!map)
			continue;
		if (map->offset <= offset && (offset + size) <= (map->offset + map->size))
			return map;
	}
	return NULL;
}

static inline void *
agp_remap (unsigned long offset, unsigned long size, drm_device_t *dev)
{
	unsigned long *phys_addr_map, i, num_pages = PAGE_ALIGN(size) / PAGE_SIZE;
	struct drm_agp_mem *agpmem;
	struct page **page_map;
	void *addr;

	size = PAGE_ALIGN(size);

#ifdef __alpha__
	offset -= dev->hose->mem_space->start;
#endif

	for (agpmem = dev->agp->memory; agpmem; agpmem = agpmem->next)
		if (agpmem->bound <= offset
		    && (agpmem->bound + (agpmem->pages << PAGE_SHIFT)) >= (offset + size))
			break;
	if (!agpmem)
		return NULL;

	/*
	 * OK, we're mapping AGP space on a chipset/platform on which memory accesses by
	 * the CPU do not get remapped by the GART.  We fix this by using the kernel's
	 * page-table instead (that's probably faster anyhow...).
	 */
	/* note: use vmalloc() because num_pages could be large... */
	page_map = vmalloc(num_pages * sizeof(struct page *));
	if (!page_map)
		return NULL;

	phys_addr_map = agpmem->memory->memory + (offset - agpmem->bound) / PAGE_SIZE;
	for (i = 0; i < num_pages; ++i)
		page_map[i] = pfn_to_page(phys_addr_map[i] >> PAGE_SHIFT);
	addr = vmap(page_map, num_pages, VM_IOREMAP, PAGE_AGP);
	vfree(page_map);

	return addr;
}

static inline unsigned long
drm_follow_page (void *vaddr)
{
	pgd_t *pgd = pgd_offset_k((unsigned long) vaddr);
	pmd_t *pmd = pmd_offset(pgd, (unsigned long) vaddr);
	pte_t *ptep = pte_offset_kernel(pmd, (unsigned long) vaddr);
	return pte_pfn(*ptep) << PAGE_SHIFT;
}

#endif /* __OS_HAS_AGP && defined(VMAP_4_ARGS) */

static inline void *drm_ioremap(unsigned long offset, unsigned long size, drm_device_t *dev)
{
#if __OS_HAS_AGP && defined(VMAP_4_ARGS)
  if ( drm_core_check_feature(dev, DRIVER_USE_AGP) && dev->agp && dev->agp->cant_use_aperture) {
		drm_map_t *map = drm_lookup_map(offset, size, dev);

		if (map && map->type == _DRM_AGP)
			return agp_remap(offset, size, dev);
	}
#endif

	return ioremap(offset, size);
}

static inline void *drm_ioremap_nocache(unsigned long offset, unsigned long size,
					drm_device_t *dev)
{
#if __OS_HAS_AGP&& defined(VMAP_4_ARGS)
	if ( drm_core_check_feature(dev, DRIVER_USE_AGP) && dev->agp && dev->agp->cant_use_aperture) {
		drm_map_t *map = drm_lookup_map(offset, size, dev);

		if (map && map->type == _DRM_AGP)
			return agp_remap(offset, size, dev);
	}
#endif

	return ioremap_nocache(offset, size);
}

static inline void drm_ioremapfree(void *pt, unsigned long size, drm_device_t *dev)
{
#if __OS_HAS_AGP && defined(VMAP_4_ARGS)
	/*
	 * This is a bit ugly.  It would be much cleaner if the DRM API would use separate
	 * routines for handling mappings in the AGP space.  Hopefully this can be done in
	 * a future revision of the interface...
	 */
	if (drm_core_check_feature(dev, DRIVER_USE_AGP) && dev->agp && dev->agp->cant_use_aperture
	    && ((unsigned long) pt >= VMALLOC_START && (unsigned long) pt < VMALLOC_END))
	{
		unsigned long offset;
		drm_map_t *map;

		offset = drm_follow_page(pt) | ((unsigned long) pt & ~PAGE_MASK);
		map = drm_lookup_map(offset, size, dev);
		if (map && map->type == _DRM_AGP) {
			vunmap(pt);
			return;
		}
	}
#endif

	iounmap(pt);
}

#endif
