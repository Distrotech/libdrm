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
 * DMA hardware state programming functions
 */

static void mach64_print_dirty( const char *msg, unsigned int flags )
{
	DRM_DEBUG( "%s: (0x%x) %s%s%s%s%s%s%s%s%s%s%s%s\n",
		msg,
		flags,
		(flags & MACH64_UPLOAD_DST_OFF_PITCH) ? "dst_off_pitch, " : "",
		(flags & MACH64_UPLOAD_Z_ALPHA_CNTL)  ? "z_alpha_cntl, " : "",
		(flags & MACH64_UPLOAD_SCALE_3D_CNTL) ? "scale_3d_cntl, " : "",
		(flags & MACH64_UPLOAD_DP_FOG_CLR)    ? "dp_fog_clr, " : "",
		(flags & MACH64_UPLOAD_DP_WRITE_MASK) ? "dp_write_mask, " : "",
		(flags & MACH64_UPLOAD_DP_PIX_WIDTH)  ? "dp_pix_width, " : "",
		(flags & MACH64_UPLOAD_SETUP_CNTL)    ? "setup_cntl, " : "",
		(flags & MACH64_UPLOAD_MISC)          ? "misc, " : "",
		(flags & MACH64_UPLOAD_TEXTURE)       ? "texture, " : "",
		(flags & MACH64_UPLOAD_TEX0IMAGE)     ? "tex0 image, " : "",
		(flags & MACH64_UPLOAD_TEX1IMAGE)     ? "tex1 image, " : "",
		(flags & MACH64_UPLOAD_CLIPRECTS)     ? "cliprects, " : "" );
}

