/*
 * Copyright (C) The Weather Channel, Inc.  2002.  All Rights Reserved.
 * Copyright 2005 Stephane Marchesin
 *
 * The Weather Channel (TM) funded Tungsten Graphics to develop the
 * initial release of the Radeon 8500 driver under the XFree86 license.
 * This notice must be preserved.
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
 * THE AUTHORS AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Keith Whitwell <keith@tungstengraphics.com>
 */


#include "drmP.h"
#include "drm.h"
#include "drm_sarea.h"
#include "nouveau_drv.h"

static struct mem_block *
split_block(struct mem_block *p, uint64_t start, uint64_t size,
	    struct drm_file *file_priv)
{
	/* Maybe cut off the start of an existing block */
	if (start > p->start) {
		struct mem_block *newblock =
			drm_alloc(sizeof(*newblock), DRM_MEM_BUFS);
		if (!newblock)
			goto out;
		newblock->start = start;
		newblock->size = p->size - (start - p->start);
		newblock->file_priv = NULL;
		newblock->next = p->next;
		newblock->prev = p;
		p->next->prev = newblock;
		p->next = newblock;
		p->size -= newblock->size;
		p = newblock;
	}

	/* Maybe cut off the end of an existing block */
	if (size < p->size) {
		struct mem_block *newblock =
			drm_alloc(sizeof(*newblock), DRM_MEM_BUFS);
		if (!newblock)
			goto out;
		newblock->start = start + size;
		newblock->size = p->size - size;
		newblock->file_priv = NULL;
		newblock->next = p->next;
		newblock->prev = p;
		p->next->prev = newblock;
		p->next = newblock;
		p->size = size;
	}

out:
	/* Our block is in the middle */
	p->file_priv = file_priv;
	return p;
}

struct mem_block *
nouveau_mem_alloc_block(struct mem_block *heap, uint64_t size,
			int align2, struct drm_file *file_priv, int tail)
{
	struct mem_block *p;
	uint64_t mask = (1 << align2) - 1;

	if (!heap)
		return NULL;

	if (tail) {
		list_for_each_prev(p, heap) {
			uint64_t start = ((p->start + p->size) - size) & ~mask;

			if (p->file_priv == 0 && start >= p->start &&
			    start + size <= p->start + p->size)
				return split_block(p, start, size, file_priv);
		}
	} else {
		list_for_each(p, heap) {
			uint64_t start = (p->start + mask) & ~mask;

			if (p->file_priv == 0 &&
			    start + size <= p->start + p->size)
				return split_block(p, start, size, file_priv);
		}
	}

	return NULL;
}

static struct mem_block *find_block(struct mem_block *heap, uint64_t start)
{
	struct mem_block *p;

	list_for_each(p, heap)
		if (p->start == start)
			return p;

	return NULL;
}

void nouveau_mem_free_block(struct mem_block *p)
{
	p->file_priv = NULL;

	/* Assumes a single contiguous range.  Needs a special file_priv in
	 * 'heap' to stop it being subsumed.
	 */
	if (p->next->file_priv == 0) {
		struct mem_block *q = p->next;
		p->size += q->size;
		p->next = q->next;
		p->next->prev = p;
		drm_free(q, sizeof(*q), DRM_MEM_BUFS);
	}

	if (p->prev->file_priv == 0) {
		struct mem_block *q = p->prev;
		q->size += p->size;
		q->next = p->next;
		q->next->prev = q;
		drm_free(p, sizeof(*q), DRM_MEM_BUFS);
	}
}

/* Initialize.  How to check for an uninitialized heap?
 */
int nouveau_mem_init_heap(struct mem_block **heap, uint64_t start,
			  uint64_t size)
{
	struct mem_block *blocks = drm_alloc(sizeof(*blocks), DRM_MEM_BUFS);

	if (!blocks)
		return -ENOMEM;

	*heap = drm_alloc(sizeof(**heap), DRM_MEM_BUFS);
	if (!*heap) {
		drm_free(blocks, sizeof(*blocks), DRM_MEM_BUFS);
		return -ENOMEM;
	}

	blocks->start = start;
	blocks->size = size;
	blocks->file_priv = NULL;
	blocks->next = blocks->prev = *heap;

	memset(*heap, 0, sizeof(**heap));
	(*heap)->file_priv = (struct drm_file *) - 1;
	(*heap)->next = (*heap)->prev = blocks;
	return 0;
}

/*
 * Free all blocks associated with the releasing file_priv
 */
