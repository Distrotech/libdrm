/* mach64_drv.h -- Private header for mach64 driver -*- linux-c -*-
 * Created: Fri Nov 24 22:07:58 2000 by gareth@valinux.com
 *
 * Copyright 2000 Gareth Hughes
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
 * GARETH HUGHES BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Gareth Hughes <gareth@valinux.com>
 */

#ifndef __MACH64_DRV_H__
#define __MACH64_DRV_H__

typedef struct drm_mach64_private {
	u32 dummy;
} drm_mach64_private_t;


				/* mach64_drv.c */
extern int  mach64_version( struct inode *inode, struct file *filp,
			    unsigned int cmd, unsigned long arg );
extern int  mach64_open( struct inode *inode, struct file *filp );
extern int  mach64_release( struct inode *inode, struct file *filp );
extern int  mach64_ioctl( struct inode *inode, struct file *filp,
			  unsigned int cmd, unsigned long arg );
extern int  mach64_lock( struct inode *inode, struct file *filp,
			 unsigned int cmd, unsigned long arg );
extern int  mach64_unlock( struct inode *inode, struct file *filp,
			   unsigned int cmd, unsigned long arg );

extern int  mach64_resctx( struct inode *inode, struct file *filp,
			   unsigned int cmd, unsigned long arg );
extern int  mach64_addctx( struct inode *inode, struct file *filp,
			   unsigned int cmd, unsigned long arg );
extern int  mach64_modctx( struct inode *inode, struct file *filp,
			   unsigned int cmd, unsigned long arg );
extern int  mach64_getctx( struct inode *inode, struct file *filp,
			   unsigned int cmd, unsigned long arg );
extern int  mach64_switchctx( struct inode *inode, struct file *filp,
			      unsigned int cmd, unsigned long arg );
extern int  mach64_newctx( struct inode *inode, struct file *filp,
			   unsigned int cmd, unsigned long arg );
extern int  mach64_rmctx( struct inode *inode, struct file *filp,
			  unsigned int cmd, unsigned long arg );

extern int  mach64_context_switch( drm_device_t *dev, int old, int new );
extern int  mach64_context_switch_complete( drm_device_t *dev, int new );


#if 0
				/* mach64_dma.c */
extern int mach64_init_cce(struct inode *inode, struct file *filp,
			 unsigned int cmd, unsigned long arg);
extern int mach64_eng_reset(struct inode *inode, struct file *filp,
			  unsigned int cmd, unsigned long arg);
extern int mach64_eng_flush(struct inode *inode, struct file *filp,
			  unsigned int cmd, unsigned long arg);
extern int mach64_submit_pkt(struct inode *inode, struct file *filp,
			   unsigned int cmd, unsigned long arg);
extern int mach64_cce_idle(struct inode *inode, struct file *filp,
			 unsigned int cmd, unsigned long arg);
extern int mach64_vertex_buf(struct inode *inode, struct file *filp,
			   unsigned int cmd, unsigned long arg);

				/* mach64_bufs.c */
extern int mach64_addbufs(struct inode *inode, struct file *filp,
			unsigned int cmd, unsigned long arg);
extern int mach64_mapbufs(struct inode *inode, struct file *filp,
			unsigned int cmd, unsigned long arg);
#endif

#endif
