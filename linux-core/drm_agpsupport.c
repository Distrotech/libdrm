/* drm_agpsupport.h -- DRM support for AGP/GART backend -*- linux-c -*-
 * Created: Mon Dec 13 09:56:45 1999 by faith@precisioninsight.com
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Author:
 *    Rickard E. (Rik) Faith <faith@valinux.com>
 *    Gareth Hughes <gareth@valinux.com>
 */

#define __NO_VERSION__
/* Cheat, check for an agp 3.0 include symbol after including agp_backend.h
 * if its not there, define what we need.
 */
#include "agp_30_symbols.h"
#include "drmP.h"

#include <linux/module.h>

#if __REALLY_HAVE_AGP

#define DRM_AGP_GET (drm_agp_t *)inter_module_get("drm_agp")
#define DRM_AGP_PUT inter_module_put("drm_agp")
#define DRM_AGP_GET_3_0 (drm_agp_3_0_t *)inter_module_get("drm_agp_3_0")
#define DRM_AGP_PUT_3_0 do { inter_module_put("drm_agp_3_0"); drm_agp_3_0 = NULL; } while(0)

static const drm_agp_t *drm_agp = NULL;
static const drm_agp_3_0_t *drm_agp_3_0 = NULL;

/* Jeff Hartmann - Rearranged slightly to make supporting the agp 3.0 
 * infrastructure a little easier.  The kernel functions are provided
 * at the top of the file, while the ioctls are at the bottom.
 */
int DRM(agp_supports_vma_map)(void)
{
	if(drm_agp_3_0 && drm_agp_3_0->vma_map_memory) return 1;
	return 0;
}

int DRM(agp_usermap)(drm_device_t *dev, drm_map_t *map, 
		     struct vm_area_struct *vma)
{
	drm_agp_mem_t *entry = (drm_agp_mem_t *)map->handle;

	return drm_agp_3_0->vma_map_memory(dev->agp->context,
					   vma,
					   entry->memory,
					   0);
}

static int DRM(agp_has_copy)(void)
{
	printk("%s\n", __func__);
	if(drm_agp && drm_agp->copy_info) return 1;
	if(drm_agp_3_0 && drm_agp_3_0->copy_info) return 1;
	return 0;
}

static int DRM(agp_has_enable)(void)
{
	printk("%s\n", __func__);
	if(drm_agp && drm_agp->enable) return 1;
	if(drm_agp_3_0 && drm_agp_3_0->enable) return 1;
	return 0;
}

static int DRM(agp_has_acquire)(void)
{
	printk("%s\n", __func__);
	if(drm_agp && drm_agp->acquire) return 1;
	if(drm_agp_3_0 && drm_agp_3_0->acquire) return 1;
	return 0;
}

static int DRM(agp_has_release)(void)
{
	printk("%s\n", __func__);
	if(drm_agp && drm_agp->release) return 1;
	if(drm_agp_3_0 && drm_agp_3_0->release) return 1;
	return 0;
}

static void DRM(agp_call_release)(drm_device_t *dev)
{
	printk("%s\n", __func__);
	if(drm_agp) drm_agp->release();
	else if (drm_agp_3_0) drm_agp_3_0->release(dev->agp->context);
}

static void DRM(agp_call_enable)(drm_device_t *dev, drm_agp_mode_t *mode)
{
	printk("%s\n", __func__);
	if(drm_agp) drm_agp->enable(mode->mode);
	else if(drm_agp_3_0) drm_agp_3_0->enable(dev->agp->context, mode->mode);
}

static int DRM(agp_call_acquire)(drm_device_t *dev)
{
	printk("%s\n", __func__);
	if(drm_agp) return drm_agp->acquire();
	else if(drm_agp_3_0) return drm_agp_3_0->acquire(dev->agp->context);
	return -EINVAL;
}

/* Revisit when we support more then one agp context. */
void DRM(agp_do_release)(void)
{
	printk("%s\n", __func__);
	if(drm_agp && drm_agp->release) drm_agp->release();
	else if(drm_agp_3_0 && drm_agp_3_0->release) drm_agp_3_0->release(0);
}

