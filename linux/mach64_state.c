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
#include "mach64.h"
#include "drmP.h"
#include "mach64_drv.h"
#include "drm.h"


/* ================================================================
 * CCE command dispatch functions
 */

static void mach64_print_dirty( const char *msg, unsigned int flags )
{
#if 0
	DRM_INFO( "%s: (0x%x) %s%s%s%s%s%s%s%s%s\n",
		  msg,
		  flags,
		  (flags & MACH64_UPLOAD_CORE)        ? "core, " : "",
		  (flags & MACH64_UPLOAD_CONTEXT)     ? "context, " : "",
		  (flags & MACH64_UPLOAD_SETUP)       ? "setup, " : "",
		  (flags & MACH64_UPLOAD_TEX0)        ? "tex0, " : "",
		  (flags & MACH64_UPLOAD_TEX1)        ? "tex1, " : "",
		  (flags & MACH64_UPLOAD_MASKS)       ? "masks, " : "",
		  (flags & MACH64_UPLOAD_WINDOW)      ? "window, " : "",
		  (flags & MACH64_UPLOAD_CLIPRECTS)   ? "cliprects, " : "",
		  (flags & MACH64_REQUIRE_QUIESCENCE) ? "quiescence, " : "" );
#endif
}

static void mach64_dma_dispatch_clear( drm_device_t *dev,
				       unsigned int flags,
				       int cx, int cy, int cw, int ch,
				       unsigned int clear_color,
				       unsigned int clear_depth )
{
	drm_mach64_private_t *dev_priv = dev->dev_private;
	drm_mach64_sarea_t *sarea_priv = dev_priv->sarea_priv;
	int nbox = sarea_priv->nbox;
	drm_clip_rect_t *pbox = sarea_priv->boxes;
	u32 fb_bpp, depth_bpp;
	int i;
	DMALOCALS;
	DRM_DEBUG( "%s\n", __FUNCTION__ );

	switch ( dev_priv->fb_bpp ) {
	case 16:
		fb_bpp = MACH64_DATATYPE_RGB565;
		break;
	case 32:
		fb_bpp = MACH64_DATATYPE_ARGB8888;
		break;
	default:
		return;
	}
	switch ( dev_priv->depth_bpp ) {
	case 16:
		depth_bpp = MACH64_DATATYPE_RGB565;
		break;
	case 24:
	case 32:
		depth_bpp = MACH64_DATATYPE_ARGB8888;
		break;
	default:
		return;
	}
	
	for ( i = 0 ; i < nbox ; i++ ) {
		int x = pbox[i].x1;
		int y = pbox[i].y1;
		int w = pbox[i].x2 - x;
		int h = pbox[i].y2 - y;
		
		DRM_DEBUG( "dispatch clear %d,%d-%d,%d flags 0x%x\n",
			   pbox[i].x1, pbox[i].y1,
			   pbox[i].x2, pbox[i].y2, flags );
		
		if ( flags & (MACH64_FRONT | MACH64_BACK) ) {
			/* Setup for color buffer clears
			 */
			DMAGETPTR( dev_priv, 14 );
			
			DMAOUTREG( MACH64_Z_CNTL, 0 );
			DMAOUTREG( MACH64_SCALE_3D_CNTL, 0 );
			
			DMAOUTREG( MACH64_CLR_CMP_CNTL, 0 );
			DMAOUTREG( MACH64_GUI_TRAJ_CNTL,
				   (MACH64_DST_X_LEFT_TO_RIGHT |
				    MACH64_DST_Y_TOP_TO_BOTTOM) );
			
			DMAOUTREG( MACH64_DP_PIX_WIDTH, ((fb_bpp << 0) |
							 (fb_bpp << 4) |
							 (fb_bpp << 8) |
							 (fb_bpp << 16) |
							 (fb_bpp << 28)) );
			
			DMAOUTREG( MACH64_DP_FRGD_CLR, clear_color );
			/* FIXME: Use color mask from state info */
			DMAOUTREG( MACH64_DP_WRITE_MASK, 0xffffffff );
			DMAOUTREG( MACH64_DP_MIX, (MACH64_BKGD_MIX_D |
						   MACH64_FRGD_MIX_S) );
			DMAOUTREG( MACH64_DP_SRC, (MACH64_BKGD_SRC_FRGD_CLR |
						   MACH64_FRGD_SRC_FRGD_CLR |
						   MACH64_MONO_SRC_ONE) );
			
			DMAADVANCE( dev_priv );
			
		}

		if ( flags & MACH64_FRONT ) {
			DMAGETPTR( dev_priv, 3 );
			
			DMAOUTREG( MACH64_DST_OFF_PITCH,
				   dev_priv->front_offset_pitch );
			DMAOUTREG( MACH64_DST_X_Y,
				   (y << 16) | x );
			DMAOUTREG( MACH64_DST_WIDTH_HEIGHT,
				   (h << 16) | w );
			
			DMAADVANCE( dev_priv );
		}
		
		if ( flags & MACH64_BACK ) {
			DMAGETPTR( dev_priv, 3 );
			
			DMAOUTREG( MACH64_DST_OFF_PITCH,
				   dev_priv->back_offset_pitch );
			DMAOUTREG( MACH64_DST_X_Y,
				   (y << 16) | x );
			DMAOUTREG( MACH64_DST_WIDTH_HEIGHT,
				   (h << 16) | w );
			
			DMAADVANCE( dev_priv );
		}
		
		if ( flags & MACH64_DEPTH ) {
			/* Setup for depth buffer clear
			 */
			DMAGETPTR( dev_priv, 12 );
			
			DMAOUTREG( MACH64_Z_CNTL, 0 );
			DMAOUTREG( MACH64_SCALE_3D_CNTL, 0 );
			
			DMAOUTREG( MACH64_CLR_CMP_CNTL, 0 );
			DMAOUTREG( MACH64_GUI_TRAJ_CNTL,
				   (MACH64_DST_X_LEFT_TO_RIGHT |
				    MACH64_DST_Y_TOP_TO_BOTTOM) );
			
			DMAOUTREG( MACH64_DP_PIX_WIDTH, ((depth_bpp << 0) |
							 (depth_bpp << 4) |
							 (depth_bpp << 8) |
							 (depth_bpp << 16) |
							 (depth_bpp << 28)) );
			
			DMAOUTREG( MACH64_DP_FRGD_CLR, clear_depth );
			DMAOUTREG( MACH64_DP_WRITE_MASK, 0xffffffff );
			DMAOUTREG( MACH64_DP_MIX, (MACH64_BKGD_MIX_D |
						   MACH64_FRGD_MIX_S) );
			DMAOUTREG( MACH64_DP_SRC, (MACH64_BKGD_SRC_FRGD_CLR |
						   MACH64_FRGD_SRC_FRGD_CLR |
						   MACH64_MONO_SRC_ONE) );
			
			DMAOUTREG( MACH64_DST_OFF_PITCH,
				   dev_priv->depth_offset_pitch );
			DMAOUTREG( MACH64_DST_X_Y,
				   (y << 16) | x );
			DMAOUTREG( MACH64_DST_WIDTH_HEIGHT,
				   (h << 16) | w );
			
			DMAADVANCE( dev_priv );
		}
	}
}