void nouveau_mem_release(struct drm_file *file_priv, struct mem_block *heap)
{
	struct mem_block *p;

	if (!heap || !heap->next)
		return;

	list_for_each(p, heap) {
		if (p->file_priv == file_priv)
			p->file_priv = NULL;
	}

	/* Assumes a single contiguous range.  Needs a special file_priv in
	 * 'heap' to stop it being subsumed.
	 */
	list_for_each(p, heap) {
		while ((p->file_priv == 0) && (p->next->file_priv == 0) &&
		       (p->next!=heap)) {
			struct mem_block *q = p->next;
			p->size += q->size;
			p->next = q->next;
			p->next->prev = p;
			drm_free(q, sizeof(*q), DRM_MEM_DRIVER);
		}
	}
}

/*
 * NV50 VM helpers
 */
#define VMBLOCK (512*1024*1024)
static int
nv50_mem_vm_preinit(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	dev_priv->vm_gart_base = roundup(0, VMBLOCK);
	dev_priv->vm_gart_size = VMBLOCK;

	dev_priv->vm_vram_base = dev_priv->vm_gart_base + dev_priv->vm_gart_size;
	dev_priv->vm_vram_size = roundup(nouveau_mem_fb_amount(dev), VMBLOCK);
	dev_priv->vm_end = dev_priv->vm_vram_base + dev_priv->vm_vram_size;

	DRM_DEBUG("NV50VM: GART 0x%016llx-0x%016llx\n",
		  dev_priv->vm_gart_base,
		  dev_priv->vm_gart_base + dev_priv->vm_gart_size - 1);
	DRM_DEBUG("NV50VM: VRAM 0x%016llx-0x%016llx\n",
		  dev_priv->vm_vram_base,
		  dev_priv->vm_vram_base + dev_priv->vm_vram_size - 1);
	return 0;
}

static void
nv50_mem_vm_takedown(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int i;

	if (!dev_priv->vm_vram_pt)
		return;

	for (i = 0; i < dev_priv->vm_vram_pt_nr; i++) {
		if (!dev_priv->vm_vram_pt[i])
			break;

		nouveau_gpuobj_del(dev, &dev_priv->vm_vram_pt[i]);
	}

	drm_free(dev_priv->vm_vram_pt,
		 dev_priv->vm_vram_pt_nr * sizeof(struct nouveau_gpuobj *),
		 DRM_MEM_DRIVER);
	dev_priv->vm_vram_pt = NULL;
	dev_priv->vm_vram_pt_nr = 0;
}

static int
nv50_mem_vm_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	const int nr = dev_priv->vm_vram_size / VMBLOCK;
	int i, ret;

	dev_priv->vm_vram_pt_nr = nr;
	dev_priv->vm_vram_pt = drm_calloc(nr, sizeof(struct nouveau_gpuobj *),
					  DRM_MEM_DRIVER);
	if (!dev_priv->vm_vram_pt)
		return -ENOMEM;

	for (i = 0; i < nr; i++) {
		ret = nouveau_gpuobj_new(dev, NULL, VMBLOCK/65536*8, 0,
					 NVOBJ_FLAG_ZERO_ALLOC |
					 NVOBJ_FLAG_ALLOW_NO_REFS,
					 &dev_priv->vm_vram_pt[i]);
		if (ret) {
			DRM_ERROR("Error creating VRAM page tables: %d\n", ret);
			return ret;
		}
	}

	return 0;
}

int
nv50_mem_vm_bind_linear(struct drm_device *dev, uint64_t virt, uint32_t size,
			uint32_t flags, uint64_t phys)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpuobj **pgt;
	unsigned psz, pfl;

	if (virt >= dev_priv->vm_gart_base &&
	    (virt + size) < (dev_priv->vm_gart_base + dev_priv->vm_gart_size)) {
		psz = 4096;
		pgt = &dev_priv->gart_info.sg_ctxdma;
		pfl = 0x21;
		virt -= dev_priv->vm_gart_base;
	} else
	if (virt >= dev_priv->vm_vram_base &&
	    (virt + size) < (dev_priv->vm_vram_base + dev_priv->vm_vram_size)) {
		psz = 65536;
		pgt = dev_priv->vm_vram_pt;
		pfl = 0x01;
		virt -= dev_priv->vm_vram_base;
	} else {
		DRM_ERROR("Invalid address: 0x%16llx-0x%16llx\n",
			  virt, virt + size - 1);
		return -EINVAL;
	}

	size &= ~(psz - 1);

	if (flags & 0x80000000) {
		while (size) {
			struct nouveau_gpuobj *pt = pgt[virt / (512*1024*1024)];
			int pte = ((virt % (512*1024*1024)) / psz) * 2;

			INSTANCE_WR(pt, pte++, 0x00000000);
			INSTANCE_WR(pt, pte++, 0x00000000);

			size -= psz;
			virt += psz;
		}
	} else {
		while (size) {
			struct nouveau_gpuobj *pt = pgt[virt / (512*1024*1024)];
			int pte = ((virt % (512*1024*1024)) / psz) * 2;

			INSTANCE_WR(pt, pte++, phys | pfl);
			INSTANCE_WR(pt, pte++, flags);

			size -= psz;
			phys += psz;
			virt += psz;
		}
	}

	return 0;
}