agp_memory *DRM(agp_allocate_memory)(drm_device_t *dev, size_t pages, u32 type)
{
	printk("%s 0\n", __func__);
	if(drm_agp) {
		printk("%s 0\n", __func__);
		if (!drm_agp->allocate_memory) return NULL;
		printk("%s 0\n", __func__);
		return drm_agp->allocate_memory(pages, type);
	} else if (drm_agp_3_0) {
		agp_memory *mem;

		printk("%s 1\n", __func__);
		if (!drm_agp_3_0->allocate_memory) return NULL;
		printk("%s 2\n", __func__);
		mem = drm_agp_3_0->allocate_memory(dev->agp->context,
						   pages, type);
		printk("drm_agp_3_0 : %p, mem : %p\n", drm_agp_3_0, mem);
		return mem;
	}
	return NULL;
}

int DRM(agp_free_memory)(drm_device_t *dev, agp_memory *handle)
{
	printk("%s\n", __func__);
	if(drm_agp) {
		if (!handle || !drm_agp->free_memory) return 0;
		drm_agp->free_memory(handle);
	} else if (drm_agp_3_0) {
		if (!handle || !drm_agp_3_0->free_memory) return 0;
		drm_agp_3_0->free_memory(dev->agp->context, handle);
	} else {
		return 0;
	}
	return 1;
}

int DRM(agp_bind_memory)(drm_device_t *dev, agp_memory *handle, off_t start)
{
	printk("%s\n", __func__);
	if(drm_agp) {
		if (!handle || !drm_agp->bind_memory) return -EINVAL;
		return drm_agp->bind_memory(handle, start);
	} else if(drm_agp_3_0) {
		if (!handle || !drm_agp_3_0->bind_memory) return -EINVAL;
		return drm_agp_3_0->bind_memory(dev->agp->context, handle, start);
	}
	return -EINVAL;
}

int DRM(agp_unbind_memory)(drm_device_t *dev, agp_memory *handle)
{
	printk("%s\n", __func__);
	if(drm_agp) {
		if (!handle || !drm_agp->unbind_memory) return -EINVAL;
		return drm_agp->unbind_memory(handle);
	} else if(drm_agp_3_0) {
		if (!handle || !drm_agp_3_0->unbind_memory) return -EINVAL;
		return drm_agp_3_0->unbind_memory(dev->agp->context, handle);
	}
	return -EINVAL;
}

drm_agp_mem_t *DRM(agp_lookup_entry)(drm_device_t *dev,
				     unsigned long handle)
{
	drm_agp_mem_t *entry;

	printk("%s\n", __func__);

	if(!dev->agp) return NULL;
	for(entry = dev->agp->memory; entry; entry = entry->next) {
		if (entry->handle == handle) return entry;
	}
	return NULL;
}

static drm_agp_head_t *DRM(agp_init_old)(void)
{
	drm_agp_head_t *head         = NULL;

	drm_agp = DRM_AGP_GET;
	if (drm_agp) {
		if (!(head = DRM(alloc)(sizeof(*head), DRM_MEM_AGPLISTS)))
			return NULL;
		memset((void *)head, 0, sizeof(*head));
		drm_agp->copy_info(&head->agp_info);
		if (head->agp_info.chipset == NOT_SUPPORTED) {
			DRM(free)(head, sizeof(*head), DRM_MEM_AGPLISTS);
			return NULL;
		}
		head->memory = NULL;
#if LINUX_VERSION_CODE <= 0x020408
		head->cant_use_aperture = 0;
		head->page_mask = ~(0xfff);
#else
		head->cant_use_aperture = head->agp_info.cant_use_aperture;
		head->page_mask = head->agp_info.page_mask;
#endif
		head->context = 0;
		head->agp_extended_info = NULL;
		head->agp_page_shift = 12;

		DRM_DEBUG("AGP %d.%d, aperture @ 0x%08lx %ZuMB\n",
			  head->agp_info.version.major,
			  head->agp_info.version.minor,
			  head->agp_info.aper_base,
			  head->agp_info.aper_size);
	}
	return head;
}
#if __TRY_AGP_3_0 != 0
static drm_agp_head_t *DRM(agp_init_3_0)(void)
{
	drm_agp_head_t *head         = NULL;

	drm_agp_3_0 = DRM_AGP_GET_3_0;
	if (drm_agp_3_0) {
		if (!(head = DRM(alloc)(sizeof(*head), DRM_MEM_AGPLISTS))) {
			DRM_AGP_PUT_3_0;
			return NULL;
		}
		memset((void *)head, 0, sizeof(*head));

		head->agp_extended_info = drm_agp_3_0->get_info_list();
		if(!head->agp_extended_info) {
			DRM(free)(head, sizeof(*head), DRM_MEM_AGPLISTS);
			DRM_AGP_PUT_3_0;
			return NULL;
		}
		head->num_ctxs = drm_agp_3_0->get_num_contexts();
		drm_agp_3_0->copy_info(0, &head->agp_info);
		if (head->agp_info.chipset == NOT_SUPPORTED) {
			DRM(free)(head, sizeof(*head), DRM_MEM_AGPLISTS);
			DRM_AGP_PUT_3_0;
			return NULL;
		}
		head->memory = NULL;
#if LINUX_VERSION_CODE <= 0x020408
		head->cant_use_aperture = 0;
		head->page_mask = ~(0xfff);
#else
		head->cant_use_aperture = head->agp_info.cant_use_aperture;
		head->page_mask = head->agp_info.page_mask;
#endif
		/* Not completely kosher. */
		head->agp_page_shift = 12;
		head->context = 0;
		DRM_DEBUG("AGP 3.0 - %d.%d, aperture @ 0x%08lx %ZuMB\n",
			  head->agp_info.version.major,
			  head->agp_info.version.minor,
			  head->agp_info.aper_base,
			  head->agp_info.aper_size);
	}
	return head;
}
#endif

