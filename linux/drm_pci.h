/**
 * \file drm_pci.h
 * \brief ioctl's to manage PCI memory
 * 
 * \warning These interfaces aren't stable yet.
 * 
 * \todo Implement the remaining ioctl's for the PCI pools.
 * \todo Add support to map these buffers.
 * \todo The wrappers here are so thin that they would be better off inlined..
 *
 * \author José Fonseca <jrfonseca@tungstengraphics.com>
 */

/*
 * Copyright 2003 José Fonseca.
 * All Rights Reserved.
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */



#ifndef _DRM_PCI_H_
#define _DRM_PCI_H_


/**
 * \brief PCI consistent memory block, for DMA.
 */
typedef struct drm_pci_mem {
	struct list_head	list;		/**< \brief Linux list */
	size_t			size;		/**< \brief size */
	void			*cpuaddr;	/**< \brief kernel virtual address */
	dma_addr_t		busaddr;	/**< \brief associated bus address */
} drm_pci_mem_t;


#if 0
/**
 * \brief PCI data
 */
typedef struct drm_pci_data {
	struct list_head  pci_mem;	/**< \brief PCI consistent memory */
} drm_pci_data_t;
#endif


/** \name Prototypes */
/*@{*/

extern void *DRM(pci_alloc)(drm_device_t *dev, size_t size, dma_addr_t *busaddr);
extern void DRM(pci_free)(drm_device_t *dev, size_t size, void *cpuaddr, dma_addr_t busaddr);
extern void *DRM(pci_pool_create)(drm_device_t *dev, size_t size, size_t align);
extern void DRM(pci_pool_destroy)(drm_device_t *dev, void *entry);
extern void *DRM(pci_pool_alloc)(void *entry, dma_addr_t *busaddr);
extern void DRM(pci_pool_free)(void *entry, void *cpuaddr, dma_addr_t busaddr);
extern int DRM(pci_alloc_ioctl)(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg);
extern int DRM(pci_free_ioctl)(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg);
extern void DRM(pci_cleanup)(drm_device_t *dev);

/*@}*/


#endif /* !_DRM_VM_H_ */
