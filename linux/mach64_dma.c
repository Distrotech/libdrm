/* mach64_dma.c -- DMA support for mach64 (Rage Pro) driver -*- linux-c -*-
 * Created: Sun Dec 03 19:20:26 2000 by gareth@valinux.com
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
 *   Gareth Hughes <gareth@valinux.com>
 */

#include "mach64.h"
#include "drmP.h"
#include "mach64_drv.h"

#include <linux/interrupt.h>	/* For task queue support */
#include <linux/delay.h>


/* ================================================================
 * Engine, FIFO control
 */

int mach64_do_wait_for_fifo( drm_mach64_private_t *dev_priv, int entries )
{
	int slots = 0, i;

	for ( i = 0 ; i < dev_priv->usec_timeout ; i++ ) {
		slots = (MACH64_READ( MACH64_FIFO_STAT ) &
			 MACH64_FIFO_SLOT_MASK);
		if ( slots <= (0x8000 >> entries) ) return 0;
		udelay( 1 );
	}

	DRM_INFO( "failed! slots=%d entries=%d\n", slots, entries );
	return -EBUSY;
}

int mach64_do_wait_for_idle( drm_mach64_private_t *dev_priv )
{
	int i, ret;

	ret = mach64_do_wait_for_fifo( dev_priv, 16 );
	if ( ret < 0 ) return ret;

	for ( i = 0 ; i < dev_priv->usec_timeout ; i++ ) {
		if ( !(MACH64_READ( MACH64_GUI_STAT ) & MACH64_GUI_ACTIVE) ) {
			return 0;
		}
		udelay( 1 );
	}

	DRM_INFO( "failed! GUI_STAT=0x%08x\n",
		   MACH64_READ( MACH64_GUI_STAT ) );
	return -EBUSY;
}


/* ================================================================
 * DMA initialization, cleanup
 */

/* Reset the engine.  This will stop the DMA if it is running.
 */
int mach64_do_engine_reset( drm_device_t *dev )
{
	drm_mach64_private_t *dev_priv = dev->dev_private;
	u32 bus_cntl, gen_test_cntl;

	DRM_DEBUG( "%s\n", __FUNCTION__ );

	/* Kill off any outstanding DMA transfers.
	 */
	bus_cntl = MACH64_READ( MACH64_BUS_CNTL );
	MACH64_WRITE( MACH64_BUS_CNTL,
				  bus_cntl | MACH64_BUS_MASTER_DIS );

	/* Reset the GUI engine (high to low transition).
	 */
	gen_test_cntl = MACH64_READ( MACH64_GEN_TEST_CNTL );
	MACH64_WRITE( MACH64_GEN_TEST_CNTL,
				  gen_test_cntl & ~MACH64_GUI_ENGINE_ENABLE );
	/* Enable the GUI engine
	 */
	gen_test_cntl = MACH64_READ( MACH64_GEN_TEST_CNTL );
	MACH64_WRITE( MACH64_GEN_TEST_CNTL,
				  gen_test_cntl | MACH64_GUI_ENGINE_ENABLE );

	/* ensure engine is not locked up by clearing any FIFO or HOST errors
	*/
	bus_cntl = MACH64_READ( MACH64_BUS_CNTL );
	MACH64_WRITE( MACH64_BUS_CNTL, bus_cntl | 0x00a00000 );

	return 0;
}