void
nv50_mem_vm_unbind(struct drm_device *dev, uint64_t virt, uint32_t size)
{
	nv50_mem_vm_bind_linear(dev, virt, size, 0x80000000, 0);
}

/*
 * Cleanup everything
 */
void nouveau_mem_takedown(struct mem_block **heap)
{
	struct mem_block *p;

	if (!*heap)
		return;

	for (p = (*heap)->next; p != *heap;) {
		struct mem_block *q = p;
		p = p->next;
		drm_free(q, sizeof(*q), DRM_MEM_DRIVER);
	}

	drm_free(*heap, sizeof(**heap), DRM_MEM_DRIVER);
	*heap = NULL;
}

void nouveau_mem_close(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	if (dev_priv->card_type >= NV_50)
		nv50_mem_vm_takedown(dev);

	if (!dev_priv->mm_enabled) {
		nouveau_mem_takedown(&dev_priv->agp_heap);
		nouveau_mem_takedown(&dev_priv->fb_heap);
		if (dev_priv->pci_heap)
			nouveau_mem_takedown(&dev_priv->pci_heap);
	} else {
		drm_bo_driver_finish(dev);
	}

	if (dev_priv->fb_mtrr) {
		drm_mtrr_del(dev_priv->fb_mtrr, drm_get_resource_start(dev, 1),
			     drm_get_resource_len(dev, 1), DRM_MTRR_WC);
		dev_priv->fb_mtrr = 0;
	}
}

/*XXX won't work on BSD because of pci_read_config_dword */
static uint32_t
nouveau_mem_fb_amount_igp(struct drm_device *dev)
{
#if defined(__linux__) && (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,19))
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pci_dev *bridge;
	uint32_t mem;

	bridge = pci_get_bus_and_slot(0, PCI_DEVFN(0,1));
	if (!bridge) {
		DRM_ERROR("no bridge device\n");
		return 0;
	}

	if (dev_priv->flags&NV_NFORCE) {
		pci_read_config_dword(bridge, 0x7C, &mem);
		return (uint64_t)(((mem >> 6) & 31) + 1)*1024*1024;
	} else
	if(dev_priv->flags&NV_NFORCE2) {
		pci_read_config_dword(bridge, 0x84, &mem);
		return (uint64_t)(((mem >> 4) & 127) + 1)*1024*1024;
	}

	DRM_ERROR("impossible!\n");
#else
	DRM_ERROR("Linux kernel >= 2.6.19 required to check for igp memory amount\n");
#endif

	return 0;
}

/* returns the amount of FB ram in bytes */
uint64_t nouveau_mem_fb_amount(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv=dev->dev_private;
	switch(dev_priv->card_type)
	{
		case NV_04:
		case NV_05:
			if (nv_rd32(NV03_BOOT_0) & 0x00000100) {
				return (((nv_rd32(NV03_BOOT_0) >> 12) & 0xf)*2+2)*1024*1024;
			} else
			switch(nv_rd32(NV03_BOOT_0)&NV03_BOOT_0_RAM_AMOUNT)
			{
				case NV04_BOOT_0_RAM_AMOUNT_32MB:
					return 32*1024*1024;
				case NV04_BOOT_0_RAM_AMOUNT_16MB:
					return 16*1024*1024;
				case NV04_BOOT_0_RAM_AMOUNT_8MB:
					return 8*1024*1024;
				case NV04_BOOT_0_RAM_AMOUNT_4MB:
					return 4*1024*1024;
			}
			break;
		case NV_10:
		case NV_11:
		case NV_17:
		case NV_20:
		case NV_30:
		case NV_40:
		case NV_44:
		case NV_50:
		default:
			if (dev_priv->flags & (NV_NFORCE | NV_NFORCE2)) {
				return nouveau_mem_fb_amount_igp(dev);
			} else {
				uint64_t mem;

				mem = (nv_rd32(NV04_FIFO_DATA) &
				       NV10_FIFO_DATA_RAM_AMOUNT_MB_MASK) >>
				      NV10_FIFO_DATA_RAM_AMOUNT_MB_SHIFT;
				return mem*1024*1024;
			}
			break;
	}

	DRM_ERROR("Unable to detect video ram size. Please report your setup to " DRIVER_EMAIL "\n");
	return 0;
}