static inline int mach64_emit_state( drm_mach64_private_t *dev_priv )
{
	drm_mach64_sarea_t *sarea_priv = dev_priv->sarea_priv;
	drm_mach64_context_regs_t *regs = &sarea_priv->context_state;
	unsigned int dirty = sarea_priv->dirty;
	u32 offset = ((regs->tex_size_pitch & 0xf0) >> 2);
	DMALOCALS;

	if ( MACH64_VERBOSE ) {
		mach64_print_dirty( __FUNCTION__, dirty );
	} else {
		DRM_DEBUG( "%s: dirty=0x%08x\n", __FUNCTION__, dirty );
	}

	DMAGETPTR( dev_priv, 19 ); /* returns on failure to get buffer */

	if ( dirty & MACH64_UPLOAD_MISC ) {
		DMAOUTREG( MACH64_DP_MIX, regs->dp_mix );
		DMAOUTREG( MACH64_DP_SRC, regs->dp_src );
		DMAOUTREG( MACH64_CLR_CMP_CNTL, regs->clr_cmp_cntl );
		DMAOUTREG( MACH64_GUI_TRAJ_CNTL, regs->gui_traj_cntl );
		DMAOUTREG( MACH64_SC_LEFT_RIGHT, regs->sc_left_right );
		DMAOUTREG( MACH64_SC_TOP_BOTTOM, regs->sc_top_bottom );
		sarea_priv->dirty &= ~MACH64_UPLOAD_MISC;
	}

	if ( dirty & MACH64_UPLOAD_DST_OFF_PITCH ) {
		DMAOUTREG( MACH64_DST_OFF_PITCH, regs->dst_off_pitch );
		sarea_priv->dirty &= ~MACH64_UPLOAD_DST_OFF_PITCH;
	}
	if ( dirty & MACH64_UPLOAD_Z_OFF_PITCH ) {
		DMAOUTREG( MACH64_Z_OFF_PITCH, regs->z_off_pitch );
		sarea_priv->dirty &= ~MACH64_UPLOAD_Z_OFF_PITCH;
	}
	if ( dirty & MACH64_UPLOAD_Z_ALPHA_CNTL ) {
		DMAOUTREG( MACH64_Z_CNTL, regs->z_cntl );
		DMAOUTREG( MACH64_ALPHA_TST_CNTL, regs->alpha_tst_cntl );
		sarea_priv->dirty &= ~MACH64_UPLOAD_Z_ALPHA_CNTL;
	}
	if ( dirty & MACH64_UPLOAD_SCALE_3D_CNTL ) {
		DMAOUTREG( MACH64_SCALE_3D_CNTL, regs->scale_3d_cntl );
		sarea_priv->dirty &= ~MACH64_UPLOAD_SCALE_3D_CNTL;
	}
	if ( dirty & MACH64_UPLOAD_DP_FOG_CLR ) {
		DMAOUTREG( MACH64_DP_FOG_CLR, regs->dp_fog_clr );
		sarea_priv->dirty &= ~MACH64_UPLOAD_DP_FOG_CLR;
	}
	if ( dirty & MACH64_UPLOAD_DP_WRITE_MASK ) {
		DMAOUTREG( MACH64_DP_WRITE_MASK, regs->dp_write_mask );
		sarea_priv->dirty &= ~MACH64_UPLOAD_DP_WRITE_MASK;
	}
	if ( dirty & MACH64_UPLOAD_DP_PIX_WIDTH ) {
		DMAOUTREG( MACH64_DP_PIX_WIDTH, regs->dp_pix_width );
		sarea_priv->dirty &= ~MACH64_UPLOAD_DP_PIX_WIDTH;
	}
	if ( dirty & MACH64_UPLOAD_SETUP_CNTL ) {
		DMAOUTREG( MACH64_SETUP_CNTL, regs->setup_cntl );
		sarea_priv->dirty &= ~MACH64_UPLOAD_SETUP_CNTL;
	}

	if ( dirty & MACH64_UPLOAD_TEXTURE ) {
		DMAOUTREG( MACH64_TEX_SIZE_PITCH, regs->tex_size_pitch );
		DMAOUTREG( MACH64_TEX_CNTL, regs->tex_cntl );
		DMAOUTREG( MACH64_SECONDARY_TEX_OFF, regs->secondary_tex_off );
		DMAOUTREG( MACH64_TEX_0_OFF + offset, regs->tex_offset );
		sarea_priv->dirty &= ~MACH64_UPLOAD_TEXTURE;
	}

#if 0
	/* FIXME: move to emit_cliprects and use hardware scissors for cliprects with
	 * vertex dispatches? 
	 */
	if ( dirty & MACH64_UPLOAD_CLIPRECTS ) {
		DMAOUTREG( MACH64_SC_LEFT_RIGHT, regs->sc_left_right );
		DMAOUTREG( MACH64_SC_TOP_BOTTOM, regs->sc_top_bottom );
		sarea_priv->dirty &= ~MACH64_UPLOAD_CLIPRECTS;
	}
#endif

	DMAADVANCE( dev_priv );
	
	sarea_priv->dirty = 0;

	return 0;

}


/* ================================================================
 * DMA command dispatch functions
 */

static int mach64_dma_dispatch_clear( drm_device_t *dev,
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
		return -EINVAL;
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
		return -EINVAL;
	}

	DMAGETPTR( dev_priv, 100 ); /* returns on failure to get buffer */

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
			DMAOUTREG( MACH64_DP_WRITE_MASK, ctx->dp_write_mask );
			DMAOUTREG( MACH64_DP_MIX, (MACH64_BKGD_MIX_D |
						   MACH64_FRGD_MIX_S) );
			DMAOUTREG( MACH64_DP_SRC, (MACH64_BKGD_SRC_FRGD_CLR |
						   MACH64_FRGD_SRC_FRGD_CLR |
						   MACH64_MONO_SRC_ONE) );
			
						
		}

		if ( flags & MACH64_FRONT ) {
						
			DMAOUTREG( MACH64_DST_OFF_PITCH,
				   dev_priv->front_offset_pitch );
			DMAOUTREG( MACH64_DST_X_Y,
				   (y << 16) | x );
			DMAOUTREG( MACH64_DST_WIDTH_HEIGHT,
				   (h << 16) | w );
			
		}
		
		if ( flags & MACH64_BACK ) {
						
			DMAOUTREG( MACH64_DST_OFF_PITCH,
				   dev_priv->back_offset_pitch );
			DMAOUTREG( MACH64_DST_X_Y,
				   (y << 16) | x );
			DMAOUTREG( MACH64_DST_WIDTH_HEIGHT,
				   (h << 16) | w );
			
		}
				
		if ( flags & MACH64_DEPTH ) {
			/* Setup for depth buffer clear
			 */
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
		}
	}

	DMAADVANCE( dev_priv );

	return 0;
}