static int mach64_do_dma_init( drm_device_t *dev, drm_mach64_init_t *init )
{
	drm_mach64_private_t *dev_priv;
	struct list_head *list;
	u32 tmp;

	DRM_DEBUG( "%s\n", __FUNCTION__ );

	dev_priv = DRM(alloc)( sizeof(drm_mach64_private_t), DRM_MEM_DRIVER );
	if ( dev_priv == NULL )
		return -ENOMEM;
	
	memset( dev_priv, 0, sizeof(drm_mach64_private_t) );

	dev_priv->is_pci	= init->is_pci;
		
	dev_priv->fb_bpp	= init->fb_bpp;
	dev_priv->front_offset  = init->front_offset;
	dev_priv->front_pitch   = init->front_pitch;
	dev_priv->back_offset   = init->back_offset;
	dev_priv->back_pitch    = init->back_pitch;

	dev_priv->depth_bpp     = init->depth_bpp;
	dev_priv->depth_offset  = init->depth_offset;
	dev_priv->depth_pitch   = init->depth_pitch;

	dev_priv->front_offset_pitch	= (((dev_priv->front_pitch/8) << 22) |
					   (dev_priv->front_offset >> 3));
	dev_priv->back_offset_pitch	= (((dev_priv->back_pitch/8) << 22) |
					   (dev_priv->back_offset >> 3));
	dev_priv->depth_offset_pitch	= (((dev_priv->depth_pitch/8) << 22) |
					   (dev_priv->depth_offset >> 3));

	dev_priv->usec_timeout		= 1000000;

	list_for_each(list, &dev->maplist->head) {
		drm_map_list_t *r_list = (drm_map_list_t *)list;
		if( r_list->map && 
		    r_list->map->type == _DRM_SHM &&
                    r_list->map->flags & _DRM_CONTAINS_LOCK ) {
			dev_priv->sarea = r_list->map;
			break;
		}
	}
	if(!dev_priv->sarea) {
		dev->dev_private = (void *)dev_priv;
	   	mach64_do_cleanup_dma(dev);
	   	DRM_ERROR("can not find sarea!\n");
	   	return -EINVAL;
	}
	DRM_FIND_MAP( dev_priv->fb, init->fb_offset );
	if(!dev_priv->fb) {
		dev->dev_private = (void *)dev_priv;
	   	mach64_do_cleanup_dma(dev);
	   	DRM_ERROR("can not find frame buffer map!\n");
	   	return -EINVAL;
	}
	DRM_FIND_MAP( dev_priv->mmio, init->mmio_offset );
	if(!dev_priv->mmio) {
		dev->dev_private = (void *)dev_priv;
	   	mach64_do_cleanup_dma(dev);
	   	DRM_ERROR("can not find mmio map!\n");
	   	return -EINVAL;
	}

	dev_priv->sarea_priv = (drm_mach64_sarea_t *)
		((u8 *)dev_priv->sarea->handle +
		 init->sarea_priv_offset);

	if( !dev_priv->is_pci ) {
		DRM_FIND_MAP( dev_priv->buffers, init->buffers_offset );
		if( !dev_priv->buffers ) {
			dev->dev_private = (void *)dev_priv;
			mach64_do_cleanup_dma( dev );
			DRM_ERROR( "can not find dma buffer map!\n" );
			return -EINVAL;
		}
		DRM_IOREMAP( dev_priv->buffers );
		if( !dev_priv->buffers->handle ) {
			dev->dev_private = (void *) dev_priv;
			mach64_do_cleanup_dma( dev );
			DRM_ERROR( "can not ioremap virtual address for"
				   " dma buffer\n" );
			return -ENOMEM;
		}
	}

	tmp = MACH64_READ( MACH64_BUS_CNTL );
	tmp = ( tmp | MACH64_BUS_EXT_REG_EN ) & ~MACH64_BUS_MASTER_DIS;
	MACH64_WRITE( MACH64_BUS_CNTL, tmp );

	tmp = MACH64_READ( MACH64_GUI_CNTL );
	MACH64_WRITE( MACH64_GUI_CNTL, ( ( tmp & ~MACH64_CMDFIFO_SIZE_MASK ) \
					 | MACH64_CMDFIFO_SIZE_128 ) );
	DRM_INFO( "GUI_STAT=0x%08x\n", MACH64_READ( MACH64_GUI_STAT ) );
	DRM_INFO( "GUI_CNTL=0x%08x\n", MACH64_READ( MACH64_GUI_CNTL ) );

	dev->dev_private = (void *) dev_priv;
	
	return 0;
}

int mach64_do_cleanup_dma( drm_device_t *dev )
{
	DRM_DEBUG( "%s\n", __FUNCTION__ );
	
	if ( dev->dev_private ) {
		drm_mach64_private_t *dev_priv = dev->dev_private;
		
		if(dev_priv->buffers) {
			DRM_IOREMAPFREE( dev_priv->buffers );
		}
		DRM(free)( dev_priv, sizeof(drm_mach64_private_t),
			   DRM_MEM_DRIVER );
		dev->dev_private = NULL;
	}
	
	return 0;
}

int mach64_dma_init( struct inode *inode, struct file *filp,
		     unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_mach64_init_t init;
		
	DRM_DEBUG( "%s\n", __FUNCTION__ );

	if ( copy_from_user( &init, (drm_mach64_init_t *)arg, sizeof(init) ) )
		return -EFAULT;

	switch ( init.func ) {
	case MACH64_INIT_DMA:
		return mach64_do_dma_init( dev, &init );
	case MACH64_CLEANUP_DMA:
		return mach64_do_cleanup_dma( dev );
	}
		
	return -EINVAL;
}

