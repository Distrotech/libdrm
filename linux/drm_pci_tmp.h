/**
 * \file drm_pci_tmp.h
 * ioctl's to manage PCI memory
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


#define __NO_VERSION__
#include <linux/pci.h>
#include "drmP.h"


/**********************************************************************/
/** \name PCI memory */
/*@{*/

/**
 * Allocate a PCI consistent memory block, for DMA.
 */
void * DRM(pci_alloc)( drm_device_t *dev, size_t size, 
		       dma_addr_t *busaddr )
{
	return pci_alloc_consistent( dev->pdev, size, busaddr );
}

/**
 * Free a PCI consistent memory block.
 */
void DRM(pci_free)( drm_device_t *dev, size_t size, void *cpuaddr, 
		    dma_addr_t busaddr )
{
	DRM_DEBUG( "%s\n", __FUNCTION__ );

	pci_free_consistent( dev->pdev, size, cpuaddr, busaddr );
}

/*@}*/


/**********************************************************************/
/** \name PCI memory pool */
/*@{*/

/**
 * Create a pool of PCI consistent memory blocks, for DMA.
 *
 * \return a handle on success, or NULL on failure.
 * 
 * \note This is a minimalistic wrapper around Linux's pci_pool_create()
 * function to ease porting to other OS's. More functionality can be exposed
 * later if actually required by the drivers.
 */
void *DRM(pci_pool_create)( drm_device_t *dev, size_t size, size_t align )
{
	DRM_DEBUG( "%s\n", __FUNCTION__ );

	return pci_pool_create( "DRM", dev->pdev, size, align, 0, 0);
}

/**
 * Destroy a pool of PCI consistent memory blocks.
 */
void DRM(pci_pool_destroy)( drm_device_t *dev, void *entry )
{
	DRM_DEBUG( "%s\n", __FUNCTION__ );

	pci_pool_destroy( (struct pci_pool *)entry );
}

/**
 * Allocate a block from a PCI consistent memory block pool.
 *
 * \return the virtual address of a block on success, or NULL on failure. 
 */
void *DRM(pci_pool_alloc)( void *entry, dma_addr_t *busaddr )
{
	DRM_DEBUG( "%s\n", __FUNCTION__ );

	return pci_pool_alloc( (struct pci_pool *)entry, 0, busaddr );
}

/**
 * Free a block back into a PCI consistent memory block pool.
 */
void DRM(pci_pool_free)( void *entry, void *cpuaddr, dma_addr_t busaddr )
{
	DRM_DEBUG( "%s\n", __FUNCTION__ );

	pci_pool_free( (struct pci_pool *)entry, cpuaddr, busaddr );
}

/*@}*/


#if 0

/**********************************************************************/
/** \name Ioctl's */
/*@{*/

int DRM(pci_alloc_ioctl)( struct inode *inode, struct file *filp,
			  unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_pci_mem_ioctl_t request;
	drm_pci_mem_t *entry;

	DRM_DEBUG( "%s\n", __FUNCTION__ );

	if ( copy_from_user( &request,
			     (drm_pci_mem_ioctl_t *)arg,
			     sizeof(request) ) )
		return -EFAULT;

	entry = DRM(alloc)( sizeof(*entry), DRM_MEM_DRIVER );
	if ( !entry )
		return -ENOMEM;

   	memset( entry, 0, sizeof(*entry) );

	entry->cpuaddr = DRM(pci_alloc)( dev, request.size, 
					 &entry->busaddr );
	
	if ( !entry->cpuaddr ) {
		DRM(free)( entry, sizeof(*entry), DRM_MEM_DRIVER );
		return -ENOMEM;
	}

	entry->size = request.size;
	request.busaddr = entry->busaddr;

	if ( copy_to_user( (drm_pci_mem_ioctl_t *)arg,
			   &request,
			   sizeof(request) ) ) {
		DRM(pci_free)( dev, entry->size, entry->cpuaddr, 
			       entry->busaddr );
		DRM(free)( entry, sizeof(*entry), DRM_MEM_DRIVER );
		return -EFAULT;
	}

	list_add( (struct list_head *)entry, &dev->pci_mem );
	
	return 0;
}

int DRM(pci_free_ioctl)( struct inode *inode, struct file *filp,
			  unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_pci_mem_ioctl_t request;
	struct list_head *ptr;
	drm_pci_mem_t *entry;

	DRM_DEBUG( "%s\n", __FUNCTION__ );

	if ( copy_from_user( &request,
			     (drm_pci_mem_ioctl_t *)arg,
			     sizeof(request) ) )
		return -EFAULT;

	list_for_each(ptr, &dev->pci_mem)
	{
		entry = list_entry(ptr, drm_pci_mem_t, list);
		if( entry->busaddr == request.busaddr ) {
			list_del(ptr);
			DRM(pci_free)( dev, entry->size, entry->cpuaddr, 
				       entry->busaddr );
			DRM(free)( entry, sizeof(*entry), DRM_MEM_DRIVER );
			return 0;
		}
	}

	return -EFAULT;
}
/*@}*/


/**********************************************************************/
/** \name Cleanup */
/*@{*/

/** 
 * Called on driver exit to free the PCI memory allocated by userspace.
 */
void DRM(pci_cleanup)( drm_device_t *dev )
{
	struct list_head *ptr, *tmp;
	drm_pci_mem_t *entry;

	DRM_DEBUG( "%s\n", __FUNCTION__ );

	list_for_each_safe(ptr, tmp, &dev->pci_mem)
	{
		entry = list_entry(ptr, drm_pci_mem_t, list);
		list_del(ptr);
		DRM(pci_free)( dev, entry->size, entry->cpuaddr, 
			       entry->busaddr );
		DRM(free)( entry, sizeof(*entry), DRM_MEM_DRIVER );
	}
}

/*@}*/

#endif