static int mach64_dma_dispatch_swap( drm_device_t *dev )
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

	DMAGETPTR( dev_priv, 13 + nbox * 4 ); /* returns on failure to get buffer */

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

	for ( i = 0 ; i < nbox ; i++ ) {
		int x = pbox[i].x1;
		int y = pbox[i].y1;
		int w = pbox[i].x2 - x;
		int h = pbox[i].y2 - y;

		DRM_DEBUG( "dispatch swap %d,%d-%d,%d\n",
			  pbox[i].x1, pbox[i].y1,
			  pbox[i].x2, pbox[i].y2 );

		DMAOUTREG( MACH64_SRC_WIDTH1, w );
		DMAOUTREG( MACH64_SRC_Y_X, (x << 16) | y );
		DMAOUTREG( MACH64_DST_Y_X, (x << 16) | y );
		DMAOUTREG( MACH64_DST_WIDTH_HEIGHT, (h << 16) | w );

	}

#if MACH64_USE_FRAME_AGING
	/* Increment the frame counter.  The client-side 3D driver must
	 * throttle the framerate by waiting for this value before
	 * performing the swapbuffer ioctl.
	 */
	dev_priv->sarea_priv->last_frame++;

	DMAOUTREG( MACH64_LAST_FRAME_REG, dev_priv->sarea_priv->last_frame );
#endif
	DMAADVANCE( dev_priv );

	/*  DMAFLUSH( dev_priv ); */
	return 0;
}

static int mach64_dma_dispatch_vertex( drm_device_t *dev, drm_buf_t *buf, 
				       int prim, int discard )
{
	drm_mach64_private_t *dev_priv = dev->dev_private;
	drm_mach64_sarea_t *sarea_priv = dev_priv->sarea_priv;
	/* Don't need DMALOCALS, since buf is a parameter */

	DRM_DEBUG( "%s: buf=%d nbox=%d\n",
		   __FUNCTION__, buf->idx, sarea_priv->nbox );

	if ( buf->used ) {
		if ( sarea_priv->dirty & ~MACH64_UPLOAD_CLIPRECTS ) {
			int ret = 0;
			ret = mach64_emit_state( dev_priv );
			if (ret < 0) return ret;
		}
		/* FIXME: deal with cliprects, at least for drawing on the front buffer */
		/* Mach64 doesn't have hardware cliprects, just one hardware scissor */
#if 0
		do {
			int i = 0;
			/* Emit the next set of up to three cliprects */
			if ( i < sarea_priv->nbox ) {
				mach64_emit_clip_rects( dev_priv,
							&sarea_priv->boxes[i]);
			}
#endif
			/* Add the buffer to the DMA queue */
			DMAADVANCE( dev_priv );
#if 0
		} while ( ++i < sarea_priv->nbox );
#endif
	}

	sarea_priv->dirty &= ~MACH64_UPLOAD_CLIPRECTS;
	sarea_priv->nbox = 0;

	return 0;
}