#if __TRY_AGP_3_0 == 0
drm_agp_head_t *DRM(agp_init)(void)
{
	return DRM(agp_init_old)();
}
#else
drm_agp_head_t *DRM(agp_init)(void)
{
	drm_agp_head_t *head;
	
	head = DRM(agp_init_3_0)();
	if(!head) head = DRM(agp_init_old)();
	return head;
}
#endif

void DRM(agp_uninit)(void)
{
	if(drm_agp) {
		DRM_AGP_PUT;
		drm_agp = NULL;
	}
	if(drm_agp_3_0) {
		DRM_AGP_PUT_3_0;
		drm_agp_3_0 = NULL;
	}
}

/* Ioctls are provided below */
int DRM(agp_getmap)(struct inode *inode, struct file *filp,
		    unsigned int cmd, unsigned long arg)
{
	drm_file_t	      *priv	 = filp->private_data;
	drm_device_t	      *dev	 = priv->dev;
	drm_agp_buffer_info_t info;
	drm_agp_mem_t	      *mem;
	agp_memory	      *agp_mem = NULL;
	int ret = 0;

	if(!drm_agp_3_0) return -EINVAL;

	if (copy_from_user(&info, (drm_agp_buffer_info_t *)arg, sizeof(info)))
		return -EFAULT;

	mem = DRM(agp_lookup_entry)(dev, info.handle);
	if(mem) {
		agp_mem = mem->memory;
	} else if(drm_agp_3_0->get_map) {
		ret = drm_agp_3_0->get_map(dev->agp->context, 
					   (int)info.handle,
					   &agp_mem);
		if(ret) return ret;
	}
	if(!agp_mem) return -EINVAL;
	info.size = agp_mem->page_count << dev->agp->agp_page_shift;
	info.type = (unsigned long)agp_mem->type;
	info.physical = (unsigned long)agp_mem->physical;
	info.offset = (unsigned long)agp_mem->pg_start << 
			dev->agp->agp_page_shift;

	if (copy_to_user((drm_agp_buffer_info_t *)arg, &info, sizeof(info)))
		return -EFAULT;
	return 0;
}

static void DRM(agp_fill_master)(drm_agp_master_t *master, 
				 agp_info_master *info)
{
	master->agp_major_version = info->agp_major_version;
	master->agp_minor_version = info->agp_minor_version;
	master->vendor_pci_id = info->device->vendor;
	master->device_pci_id = info->device->device;
	master->num_requests_enqueue = info->num_requests_enqueue;
	master->calibration_cycle_ms = info->calibration_cycle_ms;
	master->max_bandwidth_bpp = info->max_bandwidth_bpp;
	master->num_trans_per_period = info->num_trans_per_period;
	master->max_requests = info->max_requests;
	master->payload_size = info->payload_size;
	master->flags = info->flags;
}