static void nouveau_mem_reset_agp(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t saved_pci_nv_1, saved_pci_nv_19, pmc_enable;

	saved_pci_nv_1 = nv_rd32(NV04_PBUS_PCI_NV_1);
	saved_pci_nv_19 = nv_rd32(NV04_PBUS_PCI_NV_19);

	/* clear busmaster bit */
	nv_wr32(NV04_PBUS_PCI_NV_1, saved_pci_nv_1 & ~0x4);
	/* clear SBA and AGP bits */
	nv_wr32(NV04_PBUS_PCI_NV_19, saved_pci_nv_19 & 0xfffff0ff);

	/* power cycle pgraph, if enabled */
	pmc_enable = nv_rd32(NV03_PMC_ENABLE);
	if (pmc_enable & NV_PMC_ENABLE_PGRAPH) {
		nv_wr32(NV03_PMC_ENABLE, pmc_enable & ~NV_PMC_ENABLE_PGRAPH);
		nv_wr32(NV03_PMC_ENABLE, nv_rd32(NV03_PMC_ENABLE) |
				NV_PMC_ENABLE_PGRAPH);
	}

	/* and restore (gives effect of resetting AGP) */
	nv_wr32(NV04_PBUS_PCI_NV_19, saved_pci_nv_19);
	nv_wr32(NV04_PBUS_PCI_NV_1, saved_pci_nv_1);
}

static int
nouveau_mem_init_agp(struct drm_device *dev, int ttm)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct drm_agp_info info;
	struct drm_agp_mode mode;
	int ret;

	nouveau_mem_reset_agp(dev);

	ret = drm_agp_acquire(dev);
	if (ret) {
		DRM_ERROR("Unable to acquire AGP: %d\n", ret);
		return ret;
	}

	ret = drm_agp_info(dev, &info);
	if (ret) {
		DRM_ERROR("Unable to get AGP info: %d\n", ret);
		return ret;
	}

	/* see agp.h for the AGPSTAT_* modes available */
	mode.mode = info.mode;
	ret = drm_agp_enable(dev, mode);
	if (ret) {
		DRM_ERROR("Unable to enable AGP: %d\n", ret);
		return ret;
	}

	if (!ttm) {
		struct drm_agp_buffer agp_req;
		struct drm_agp_binding bind_req;

		agp_req.size = info.aperture_size;
		agp_req.type = 0;
		ret = drm_agp_alloc(dev, &agp_req);
		if (ret) {
			DRM_ERROR("Unable to alloc AGP: %d\n", ret);
				return ret;
		}

		bind_req.handle = agp_req.handle;
		bind_req.offset = 0;
		ret = drm_agp_bind(dev, &bind_req);
		if (ret) {
			DRM_ERROR("Unable to bind AGP: %d\n", ret);
			return ret;
		}
	}

	dev_priv->gart_info.type	= NOUVEAU_GART_AGP;
	dev_priv->gart_info.aper_base	= info.aperture_base;
	dev_priv->gart_info.aper_size	= info.aperture_size;
	return 0;
}