static int mach64_dma_dispatch_blit( drm_device_t *dev,
				     drm_mach64_blit_t *blit )
{
	drm_mach64_private_t *dev_priv = dev->dev_private;
	drm_device_dma_t *dma = dev->dma;
	int dword_shift, dwords;
	DMALOCALS; /* declares buf=NULL, p, outcount=0 */

	/* The compiler won't optimize away a division by a variable,
	 * even if the only legal values are powers of two.  Thus, we'll
	 * use a shift instead.
	 */
	switch ( blit->format ) {
	case MACH64_DATATYPE_ARGB8888:
		dword_shift = 0;
		break;
	case MACH64_DATATYPE_ARGB1555:
	case MACH64_DATATYPE_RGB565:
	case MACH64_DATATYPE_ARGB4444:
		dword_shift = 1;
		break;
	case MACH64_DATATYPE_CI8:
	case MACH64_DATATYPE_RGB8:
		dword_shift = 2;
		break;
	default:
		DRM_ERROR( "invalid blit format %d\n", blit->format );
		return -EINVAL;
	}

	/* Dispatch the blit buffer.
	 * We don't need DMAGETPTR, since we already have one
	 */
	buf = dma->buflist[blit->idx];
	
	if ( buf->pid != current->pid ) {
		DRM_ERROR( "process %d using buffer owned by %d\n",
			   current->pid, buf->pid );
		return -EINVAL;
	}

	if ( buf->pending ) {
		DRM_ERROR( "sending pending buffer %d\n", blit->idx );
		return -EINVAL;
	}

	p = GETBUFPTR( buf );

	dwords =  (blit->width * blit->height) >> dword_shift;
	/* Add in a command for every 16 dwords */
	dwords += ( ( dwords + 15 ) / 16 );
	buf->used = dwords << 2;

	/* Blit via the host data registers (gui-master)
	 * Add state setup at the start of the buffer -- 
	 * the client leaves space for this based on MACH64_HOSTDATA_BLIT_OFFSET
	 */
	DMAOUTREG( MACH64_Z_CNTL, 0 );
	DMAOUTREG( MACH64_SCALE_3D_CNTL, 0 );

	DMAOUTREG( MACH64_SC_LEFT_RIGHT, 0 | ( 8191 << 16 ) );	/* no scissor */
	DMAOUTREG( MACH64_SC_TOP_BOTTOM, 0 | ( 16383 << 16 )  );

	DMAOUTREG( MACH64_CLR_CMP_CNTL, 0 );			/* disable */
	DMAOUTREG( MACH64_GUI_TRAJ_CNTL, 
		   MACH64_DST_X_LEFT_TO_RIGHT 
		   | MACH64_DST_Y_TOP_TO_BOTTOM );

	DMAOUTREG( MACH64_DP_PIX_WIDTH,
		   ( blit->format << 0 )			/* dst pix width */
		   | ( blit->format << 4 )			/* composite pix width */
		   | ( blit->format << 8 )			/* src pix width */
		   | ( blit->format << 16 )			/* host data pix width */
		   | ( blit->format << 28 )			/* scaler/3D pix width */
		   );

	DMAOUTREG( MACH64_DP_WRITE_MASK, 0xffffffff );		/* enable all planes */
	DMAOUTREG( MACH64_DP_MIX, 
		   MACH64_BKGD_MIX_D 
		   | MACH64_FRGD_MIX_S );
	DMAOUTREG( MACH64_DP_SRC, 
		   MACH64_BKGD_SRC_BKGD_CLR 
		   | MACH64_FRGD_SRC_HOST 
		   | MACH64_MONO_SRC_ONE );

	DMAOUTREG( MACH64_DST_OFF_PITCH, (blit->pitch << 22) | (blit->offset >> 3) );
	DMAOUTREG( MACH64_DST_X_Y, (blit->y << 16) | blit->x );
	DMAOUTREG( MACH64_DST_WIDTH_HEIGHT, (blit->height << 16) | blit->width );

	DRM_DEBUG( "%s: %d bytes\n", __FUNCTION__, buf->used );

	/* Add the buffer to the queue */
	DMAADVANCE( dev_priv );
	
	dev_priv->sarea_priv->dirty |= (MACH64_UPLOAD_CONTEXT |
					MACH64_UPLOAD_MISC);

	return 0;
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
	int ret;

	DRM_DEBUG( "%s: pid=%d\n", __FUNCTION__, current->pid  );

	LOCK_TEST_WITH_RETURN( dev );
	
	if ( copy_from_user( &clear, (drm_mach64_clear_t *) arg,
			     sizeof(clear) ) )
		return -EFAULT;

	VB_AGE_TEST_WITH_RETURN( dev_priv );
	RING_SPACE_TEST_WITH_RETURN( dev_priv );
	
	if ( sarea_priv->nbox > MACH64_NR_SAREA_CLIPRECTS )
		sarea_priv->nbox = MACH64_NR_SAREA_CLIPRECTS;

	ret = mach64_dma_dispatch_clear( dev, clear.flags,
				   clear.x, clear.y, clear.w, clear.h,
				   clear.clear_color, clear.clear_depth );

	/* Make sure we restore the 3D state next time.
	 */
	sarea_priv->dirty |= (MACH64_UPLOAD_CONTEXT |
			      MACH64_UPLOAD_MISC);
	return ret;
}