static void mach64_dma_dispatch_swap( drm_device_t *dev )
{
	drm_mach64_private_t *dev_priv = dev->dev_private;
	drm_mach64_sarea_t *sarea_priv = dev_priv->sarea_priv;
	int nbox = sarea_priv->nbox;
	drm_clip_rect_t *pbox = sarea_priv->boxes;
	u32 fb_bpp;
	int i;
	DMALOCALS;
	DRM_DEBUG( "%s\n", __FUNCTION__ );

	switch ( dev_priv->fb_bpp ) {
	case 16:
		fb_bpp = MACH64_DATATYPE_RGB565;
		break;
	case 32:
	default:
		fb_bpp = MACH64_DATATYPE_ARGB8888;
		break;
	}

	DMAGETPTR( dev_priv, 10 );

	DMAOUTREG( MACH64_Z_CNTL, 0 );
	DMAOUTREG( MACH64_SCALE_3D_CNTL, 0 );

	DMAOUTREG( MACH64_CLR_CMP_CNTL, 0 );
	DMAOUTREG( MACH64_GUI_TRAJ_CNTL, (MACH64_DST_X_LEFT_TO_RIGHT |
					  MACH64_DST_Y_TOP_TO_BOTTOM) );

	DMAOUTREG( MACH64_DP_PIX_WIDTH, ((fb_bpp << 0) |
					 (fb_bpp << 4) |
					 (fb_bpp << 8) |
					 (fb_bpp << 16) |
					 (fb_bpp << 28)) );

	DMAOUTREG( MACH64_DP_WRITE_MASK, 0xffffffff );
	DMAOUTREG( MACH64_DP_MIX, (MACH64_BKGD_MIX_D |
				   MACH64_FRGD_MIX_S) );
	DMAOUTREG( MACH64_DP_SRC, (MACH64_BKGD_SRC_BKGD_CLR |
				   MACH64_FRGD_SRC_BLIT |
				   MACH64_MONO_SRC_ONE) );

	DMAOUTREG( MACH64_SRC_OFF_PITCH, dev_priv->back_offset_pitch );
	DMAOUTREG( MACH64_DST_OFF_PITCH, dev_priv->front_offset_pitch );

	DMAADVANCE( dev_priv );

	for ( i = 0 ; i < nbox ; i++ ) {
		int x = pbox[i].x1;
		int y = pbox[i].y1;
		int w = pbox[i].x2 - x;
		int h = pbox[i].y2 - y;

		DRM_DEBUG( "dispatch swap %d,%d-%d,%d\n",
			  pbox[i].x1, pbox[i].y1,
			  pbox[i].x2, pbox[i].y2 );

		DMAGETPTR( dev_priv, 4 );

		DMAOUTREG( MACH64_SRC_WIDTH1, w );
		DMAOUTREG( MACH64_SRC_Y_X, (x << 16) | y );
		DMAOUTREG( MACH64_DST_Y_X, (x << 16) | y );
		DMAOUTREG( MACH64_DST_WIDTH_HEIGHT, (h << 16) | w );

		DMAADVANCE( dev_priv );
	}

#if 0
	/* Increment the frame counter.  The client-side 3D driver must
	 * throttle the framerate by waiting for this value before
	 * performing the swapbuffer ioctl.
	 */
	dev_priv->sarea_priv->last_frame++;

	BEGIN_RING( 2 );

	OUT_RING( CCE_PACKET0( MACH64_LAST_FRAME_REG, 0 ) );
	OUT_RING( dev_priv->sarea_priv->last_frame );

	ADVANCE_RING();
#endif
}