static void DRM(agp_fill_driver)(drm_agp_driver_info_t *driver, 
				 agp_extended_info *info)
{
	driver->driver_flags = info->driver_flags;
	driver->agp_major_version = info->bridge->agp_major_version;
	driver->agp_minor_version = info->bridge->agp_minor_version;
	driver->num_requests_enqueue = info->bridge->num_requests_enqueue;
	driver->calibration_cycle_ms = info->bridge->calibration_cycle_ms;
	driver->optimum_request_size = info->bridge->optimum_request_size;
	driver->max_bandwidth_bpp = info->bridge->max_bandwidth_bpp;
	driver->iso_latency_in_periods = info->bridge->iso_latency_in_periods;
	driver->num_trans_per_period = info->bridge->num_trans_per_period;
	driver->payload_size = info->bridge->payload_size;
	driver->target_device_pci_id = info->bridge->device->device;
	driver->target_vendor_pci_id = info->bridge->device->vendor;
	driver->target_flags = info->bridge->flags;
	driver->aper_base = info->aper_base;
	driver->aper_size = info->aper_size;
	driver->agp_page_shift = info->agp_page_shift;
	driver->agp_page_mask = info->agp_page_mask;
	driver->alloc_page_shift = info->alloc_page_shift;
	driver->alloc_page_mask = info->alloc_page_mask;
	driver->max_system_pages = info->max_system_pages;
	driver->current_memory = info->current_memory;
	driver->num_masters = info->num_masters;
	driver->context_id = info->agp_ctxt_idx;
}

int DRM(agp_e_info)(struct inode *inode, struct file *filp,
		    unsigned int cmd, unsigned long arg)
{
	drm_file_t	  *priv	 = filp->private_data;
	drm_device_t	  *dev	 = priv->dev;
	agp_extended_info *info	 = NULL;
	struct list_head  *ptr;
	int		  length = 1;
	drm_agp_driver_info_t	driver;
	drm_agp_master_t	master;
	char *empty = "\0";
	char *pos = (char *)arg;

	if(!drm_agp_3_0) return -EINVAL;

	list_for_each(ptr, dev->agp->agp_extended_info) {
		info = list_entry(ptr, agp_extended_info, info_list);
		if(info->agp_ctxt_idx == dev->agp->context) break;
		info = NULL;
	}
	if(!info) return -EINVAL;
	if(info->driver_name) {
		length = strlen(info->driver_name) + 1;
	} else {
		info->driver_name = empty;
	}
	DRM(agp_fill_driver)(&driver, info);
	pos += sizeof(driver);
	driver.driver_name = pos;
	if(info->num_masters) {
		driver.masters = (drm_agp_master_t *)(pos + length);
	} else {
		driver.masters = NULL;
	}

	if(copy_to_user((void *)arg, &driver, sizeof(driver))) {
		if(info->driver_name == empty) info->driver_name = NULL;
		return -EFAULT;
	}
	if(copy_to_user((void *)pos, info->driver_name, length)) {
		if(info->driver_name == empty) info->driver_name = NULL;
		return -EFAULT;
	}
	pos += length;
	list_for_each(ptr, &info->master_list) {
		agp_info_master *minfo = list_entry(ptr,
						    agp_info_master,
						    masters);
		DRM(agp_fill_master)(&master, minfo);
		if(copy_to_user((void *)pos, 
				&master, sizeof(master))) {
			if(info->driver_name == empty) 
				info->driver_name = NULL;
			return -EFAULT;
		}
		pos += sizeof(master);
	}

	if(info->driver_name == empty) info->driver_name = NULL;
	return 0;	
}

int DRM(agp_e_size)(struct inode *inode, struct file *filp,
		    unsigned int cmd, unsigned long arg)
{
	drm_file_t	 *priv	 = filp->private_data;
	drm_device_t	 *dev	 = priv->dev;
	agp_extended_info *info = NULL;
	struct list_head *ptr;
	int size = 0;

	if(!drm_agp_3_0) return -EINVAL;

	list_for_each(ptr, dev->agp->agp_extended_info) {
		info = list_entry(ptr, agp_extended_info, info_list);
		if(info->agp_ctxt_idx == dev->agp->context) break;
		info = NULL;
	}

	if(!info) return -EINVAL;
	size = sizeof(drm_agp_driver_info_t);
	size += sizeof(drm_agp_master_t) * info->num_masters;
	if(info->driver_name) size += strlen(info->driver_name) + 1;
	else size += 1;

	return size;	  
}