int mach64_dma_swap( struct inode *inode, struct file *filp,
		     unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_mach64_private_t *dev_priv = dev->dev_private;
	drm_mach64_sarea_t *sarea_priv = dev_priv->sarea_priv;
	int ret;

	DRM_DEBUG( "%s: pid=%d\n", __FUNCTION__, current->pid );

	LOCK_TEST_WITH_RETURN( dev );

	VB_AGE_TEST_WITH_RETURN( dev_priv );
	RING_SPACE_TEST_WITH_RETURN( dev_priv );
	
	if ( sarea_priv->nbox > MACH64_NR_SAREA_CLIPRECTS )
		sarea_priv->nbox = MACH64_NR_SAREA_CLIPRECTS;

	ret = mach64_dma_dispatch_swap( dev );

	/* Make sure we restore the 3D state next time.
	 */
	sarea_priv->dirty |= (MACH64_UPLOAD_CONTEXT |
			      MACH64_UPLOAD_MISC);
	return ret;
}

int mach64_dma_vertex( struct inode *inode, struct file *filp,
		       unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_mach64_private_t *dev_priv = dev->dev_private;
	drm_device_dma_t *dma = dev->dma;
	drm_buf_t *buf;
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

	if ( vertex.idx < 0 || vertex.idx >= dma->buf_count ) {
		DRM_ERROR( "buffer index %d (of %d max)\n",
			   vertex.idx, dma->buf_count - 1 );
		return -EINVAL;
	}
#if 0
	if ( vertex.prim < 0 ||
	     vertex.prim > R128_CCE_VC_CNTL_PRIM_TYPE_TRI_TYPE2 ) {
		DRM_ERROR( "buffer prim %d\n", vertex.prim );
		return -EINVAL;
	}
#endif
	VB_AGE_TEST_WITH_RETURN( dev_priv );
	RING_SPACE_TEST_WITH_RETURN( dev_priv );
		
	buf = dma->buflist[vertex.idx];
	
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

	return mach64_dma_dispatch_vertex( dev, buf, vertex.prim, vertex.discard );
}

int mach64_dma_blit( struct inode *inode, struct file *filp,
		     unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_device_dma_t *dma = dev->dma;
	drm_mach64_private_t *dev_priv = dev->dev_private;
	drm_mach64_blit_t blit;

	LOCK_TEST_WITH_RETURN( dev );

	if ( copy_from_user( &blit, (drm_mach64_blit_t *)arg,
			     sizeof(blit) ) )
		return -EFAULT;

	DRM_DEBUG( "%s: pid=%d index=%d\n",
		   __FUNCTION__, current->pid, blit.idx );

	if ( blit.idx < 0 || blit.idx >= dma->buf_count ) {
		DRM_ERROR( "buffer index %d (of %d max)\n",
			   blit.idx, dma->buf_count - 1 );
		return -EINVAL;
	}

	VB_AGE_TEST_WITH_RETURN( dev_priv );
	RING_SPACE_TEST_WITH_RETURN( dev_priv );

	return mach64_dma_dispatch_blit( dev, &blit );

}