int mach64_dma_idle( struct inode *inode, struct file *filp,
		     unsigned int cmd, unsigned long arg )
{
        drm_file_t *priv = filp->private_data;
        drm_device_t *dev = priv->dev;
	drm_mach64_private_t *dev_priv = dev->dev_private;

	DRM_DEBUG( "%s\n", __FUNCTION__ );

	LOCK_TEST_WITH_RETURN( dev );
	
	return mach64_do_wait_for_idle( dev_priv );
}

int mach64_engine_reset( struct inode *inode, struct file *filp,
			 unsigned int cmd, unsigned long arg )
{
        drm_file_t *priv = filp->private_data;
        drm_device_t *dev = priv->dev;
	DRM_DEBUG( "%s\n", __FUNCTION__ );

	LOCK_TEST_WITH_RETURN( dev );

	return mach64_do_engine_reset( dev );
}


/* ================================================================
 * Primary DMA stream management
 */


/* ================================================================
 * Freelist management
 */
#define MACH64_BUFFER_USED	0xffffffff
#define MACH64_BUFFER_FREE	0

drm_buf_t *mach64_freelist_get( drm_device_t *dev )
{
	drm_device_dma_t *dma = dev->dma;
	drm_mach64_private_t *dev_priv = dev->dev_private;
	drm_mach64_buf_priv_t *buf_priv;
	drm_buf_t *buf;
	int i, t;

	/* FIXME: Optimize -- use freelist code */

	for ( i = 0 ; i < dma->buf_count ; i++ ) {
		buf = dma->buflist[i];
		buf_priv = buf->dev_private;
		if ( buf->pid == 0 )
			return buf;
	}

#if 0
	for ( t = 0 ; t < dev_priv->usec_timeout ; t++ ) {
		u32 done_age = MACH64_READ( MACH64_LAST_DISPATCH_REG );

		for ( i = 0 ; i < dma->buf_count ; i++ ) {
			buf = dma->buflist[i];
			buf_priv = buf->dev_private;
			if ( buf->pending && buf_priv->age <= done_age ) {
				/* The buffer has been processed, so it
				 * can now be used.
				 */
				buf->pending = 0;
				return buf;
			}
		}
		udelay( 1 );
	}
#endif

	DRM_ERROR( "returning NULL!\n" );
	return NULL;
}
/* ================================================================
 * DMA command submission
 */


static int mach64_dma_get_buffers( drm_device_t *dev, drm_dma_t *d )
{
	int i;
	drm_buf_t *buf;

	for ( i = d->granted_count ; i < d->request_count ; i++ ) {
		buf = mach64_freelist_get( dev );
		if ( !buf ) return -EAGAIN;

		buf->pid = current->pid;

		if ( copy_to_user( &d->request_indices[i], &buf->idx,
				   sizeof(buf->idx) ) )
			return -EFAULT;
		if ( copy_to_user( &d->request_sizes[i], &buf->total,
				   sizeof(buf->total) ) )
			return -EFAULT;

		d->granted_count++;
	}
	return 0;
}

int mach64_dma_buffers( struct inode *inode, struct file *filp,
		      unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_device_dma_t *dma = dev->dma;
	int ret = 0;
	drm_dma_t d;

	LOCK_TEST_WITH_RETURN( dev );

	if ( copy_from_user( &d, (drm_dma_t *) arg, sizeof(d) ) )
		return -EFAULT;

	/* Please don't send us buffers.
	 */
	if ( d.send_count != 0 ) {
		DRM_ERROR( "Process %d trying to send %d buffers via drmDMA\n",
			   current->pid, d.send_count );
		return -EINVAL;
	}

	/* We'll send you buffers.
	 */
	if ( d.request_count < 0 || d.request_count > dma->buf_count ) {
		DRM_ERROR( "Process %d trying to get %d buffers (of %d max)\n",
			   current->pid, d.request_count, dma->buf_count );
		return -EINVAL;
	}

	d.granted_count = 0;

	if ( d.request_count ) {
		ret = mach64_dma_get_buffers( dev, &d );
	}

	if ( copy_to_user( (drm_dma_t *) arg, &d, sizeof(d) ) )
		return -EFAULT;

	return ret;
}