int
nouveau_mem_init_ttm(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t vram_size, bar1_size, text_size;
	int ret;

	dev_priv->fb_phys = drm_get_resource_start(dev, 1);
	dev_priv->gart_info.type = NOUVEAU_GART_NONE;

	if (dev_priv->card_type >= NV_50) {
		ret = nv50_mem_vm_preinit(dev);
		if (ret)
			return ret;
	}

	drm_bo_driver_init(dev);

	/* non-mappable vram */
	dev_priv->fb_available_size = nouveau_mem_fb_amount(dev);
	dev_priv->fb_available_size -= dev_priv->ramin_rsvd_vram;
	vram_size = dev_priv->fb_available_size >> PAGE_SHIFT;
	bar1_size = drm_get_resource_len(dev, 1) >> PAGE_SHIFT;
	text_size = (256 * 1024) >> PAGE_SHIFT;
	if (bar1_size < vram_size) {
		if ((ret = drm_bo_init_mm(dev, DRM_BO_MEM_PRIV0, bar1_size,
					  vram_size - bar1_size, 1))) {
			DRM_ERROR("Failed PRIV0 mm init: %d\n", ret);
			return ret;
		}
		vram_size = bar1_size;
	}

	/* mappable vram */
	if ((ret = drm_bo_init_mm(dev, DRM_BO_MEM_VRAM, text_size,
				  vram_size - text_size, 1))) {
		DRM_ERROR("Failed VRAM mm init: %d\n", ret);
		return ret;
	}

	/* GART */
#if !defined(__powerpc__) && !defined(__ia64__)
	if (drm_device_is_agp(dev) && dev->agp) {
		if ((ret = nouveau_mem_init_agp(dev, 1)))
			DRM_ERROR("Error initialising AGP: %d\n", ret);
	}
#endif

	if (dev_priv->gart_info.type == NOUVEAU_GART_NONE) {
		if ((ret = nouveau_sgdma_init(dev)))
			DRM_ERROR("Error initialising PCI SGDMA: %d\n", ret);
	}

	if ((ret = drm_bo_init_mm(dev, DRM_BO_MEM_TT, 0,
				  dev_priv->gart_info.aper_size >> PAGE_SHIFT,
				  1))) {
		DRM_ERROR("Failed TT mm init: %d\n", ret);
		return ret;
	}

	dev_priv->fb_mtrr = drm_mtrr_add(drm_get_resource_start(dev, 1),
					 drm_get_resource_len(dev, 1),
					 DRM_MTRR_WC);

	if (dev_priv->card_type >= NV_50) {
		ret = nv50_mem_vm_init(dev);
		if (ret) {
			DRM_ERROR("Error creating VM page tables: %d\n", ret);
			return ret;
		}
	}

	return 0;
}

int nouveau_mem_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t fb_size, vram_base, gart_base;
	int ret = 0;

	dev_priv->agp_heap = dev_priv->pci_heap = dev_priv->fb_heap = NULL;
	dev_priv->fb_phys = 0;
	dev_priv->gart_info.type = NOUVEAU_GART_NONE;

	/* setup a mtrr over the FB */
	dev_priv->fb_mtrr = drm_mtrr_add(drm_get_resource_start(dev, 1),
					 nouveau_mem_fb_amount(dev),
					 DRM_MTRR_WC);

	if (dev_priv->card_type >= NV_50) {
		ret = nv50_mem_vm_preinit(dev);
		if (ret)
			return ret;
	}
	gart_base = dev_priv->vm_gart_base;
	vram_base = dev_priv->vm_vram_base;

	/* Init FB */
	dev_priv->fb_phys=drm_get_resource_start(dev,1);
	fb_size = nouveau_mem_fb_amount(dev);
	/* On at least NV40, RAMIN is actually at the end of vram.
	 * We don't want to allocate this... */
	if (dev_priv->card_type >= NV_40)
		fb_size -= dev_priv->ramin_rsvd_vram;
	dev_priv->fb_available_size = fb_size;
	DRM_DEBUG("Available VRAM: %dKiB\n", fb_size>>10);

	if (fb_size>256*1024*1024) {
		/* On cards with > 256Mb, you can't map everything.
		 * So we create a second FB heap for that type of memory */
		if (nouveau_mem_init_heap(&dev_priv->fb_heap,
					  vram_base, 256*1024*1024))
			return -ENOMEM;
		if (nouveau_mem_init_heap(&dev_priv->fb_nomap_heap,
					  vram_base + 256*1024*1024,
					  fb_size - 256*1024*1024))
			return -ENOMEM;
	} else {
		if (nouveau_mem_init_heap(&dev_priv->fb_heap, vram_base, fb_size))
			return -ENOMEM;
		dev_priv->fb_nomap_heap=NULL;
	}

#if !defined(__powerpc__) && !defined(__ia64__)
	/* Init AGP / NV50 PCIEGART */
	if (drm_device_is_agp(dev) && dev->agp) {
		if ((ret = nouveau_mem_init_agp(dev, 0)))
			DRM_ERROR("Error initialising AGP: %d\n", ret);
	}
