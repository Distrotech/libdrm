/**
 * \file drm_pci.c
 * PCI consistent memory allocation.
 * 
 * \warning These interfaces aren't stable yet.
 * 
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
void * drm_pci_alloc( drm_device_t *dev, size_t size, 
		       dma_addr_t *busaddr )
{
	return pci_alloc_consistent( dev->pdev, size, busaddr );
}

/**
 * Free a PCI consistent memory block.
 */
void drm_pci_free( drm_device_t *dev, size_t size, void *cpuaddr, 
		    dma_addr_t busaddr )
{
	DRM_DEBUG( "%s\n", __FUNCTION__ );

	pci_free_consistent( dev->pdev, size, cpuaddr, busaddr );
}

/*@}*/


/**********************************************************************/
/** 
 * \name PCI memory pool 
 *
 * These are low level abstractions of the OS primitives.  Don't call these
 * directly from the drivers.  Use drm_pool_pci_alloc() and drm_pool_free()
 * instead.
 */
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
void *drm_pci_pool_create( drm_device_t *dev, size_t size, size_t align )
{
	return pci_pool_create( "DRM", dev->pdev, size, align, 0, 0);
}

/**
 * Destroy a pool of PCI consistent memory blocks.
 */
void drm_pci_pool_destroy( drm_device_t *dev, void *entry )
{
	pci_pool_destroy( (struct pci_pool *)entry );
}

/**
 * Allocate a block from a PCI consistent memory block pool.
 *
 * \return the virtual address of a block on success, or NULL on failure. 
 */
void *drm_pci_pool_alloc( void *entry, dma_addr_t *busaddr )
{
	return pci_pool_alloc( (struct pci_pool *)entry, 0, busaddr );
}

/**
 * Free a block back into a PCI consistent memory block pool.
 */
void drm_pci_pool_free( void *entry, void *cpuaddr, dma_addr_t busaddr )
{
	pci_pool_free( (struct pci_pool *)entry, cpuaddr, busaddr );
}

/*@}*/


#if 0

/**********************************************************************/
/** \name Ioctl's */
/*@{*/

int drm_pci_alloc_ioctl( struct inode *inode, struct file *filp,
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

	entry = drm_alloc( sizeof(*entry), DRM_MEM_DRIVER );
	if ( !entry )
		return -ENOMEM;

   	memset( entry, 0, sizeof(*entry) );

	entry->cpuaddr = drm_pci_alloc( dev, request.size, 
					 &entry->busaddr );
	
	if ( !entry->cpuaddr ) {
		drm_free( entry );
		return -ENOMEM;
	}

	entry->size = request.size;
	request.busaddr = entry->busaddr;

	if ( copy_to_user( (drm_pci_mem_ioctl_t *)arg,
			   &request,
			   sizeof(request) ) ) {
		drm_pci_free( dev, entry->size, entry->cpuaddr, 
			       entry->busaddr );
		drm_free( entry );
		return -EFAULT;
	}

	list_add( (struct list_head *)entry, &dev->pci_mem );
	
	return 0;
}

int drm_pci_free_ioctl( struct inode *inode, struct file *filp,
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
			drm_pci_free( dev, entry->size, entry->cpuaddr, 
				       entry->busaddr );
			drm_free( entry );
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
void drm_pci_cleanup( drm_device_t *dev )
{
	struct list_head *ptr, *tmp;
	drm_pci_mem_t *entry;

	DRM_DEBUG( "%s\n", __FUNCTION__ );

	list_for_each_safe(ptr, tmp, &dev->pci_mem)
	{
		entry = list_entry(ptr, drm_pci_mem_t, list);
		list_del(ptr);
		drm_pci_free( dev, entry->size, entry->cpuaddr, 
			       entry->busaddr );
		drm_free( entry );
	}
}

/*@}*/

#endif