int DRM(agp_no_ctxs)(struct inode *inode, struct file *filp,
		     unsigned int cmd, unsigned long arg)
{
	drm_file_t	 *priv	 = filp->private_data;
	drm_device_t	 *dev	 = priv->dev;

	if(!drm_agp_3_0) return -EINVAL;
	return dev->agp->num_ctxs;
}

/* Really a no-op for now. */
int DRM(agp_chg_ctx)(struct inode *inode, struct file *filp,
		     unsigned int cmd, unsigned long arg)
{
	drm_file_t	 *priv	 = filp->private_data;
	drm_device_t	 *dev	 = priv->dev;

	if(!drm_agp_3_0 || arg >= dev->agp->num_ctxs ||
	   arg < 0) return -EINVAL;

	return 0;
}

int DRM(agp_info)(struct inode *inode, struct file *filp,
		  unsigned int cmd, unsigned long arg)
{
	drm_file_t	 *priv	 = filp->private_data;
	drm_device_t	 *dev	 = priv->dev;
	agp_kern_info    *kern;
	drm_agp_info_t   info;

	if (!dev->agp || !dev->agp->acquired || !DRM(agp_has_copy)())
		return -EINVAL;

	kern                   = &dev->agp->agp_info;
	info.agp_version_major = kern->version.major;
	info.agp_version_minor = kern->version.minor;
	info.mode              = kern->mode;
	info.aperture_base     = kern->aper_base;
	info.aperture_size     = kern->aper_size * 1024 * 1024;
	info.memory_allowed    = kern->max_memory << PAGE_SHIFT;
	info.memory_used       = kern->current_memory << PAGE_SHIFT;
	info.id_vendor         = kern->device->vendor;
	info.id_device         = kern->device->device;

	if (copy_to_user((drm_agp_info_t *)arg, &info, sizeof(info)))
		return -EFAULT;
	return 0;
}

int DRM(agp_acquire)(struct inode *inode, struct file *filp,
		     unsigned int cmd, unsigned long arg)
{
	drm_file_t	 *priv	 = filp->private_data;
	drm_device_t	 *dev	 = priv->dev;
	int              retcode;

	if (!dev->agp || dev->agp->acquired || !DRM(agp_has_acquire)())
		return -EINVAL;
	if ((retcode = DRM(agp_call_acquire)(dev))) return retcode;
	dev->agp->acquired = 1;
	return 0;
}

int DRM(agp_release)(struct inode *inode, struct file *filp,
		     unsigned int cmd, unsigned long arg)
{
	drm_file_t	 *priv	 = filp->private_data;
	drm_device_t	 *dev	 = priv->dev;

	if (!dev->agp || !dev->agp->acquired || !DRM(agp_has_release)())
		return -EINVAL;
	DRM(agp_call_release)(dev);
	dev->agp->acquired = 0;
	return 0;

}

int DRM(agp_enable)(struct inode *inode, struct file *filp,
		    unsigned int cmd, unsigned long arg)
{
	drm_file_t	 *priv	 = filp->private_data;
	drm_device_t	 *dev	 = priv->dev;
	drm_agp_mode_t   mode;

	if (!dev->agp || !dev->agp->acquired || !DRM(agp_has_enable)())
		return -EINVAL;

	if (copy_from_user(&mode, (drm_agp_mode_t *)arg, sizeof(mode)))
		return -EFAULT;

	dev->agp->mode    = mode.mode;
	DRM(agp_call_enable)(dev, &mode);
	dev->agp->base    = dev->agp->agp_info.aper_base;
	dev->agp->enabled = 1;
	return 0;
}

