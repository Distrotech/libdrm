/** 
 * \file drm_memory.h 
 * Memory management wrappers for DRM
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


#ifndef _DRM_MEMORY_H_
#define _DRM_MEMORY_H_


/** \name Prototypes */
/*@{*/

extern void	     DRM(mem_init)(void);
extern int	     DRM(mem_info)(char *buf, char **start, off_t offset, int request, int *eof, void *data);
extern void	     *DRM(alloc)(size_t size, int area);
extern void	     *DRM(realloc)(void *oldpt, size_t oldsize, size_t size, int area);
extern void	     DRM(free)(void *pt, size_t size, int area);
extern unsigned long DRM(alloc_pages)(int order, int area);
extern void	     DRM(free_pages)(unsigned long address, int order, int area);
extern void	     *DRM(ioremap)(unsigned long offset, unsigned long size, drm_device_t *dev);
extern void	     *DRM(ioremap_nocache)(unsigned long offset, unsigned long size, drm_device_t *dev);
extern void	     DRM(ioremapfree)(void *pt, unsigned long size, drm_device_t *dev);

#if __REALLY_HAVE_AGP
extern agp_memory    *DRM(agp_alloc)(int pages, u32 type);
extern int           DRM(agp_free)(agp_memory *handle, int pages);
extern int           DRM(agp_bind)(agp_memory *handle, unsigned int start);
extern int           DRM(agp_unbind)(agp_memory *handle);
#endif

/*@}*/


#endif /* !_DRM_MEMORY_H_ */
