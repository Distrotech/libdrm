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
 *    Gareth Hughes <gareth@valinux.com>
 */

#define __NO_VERSION__
#include "drmP.h"
#include "mach64_drv.h"

#include <linux/interrupt.h>	/* For task queue support */
#include <linux/delay.h>


#define DO_REMAP(_m)	(_m)->handle = drm_ioremap( (_m)->offset, (_m)->size )

#define DO_REMAPFREE(_m)						\
	do {								\
		if ( (_m)->handle && (_m)->size )			\
			drm_ioremapfree( (_m)->handle, (_m)->size );	\
	} while (0)

#define DO_FIND_MAP(_m, _o)						\
	do {								\
		int _i;							\
		for ( _i = 0 ; _i < dev->map_count ; _i++ ) {		\
			if ( dev->maplist[_i]->offset == _o ) {		\
				_m = dev->maplist[_i];			\
				break;					\
			}						\
		}							\
	} while (0)


/* ================================================================
 * Engine control
 */

int mach64_do_wait_for_fifo( drm_mach64_private_t *dev_priv, int entries )
{
	int i;

	for ( i = 0 ; i < dev_priv->usec_timeout ; i++ ) {
		int slots = (MACH64_READ( MACH64_FIFO_STAT ) &
			     MACH64_FIFO_SLOT_MASK);
		if ( slots <= (0x8000 >> entries) ) return 0;
		udelay( 1 );
	}

	DRM_ERROR( "%s failed!\n", __FUNCTION__ );
	return -EBUSY;
}

static int mach64_do_wait_for_idle( drm_mach64_private_t *dev_priv )
{
	int i, ret;

	ret = mach64_do_wait_for_fifo( dev_priv, 16 );
	if ( ret ) return ret;

	for ( i = 0 ; i < dev_priv->usec_timeout ; i++ ) {
		if ( !(MACH64_READ( MACH64_GUI_STAT ) & MACH64_GUI_ACTIVE) ) {
			return 0;
		}
		udelay( 1 );
	}

	DRM_ERROR( "%s failed!\n", __FUNCTION__ );
	return -EBUSY;
}

static int mach64_do_dma_init( drm_device_t *dev, drm_mach64_init_t *init )
{
      	drm_mach64_private_t *dev_priv;
	u32 tmp;
	DRM_DEBUG( "%s\n", __FUNCTION__ );

	dev_priv = drm_alloc( sizeof(drm_mach64_private_t), DRM_MEM_DRIVER );
	if ( dev_priv == NULL )
		return -ENOMEM;
	dev->dev_private = (void *) dev_priv;

	memset( dev_priv, 0, sizeof(drm_mach64_private_t) );

	dev_priv->fb_bpp		= init->fb_bpp;
	dev_priv->front_offset		= init->front_offset;
	dev_priv->front_pitch		= init->front_pitch;
	dev_priv->back_offset		= init->back_offset;
	dev_priv->back_pitch		= init->back_pitch;

	dev_priv->depth_bpp		= init->depth_bpp;
	dev_priv->depth_offset		= init->depth_offset;
	dev_priv->depth_pitch		= init->depth_pitch;

	dev_priv->front_offset_pitch	= (((dev_priv->front_pitch/8) << 22) |
					   (dev_priv->front_offset >> 3));
	dev_priv->back_offset_pitch	= (((dev_priv->back_pitch/8) << 22) |
					   (dev_priv->back_offset >> 3));
	dev_priv->depth_offset_pitch	= (((dev_priv->depth_pitch/8) << 22) |
					   (dev_priv->depth_offset >> 3));

	dev_priv->usec_timeout		= 1000000;

	dev_priv->sarea = dev->maplist[0];
	dev_priv->sarea_priv = (drm_mach64_sarea_t *)
		((u8 *)dev_priv->sarea->handle +
		 init->sarea_priv_offset);

	DO_FIND_MAP( dev_priv->fb, init->fb_offset );
	DO_FIND_MAP( dev_priv->mmio, init->mmio_offset );

	/* FIXME: Do the scratch register test for now, can remove
	 * later on.
	 */
	tmp = MACH64_READ( MACH64_SCRATCH_REG0 );

	MACH64_WRITE( MACH64_SCRATCH_REG0, 0x55555555 );

	if ( MACH64_READ( MACH64_SCRATCH_REG0 ) == 0x55555555 ) {

		MACH64_WRITE( MACH64_SCRATCH_REG0, 0xaaaaaaaa );

		if ( MACH64_READ( MACH64_SCRATCH_REG0 ) != 0xaaaaaaaa ) {
			DRM_ERROR( "2nd scratch reg failed!\n" );
		}
	} else {
		DRM_ERROR( "1st scratch reg failed!\n" );
	}

	MACH64_WRITE( MACH64_SCRATCH_REG0, tmp );

	return 0;
}

static int mach64_do_dma_cleanup( drm_device_t *dev )
{
	DRM_DEBUG( "%s\n", __FUNCTION__ );

	if ( dev->dev_private ) {
		drm_mach64_private_t *dev_priv = dev->dev_private;

		drm_free( dev_priv, sizeof(drm_mach64_private_t),
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

	if ( copy_from_user( &init, (drm_mach64_init_t *)arg, sizeof(init) ) )
		return -EFAULT;

	switch ( init.func ) {
	case MACH64_INIT_DMA:
		return mach64_do_dma_init( dev, &init );
	case MACH64_CLEANUP_DMA:
		return mach64_do_dma_cleanup( dev );
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

	if ( !_DRM_LOCK_IS_HELD( dev->lock.hw_lock->lock ) ||
	     dev->lock.pid != current->pid ) {
		DRM_ERROR( "%s called without lock held\n", __FUNCTION__ );
		return -EINVAL;
	}

	return mach64_do_wait_for_idle( dev_priv );
}