int DRM(agp_alloc)(struct inode *inode, struct file *filp,
		   unsigned int cmd, unsigned long arg)
{
	drm_file_t	 *priv	 = filp->private_data;
	drm_device_t	 *dev	 = priv->dev;
	drm_agp_buffer_t request;
	drm_agp_mem_t    *entry;
	agp_memory       *memory;
	unsigned long    pages;
	u32 		 type;

	if (!dev->agp || !dev->agp->acquired) return -EINVAL;
	if (copy_from_user(&request, (drm_agp_buffer_t *)arg, sizeof(request)))
		return -EFAULT;
	if (!(entry = DRM(alloc)(sizeof(*entry), DRM_MEM_AGPLISTS)))
		return -ENOMEM;

   	memset(entry, 0, sizeof(*entry));

	pages = (request.size + PAGE_SIZE - 1) / PAGE_SIZE;
	type = (u32) request.type;

	if (!(memory = DRM(alloc_agp)(dev, pages, type))) {
		DRM(free)(entry, sizeof(*entry), DRM_MEM_AGPLISTS);
		return -ENOMEM;
	}

	entry->handle    = (unsigned long)memory->memory;
	entry->memory    = memory;
	entry->bound     = 0;
	entry->pages     = pages;
	entry->prev      = NULL;
	entry->next      = dev->agp->memory;
	if (dev->agp->memory) dev->agp->memory->prev = entry;
	dev->agp->memory = entry;

	request.handle   = entry->handle;
        request.physical = memory->physical;

	if (copy_to_user((drm_agp_buffer_t *)arg, &request, sizeof(request))) {
		dev->agp->memory       = entry->next;
		dev->agp->memory->prev = NULL;
		DRM(free_agp)(dev, memory, pages);
		DRM(free)(entry, sizeof(*entry), DRM_MEM_AGPLISTS);
		return -EFAULT;
	}
	return 0;
}

int DRM(agp_unbind)(struct inode *inode, struct file *filp,
		    unsigned int cmd, unsigned long arg)
{
	drm_file_t	  *priv	 = filp->private_data;
	drm_device_t	  *dev	 = priv->dev;
	drm_agp_binding_t request;
	drm_agp_mem_t     *entry;

	if (!dev->agp || !dev->agp->acquired) return -EINVAL;
	if (copy_from_user(&request, (drm_agp_binding_t *)arg, sizeof(request)))
		return -EFAULT;
	if (!(entry = DRM(agp_lookup_entry)(dev, request.handle)))
		return -EINVAL;
	if (!entry->bound) return -EINVAL;
	return DRM(unbind_agp)(dev, entry->memory);
}

int DRM(agp_bind)(struct inode *inode, struct file *filp,
		  unsigned int cmd, unsigned long arg)
{
	drm_file_t	  *priv	 = filp->private_data;
	drm_device_t	  *dev	 = priv->dev;
	drm_agp_binding_t request;
	drm_agp_mem_t     *entry;
	int               retcode;
	int               page;

	if (!dev->agp || !dev->agp->acquired || !drm_agp->bind_memory)
		return -EINVAL;
	if (copy_from_user(&request, (drm_agp_binding_t *)arg, sizeof(request)))
		return -EFAULT;
	if (!(entry = DRM(agp_lookup_entry)(dev, request.handle)))
		return -EINVAL;
	if (entry->bound) return -EINVAL;
	page = (request.offset + PAGE_SIZE - 1) / PAGE_SIZE;
	if ((retcode = DRM(bind_agp)(dev, entry->memory, page))) return retcode;
	entry->bound = dev->agp->base + (page << PAGE_SHIFT);
	DRM_DEBUG("base = 0x%lx entry->bound = 0x%lx\n",
		  dev->agp->base, entry->bound);
	return 0;
}

int DRM(agp_free)(struct inode *inode, struct file *filp,
		  unsigned int cmd, unsigned long arg)
{
	drm_file_t	 *priv	 = filp->private_data;
	drm_device_t	 *dev	 = priv->dev;
	drm_agp_buffer_t request;
	drm_agp_mem_t    *entry;

	if (!dev->agp || !dev->agp->acquired) return -EINVAL;
	if (copy_from_user(&request, (drm_agp_buffer_t *)arg, sizeof(request)))
		return -EFAULT;
	if (!(entry = DRM(agp_lookup_entry)(dev, request.handle)))
		return -EINVAL;
	if (entry->bound) DRM(unbind_agp)(dev, entry->memory);

	if (entry->prev) entry->prev->next = entry->next;
	else             dev->agp->memory  = entry->next;
	if (entry->next) entry->next->prev = entry->prev;
	DRM(free_agp)(dev, entry->memory, entry->pages);
	DRM(free)(entry, sizeof(*entry), DRM_MEM_AGPLISTS);
	return 0;
}

#endif /* __REALLY_HAVE_AGP */
