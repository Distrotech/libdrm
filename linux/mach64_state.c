/* mach64_state.c -- State support for mach64 (Rage Pro) driver -*- linux-c -*-
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
	drm_mach64_context_regs_t *ctx = &sarea_priv->context_state;
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
			
			DMAOUTREG( MACH64_SC_LEFT_RIGHT, ctx->sc_left_right );
			DMAOUTREG( MACH64_SC_TOP_BOTTOM, ctx->sc_top_bottom );

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
			/*DMAOUTREG( MACH64_DP_WRITE_MASK, 0xffffffff );*/
			DMAOUTREG( MACH64_DP_WRITE_MASK, ctx->dp_write_mask );
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
			DMAGETPTR( dev_priv, 14 );
			
			DMAOUTREG( MACH64_Z_CNTL, 0 );
			DMAOUTREG( MACH64_SCALE_3D_CNTL, 0 );
			
			DMAOUTREG( MACH64_SC_LEFT_RIGHT, ctx->sc_left_right );
			DMAOUTREG( MACH64_SC_TOP_BOTTOM, ctx->sc_top_bottom );

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

	DMAGETPTR( dev_priv, 12 );

	DMAOUTREG( MACH64_Z_CNTL, 0 );
	DMAOUTREG( MACH64_SCALE_3D_CNTL, 0 );

	DMAOUTREG( MACH64_SC_LEFT_RIGHT, 0 | ( 8191 << 16 ) ); /* no scissor */
	DMAOUTREG( MACH64_SC_TOP_BOTTOM, 0 | ( 16383 << 16 ) );

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

static void mach64_dma_dispatch_vertex( drm_device_t *dev,
					drm_buf_t *buf )
{
	drm_mach64_private_t *dev_priv = dev->dev_private;
	drm_mach64_buf_priv_t *buf_priv = buf->dev_private;
	drm_mach64_sarea_t *sarea_priv = dev_priv->sarea_priv;
	int offset = buf->bus_address;
	int size = buf->used;
	int i = 0;
	DRM_DEBUG( "%s: buf=%d nbox=%d\n",
		   __FUNCTION__, buf->idx, sarea_priv->nbox );

	if ( 0 )
		mach64_print_dirty( "dispatch_vertex", sarea_priv->dirty );

	DRM_INFO( "idx = %u, offset=0x%08x, address=0x%08x, used=%u\n", buf->idx, buf->bus_address, buf->address, buf->used );
	
	if ( buf->used ) {
#if 0
		buf_priv->dispatched = 1;

		if ( sarea_priv->dirty & ~MACH64_UPLOAD_CLIPRECTS ) {
			mach64_emit_state( dev_priv );
		}
#endif

		do {
#if 0
			/* Emit the next set of up to three cliprects */
			if ( i < sarea_priv->nbox ) {
				mach64_emit_clip_rects( dev_priv,
							&sarea_priv->boxes[i]);
			}
#endif

			/* Emit the vertex buffer rendering commands */
			if (0) {
				unsigned * p = buf->address;
				unsigned used = buf->used >> 2;

				while(used)
				{
					unsigned reg, count;

					reg = *p & 0xffff;
					count = (*p >> 16) + 1;

					p++;
					used--;

					while(count && used)
					{
						unsigned data;

						data = *p;

						/* MACH64_WRITE( reg, data ); */

						p++;
						used--;
						count--;
					}
				}
			}
		} while ( ++i < sarea_priv->nbox );
	}

#if 0
	if ( buf_priv->discard ) {
		buf_priv->age = dev_priv->sarea_priv->last_dispatch;

		/* Emit the vertex buffer age */
		BEGIN_RING( 2 );

		OUT_RING( CCE_PACKET0( MACH64_LAST_DISPATCH_REG, 0 ) );
		OUT_RING( buf_priv->age );

		ADVANCE_RING();

		buf->pending = 1;
		buf->used = 0;
		/* FIXME: Check dispatched field */
		buf_priv->dispatched = 0;
	}
#else
	buf->used = 0;
	buf->pid = 0;
#endif

	dev_priv->sarea_priv->last_dispatch++;

	sarea_priv->dirty &= ~MACH64_UPLOAD_CLIPRECTS;
	sarea_priv->nbox = 0;
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

	LOCK_TEST_WITH_RETURN( dev );

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

	LOCK_TEST_WITH_RETURN( dev );

	if ( sarea_priv->nbox > MACH64_NR_SAREA_CLIPRECTS )
		sarea_priv->nbox = MACH64_NR_SAREA_CLIPRECTS;

	mach64_dma_dispatch_swap( dev );

	/* Make sure we restore the 3D state next time.
	 */
	dev_priv->sarea_priv->dirty |= (MACH64_UPLOAD_CONTEXT |
					MACH64_UPLOAD_MISC);
	return 0;
}

int mach64_dma_vertex( struct inode *inode, struct file *filp,
		       unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_mach64_private_t *dev_priv = dev->dev_private;
	drm_device_dma_t *dma = dev->dma;
	drm_buf_t *buf;
	drm_mach64_buf_priv_t *buf_priv;
	drm_mach64_vertex_t vertex;

	LOCK_TEST_WITH_RETURN( dev );

	if ( !dev_priv ) {
		DRM_ERROR( "%s called with no initialization\n", __FUNCTION__ );
		return -EINVAL;
	}

	if ( copy_from_user( &vertex, (drm_mach64_vertex_t *)arg,
			     sizeof(vertex) ) )
		return -EFAULT;

	DRM_DEBUG( "%s: pid=%d index=%d count=%d discard=%d\n",
		   __FUNCTION__, current->pid,
		   vertex.idx, vertex.count, vertex.discard );

#if 0
	if ( vertex.idx < 0 || vertex.idx >= dma->buf_count ) {
		DRM_ERROR( "buffer index %d (of %d max)\n",
			   vertex.idx, dma->buf_count - 1 );
		return -EINVAL;
	}
	if ( vertex.prim < 0 ||
	     vertex.prim > R128_CCE_VC_CNTL_PRIM_TYPE_TRI_TYPE2 ) {
		DRM_ERROR( "buffer prim %d\n", vertex.prim );
		return -EINVAL;
	}
#endif

	buf = dma->buflist[vertex.idx];
	buf_priv = buf->dev_private;

	if ( buf->pid != current->pid ) {
		DRM_ERROR( "process %d using buffer owned by %d\n",
			   current->pid, buf->pid );
		return -EINVAL;
	}
	if ( buf->pending ) {
		DRM_ERROR( "sending pending buffer %d\n", vertex.idx );
		return -EINVAL;
	}

	buf->used = vertex.count;
#if 0
	buf_priv->prim = vertex.prim;
	buf_priv->discard = vertex.discard;
#endif

	mach64_dma_dispatch_vertex( dev, buf );

	return 0;
}