#endif

	/*Note: this is *not* just NV50 code, but only used on NV50 for now */
	if (dev_priv->gart_info.type == NOUVEAU_GART_NONE &&
	    dev_priv->card_type >= NV_50) {
		ret = nouveau_sgdma_init(dev);
		if (!ret) {
			ret = nouveau_sgdma_nottm_hack_init(dev);
			if (ret)
				nouveau_sgdma_takedown(dev);
		}

		if (ret)
			DRM_ERROR("Error initialising SG DMA: %d\n", ret);
	}

	if (dev_priv->gart_info.type != NOUVEAU_GART_NONE) {
		if (nouveau_mem_init_heap(&dev_priv->agp_heap, gart_base,
					  dev_priv->gart_info.aper_size)) {
			if (dev_priv->gart_info.type == NOUVEAU_GART_SGDMA) {
				nouveau_sgdma_nottm_hack_takedown(dev);
				nouveau_sgdma_takedown(dev);
			}
		}
	}

	/* NV04-NV40 PCIEGART */
	if (!dev_priv->agp_heap && dev_priv->card_type < NV_50) {
		struct drm_scatter_gather sgreq;

		DRM_DEBUG("Allocating sg memory for PCI DMA\n");
		sgreq.size = 16 << 20; //16MB of PCI scatter-gather zone

		if (drm_sg_alloc(dev, &sgreq)) {
			DRM_ERROR("Unable to allocate %ldMB of scatter-gather"
				  " pages for PCI DMA!",sgreq.size>>20);
		} else {
			if (nouveau_mem_init_heap(&dev_priv->pci_heap, 0,
						  dev->sg->pages * PAGE_SIZE)) {
				DRM_ERROR("Unable to initialize pci_heap!");
			}
		}
	}

	if (dev_priv->card_type >= NV_50) {
		ret = nv50_mem_vm_init(dev);
		if (ret) {
			DRM_ERROR("Error creating VM page tables: %d\n", ret);
			return ret;
		}
	}

	return 0;
}

struct mem_block *
nouveau_mem_alloc(struct drm_device *dev, int alignment, uint64_t size,
		  int flags, struct drm_file *file_priv)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct mem_block *block;
	int type, tail = !(flags & NOUVEAU_MEM_USER);
	int ret;

	/* This case will only ever be hit through in-kernel use */
	if (dev_priv->mm_enabled) {
		uint64_t memtype = 0;

		size = (size + 65535) & ~65535;
		if (alignment < (65536 / PAGE_SIZE))
			alignment = (65536 / PAGE_SIZE);

		block = drm_calloc(1, sizeof(*block), DRM_MEM_DRIVER);
		if (!block)
			return NULL;

		if (flags & (NOUVEAU_MEM_FB | NOUVEAU_MEM_FB_ACCEPTABLE))
			memtype |= DRM_BO_FLAG_MEM_VRAM;
		if (flags & NOUVEAU_MEM_AGP)
			memtype |= DRM_BO_FLAG_MEM_TT;
		if (flags & NOUVEAU_MEM_NOVM)
			memtype |= DRM_NOUVEAU_BO_FLAG_NOVM;

		ret = drm_buffer_object_create(dev, size, drm_bo_type_kernel,
					       DRM_BO_FLAG_READ |
					       DRM_BO_FLAG_WRITE |
					       DRM_BO_FLAG_NO_EVICT |
					       memtype, DRM_BO_HINT_DONT_FENCE,
					       alignment, 0, &block->bo);
		if (ret) {
			drm_free(block, sizeof(*block), DRM_MEM_DRIVER);
			return NULL;
		}

		block->start = block->bo->offset;
		block->size = block->bo->mem.size;

		if (block->bo->mem.mem_type == DRM_BO_MEM_VRAM) {
			block->flags = NOUVEAU_MEM_FB;
			if (flags & NOUVEAU_MEM_NOVM) {
				block->flags |= NOUVEAU_MEM_NOVM;
				block->start -= dev_priv->vm_vram_base;
			}
		} else
		if (block->bo->mem.mem_type == DRM_BO_MEM_TT) {
			block->flags = NOUVEAU_MEM_AGP;
		}

		return block;
	}

	/*
	 * Make things easier on ourselves: all allocations are page-aligned.
	 * We need that to map allocated regions into the user space
	 */
	if (alignment < PAGE_SIZE)
		alignment = PAGE_SIZE;

	/* Align allocation sizes to 64KiB blocks on G8x.  We use a 64KiB
	 * page size in the GPU VM.
	 */
	if (flags & NOUVEAU_MEM_FB && dev_priv->card_type >= NV_50) {
		size = (size + 65535) & ~65535;
		if (alignment < 65536)
			alignment = 65536;
	}

	/* Further down wants alignment in pages, not bytes */
	alignment >>= PAGE_SHIFT;

	/*
	 * Warn about 0 sized allocations, but let it go through. It'll return 1 page
	 */
	if (size == 0)
		DRM_INFO("warning : 0 byte allocation\n");

	/*
	 * Keep alloc size a multiple of the page size to keep drm_addmap() happy
	 */
	if (size & (~PAGE_MASK))
		size = ((size/PAGE_SIZE) + 1) * PAGE_SIZE;


