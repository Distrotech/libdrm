/**
 * \file drm_vm.h
 * Memory mapping for DRM
 * 
 * \author Rickard E. (Rik) Faith <faith@valinux.com>
 * \author Gareth Hughes <gareth@valinux.com>
 */

/*
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
 */

#ifndef _DRM_VM_H_
#define _DRM_VM_H_


/**
 * Mappings list
 */
typedef struct drm_map_list {
	struct list_head	head;	/**< list head */
	drm_map_t		*map;	/**< mapping */
} drm_map_list_t;


/**
 * Virtual Memory Areas list.  Used debugging purposes.
 */
typedef struct drm_vma_entry {
	struct vm_area_struct *vma;
	struct drm_vma_entry  *next;
	pid_t		      pid;
} drm_vma_entry_t;


/** \name Prototypes */
/*@{*/

extern struct page *drm_vm_nopage(struct vm_area_struct *vma, unsigned long address, int write_access);
extern struct page *drm_vm_shm_nopage(struct vm_area_struct *vma, unsigned long address, int write_access);
extern struct page *drm_vm_dma_nopage(struct vm_area_struct *vma, unsigned long address, int write_access);
extern struct page *drm_vm_sg_nopage(struct vm_area_struct *vma, unsigned long address, int write_access);
extern void drm_vm_open(struct vm_area_struct *vma);
extern void drm_vm_close(struct vm_area_struct *vma);
extern void drm_vm_shm_close(struct vm_area_struct *vma);
extern int drm_mmap_dma(struct file *filp, struct vm_area_struct *vma);
extern int drm_mmap(struct file *filp, struct vm_area_struct *vma);
extern unsigned int drm_poll(struct file *filp, struct poll_table_struct *wait);
extern ssize_t drm_read(struct file *filp, char *buf, size_t count, loff_t *off);

/*@}*/


#endif /* !_DRM_VM_H_ */