/* ================================================================
 * IOCTL functions
 */

int mach64_dma_clear( struct inode *inode, struct file *filp,
		    unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_mach64_private_t *dev_priv = dev->dev_private;
	drm_mach64_sarea_t *sarea_priv = dev_priv->sarea_priv;
	drm_mach64_clear_t clear;
	DRM_DEBUG( "%s\n", __FUNCTION__ );

	if ( !_DRM_LOCK_IS_HELD( dev->lock.hw_lock->lock ) ||
	     dev->lock.pid != current->pid ) {
		DRM_ERROR( "mach64_cce_clear called without lock held\n" );
		return -EINVAL;
	}

	if ( copy_from_user( &clear, (drm_mach64_clear_t *) arg,
			     sizeof(clear) ) )
		return -EFAULT;

	if ( sarea_priv->nbox > MACH64_NR_SAREA_CLIPRECTS )
		sarea_priv->nbox = MACH64_NR_SAREA_CLIPRECTS;

	mach64_dma_dispatch_clear( dev, clear.flags,
				   clear.x, clear.y, clear.w, clear.h,
				   clear.clear_color, clear.clear_depth );

	/* Make sure we restore the 3D state next time.
	 */
	dev_priv->sarea_priv->dirty |= (MACH64_UPLOAD_CONTEXT |
					MACH64_UPLOAD_MISC);
	return 0;
}

int mach64_dma_swap( struct inode *inode, struct file *filp,
		     unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_mach64_private_t *dev_priv = dev->dev_private;
	drm_mach64_sarea_t *sarea_priv = dev_priv->sarea_priv;
	DRM_DEBUG( "%s\n", __FUNCTION__ );

	if ( !_DRM_LOCK_IS_HELD( dev->lock.hw_lock->lock ) ||
	     dev->lock.pid != current->pid ) {
		DRM_ERROR( "mach64_cce_swap called without lock held\n" );
		return -EINVAL;
	}

	if ( sarea_priv->nbox > MACH64_NR_SAREA_CLIPRECTS )
		sarea_priv->nbox = MACH64_NR_SAREA_CLIPRECTS;

	mach64_dma_dispatch_swap( dev );

	/* Make sure we restore the 3D state next time.
	 */
	dev_priv->sarea_priv->dirty |= (MACH64_UPLOAD_CONTEXT |
					MACH64_UPLOAD_MISC);
	return 0;
}