#define NOUVEAU_MEM_ALLOC_AGP {\
		type=NOUVEAU_MEM_AGP;\
                block = nouveau_mem_alloc_block(dev_priv->agp_heap, size,\
                                                alignment, file_priv, tail); \
                if (block) goto alloc_ok;\
	        }

#define NOUVEAU_MEM_ALLOC_PCI {\
                type = NOUVEAU_MEM_PCI;\
                block = nouveau_mem_alloc_block(dev_priv->pci_heap, size, \
						alignment, file_priv, tail); \
                if ( block ) goto alloc_ok;\
	        }

#define NOUVEAU_MEM_ALLOC_FB {\
                type=NOUVEAU_MEM_FB;\
                if (!(flags&NOUVEAU_MEM_MAPPED)) {\
                        block = nouveau_mem_alloc_block(dev_priv->fb_nomap_heap,\
                                                        size, alignment, \
							file_priv, tail); \
                        if (block) goto alloc_ok;\
                }\
                block = nouveau_mem_alloc_block(dev_priv->fb_heap, size,\
                                                alignment, file_priv, tail);\
                if (block) goto alloc_ok;\
	        }


	if (flags&NOUVEAU_MEM_FB) NOUVEAU_MEM_ALLOC_FB
	if (flags&NOUVEAU_MEM_AGP) NOUVEAU_MEM_ALLOC_AGP
	if (flags&NOUVEAU_MEM_PCI) NOUVEAU_MEM_ALLOC_PCI
	if (flags&NOUVEAU_MEM_FB_ACCEPTABLE) NOUVEAU_MEM_ALLOC_FB
	if (flags&NOUVEAU_MEM_AGP_ACCEPTABLE) NOUVEAU_MEM_ALLOC_AGP
	if (flags&NOUVEAU_MEM_PCI_ACCEPTABLE) NOUVEAU_MEM_ALLOC_PCI


	return NULL;

alloc_ok:
	block->flags=type;

	/* On G8x, map memory into VM */
	if (block->flags & NOUVEAU_MEM_FB && dev_priv->card_type >= NV_50 &&
	    !(flags & NOUVEAU_MEM_NOVM)) {
		unsigned offset = block->start - dev_priv->vm_vram_base;
		unsigned tile = 0;

		/* The tiling stuff is *not* what NVIDIA does - but both the
		 * 2D and 3D engines seem happy with this simpler method.
		 * Should look into why NVIDIA do what they do at some point.
		 */
		if (flags & NOUVEAU_MEM_TILE) {
			if (flags & NOUVEAU_MEM_TILE_ZETA)
				tile = 0x00002800;
			else
				tile = 0x00007000;
		}

		ret = nv50_mem_vm_bind_linear(dev, block->start, block->size,
					      tile, offset);
		if (ret) {
			DRM_ERROR("error binding into vm: %d\n", ret);
			nouveau_mem_free_block(block);
			return NULL;
		}
	} else
	if (block->flags & NOUVEAU_MEM_FB && dev_priv->card_type >= NV_50) {
		block->start -= dev_priv->vm_vram_base;
		block->flags |= NOUVEAU_MEM_NOVM;
	}	

	if (flags & NOUVEAU_MEM_MAPPED)
	{
		struct drm_map_list *entry;
		uint64_t map_offset;
		int ret = 0;

		block->flags |= NOUVEAU_MEM_MAPPED;

		map_offset = block->start;
		if (!(block->flags & NOUVEAU_MEM_NOVM)) {
			if (block->flags & NOUVEAU_MEM_FB)
				map_offset -= dev_priv->vm_vram_base;
			else
			if (block->flags & NOUVEAU_MEM_AGP)
				map_offset -= dev_priv->vm_gart_base;
		}

		if (type == NOUVEAU_MEM_AGP) {
			if (dev_priv->gart_info.type != NOUVEAU_GART_SGDMA)
			ret = drm_addmap(dev, map_offset, block->size,
					 _DRM_AGP, 0, &block->map);
			else
			ret = drm_addmap(dev, map_offset, block->size,
					 _DRM_SCATTER_GATHER, 0, &block->map);
		}
		else if (type == NOUVEAU_MEM_FB)
			ret = drm_addmap(dev, map_offset + dev_priv->fb_phys,
					 block->size, _DRM_FRAME_BUFFER,
					 0, &block->map);
		else if (type == NOUVEAU_MEM_PCI)
			ret = drm_addmap(dev, map_offset, block->size,
					 _DRM_SCATTER_GATHER, 0, &block->map);

		if (ret) {
			nouveau_mem_free_block(block);
			return NULL;
		}

		entry = drm_find_matching_map(dev, block->map);
		if (!entry) {
			nouveau_mem_free_block(block);
			return NULL;
		}
		block->map_handle = entry->user_token;
	}

	DRM_DEBUG("allocated %lld bytes at 0x%llx type=0x%08x\n", block->size, block->start, block->flags);
	return block;
}

void nouveau_mem_free(struct drm_device* dev, struct mem_block* block)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	DRM_DEBUG("freeing 0x%llx type=0x%08x\n", block->start, block->flags);

	if (dev_priv->mm_enabled) {
		if (block->kmap.virtual)
			drm_bo_kunmap(&block->kmap);
		drm_bo_usage_deref_unlocked(&block->bo);
		drm_free(block, sizeof(*block), DRM_MEM_DRIVER);
		return;
	}

	if (block->flags&NOUVEAU_MEM_MAPPED)
		drm_rmmap(dev, block->map);

	/* G8x: Remove pages from vm */
	if (block->flags & NOUVEAU_MEM_FB && dev_priv->card_type >= NV_50 &&
	    !(block->flags & NOUVEAU_MEM_NOVM)) {
		nv50_mem_vm_unbind(dev, block->start, block->size);
	}

	nouveau_mem_free_block(block);
}

/*
 * Ioctls
 */

int
nouveau_ioctl_mem_alloc(struct drm_device *dev, void *data,
			struct drm_file *file_priv)
{
	struct drm_nouveau_mem_alloc *alloc = data;
	struct mem_block *block;

	NOUVEAU_CHECK_INITIALISED_WITH_RETURN;
	NOUVEAU_CHECK_MM_DISABLED_WITH_RETURN;

	if (alloc->flags & NOUVEAU_MEM_INTERNAL)
		return -EINVAL;

	block = nouveau_mem_alloc(dev, alloc->alignment, alloc->size,
				  alloc->flags | NOUVEAU_MEM_USER, file_priv);
	if (!block)
		return -ENOMEM;
	alloc->map_handle=block->map_handle;
	alloc->offset=block->start;
	alloc->flags=block->flags;

	return 0;
}

int
nouveau_ioctl_mem_free(struct drm_device *dev, void *data,
		       struct drm_file *file_priv)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct drm_nouveau_mem_free *memfree = data;
	struct mem_block *block;

	NOUVEAU_CHECK_INITIALISED_WITH_RETURN;
	NOUVEAU_CHECK_MM_DISABLED_WITH_RETURN;

	block=NULL;
	if (memfree->flags & NOUVEAU_MEM_FB)
		block = find_block(dev_priv->fb_heap, memfree->offset);
	else if (memfree->flags & NOUVEAU_MEM_AGP)
		block = find_block(dev_priv->agp_heap, memfree->offset);
	else if (memfree->flags & NOUVEAU_MEM_PCI)
		block = find_block(dev_priv->pci_heap, memfree->offset);
	if (!block)
		return -EFAULT;
	if (block->file_priv != file_priv)
		return -EPERM;

	nouveau_mem_free(dev, block);
	return 0;
}

int
nouveau_ioctl_mem_tile(struct drm_device *dev, void *data,
		       struct drm_file *file_priv)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct drm_nouveau_mem_tile *memtile = data;
	struct mem_block *block = NULL;
	unsigned offset, tile = 0;
	int ret;

	NOUVEAU_CHECK_INITIALISED_WITH_RETURN;
	NOUVEAU_CHECK_MM_DISABLED_WITH_RETURN;

	if (dev_priv->card_type < NV_50)
		return -EINVAL;
	
	if (memtile->flags & NOUVEAU_MEM_FB)
		block = find_block(dev_priv->fb_heap, memtile->offset);

	if (!block || (memtile->delta + memtile->size > block->size))
		return -EINVAL;

	if (block->file_priv != file_priv)
		return -EPERM;

	offset  = block->start + memtile->delta;	
	offset -= dev_priv->vm_vram_base;

	if (memtile->flags & NOUVEAU_MEM_TILE) {
		if (memtile->flags & NOUVEAU_MEM_TILE_ZETA)
			tile = 0x00002800;
		else
			tile = 0x00007000;
	}

	ret = nv50_mem_vm_bind_linear(dev, block->start + memtile->delta,
				      memtile->size, tile, offset);
	if (ret)
		return ret;

	return 0;
}

