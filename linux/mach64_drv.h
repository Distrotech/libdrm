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
	drm_mach64_sarea_t *sarea_priv;

	unsigned int fb_bpp;
	unsigned int front_offset, front_pitch;
	unsigned int back_offset, back_pitch;

	unsigned int depth_bpp;
	unsigned int depth_offset, depth_pitch;

	u32 front_offset_pitch;
	u32 back_offset_pitch;
	u32 depth_offset_pitch;

	int usec_timeout;

	drm_map_t *sarea;
	drm_map_t *fb;
	drm_map_t *mmio;
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

				/* mach64_context.c */
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

				/* mach64_dma.c */
extern int mach64_dma_init( struct inode *inode, struct file *filp,
			    unsigned int cmd, unsigned long arg );
extern int mach64_dma_idle( struct inode *inode, struct file *filp,
			    unsigned int cmd, unsigned long arg );

				/* r128_state.c */
extern int mach64_dma_clear( struct inode *inode, struct file *filp,
			     unsigned int cmd, unsigned long arg );
extern int mach64_dma_swap( struct inode *inode, struct file *filp,
			    unsigned int cmd, unsigned long arg );


/* ================================================================
 * Registers
 */

#define MACH64_ALPHA_TST_CNTL 			0x0550

#define MACH64_BUS_CNTL 			0x04a0
#	define MACH64_BUS_MSTR_RESET			(1 << 1)
#	define MACH64_BUS_FLUSH_BUF			(1 << 2)
#	define MACH64_BUS_MASTER_DIS			(1 << 6)
#	define MACH64_BUS_EXT_REG_EN			(1 << 27)

#define MACH64_COMPOSITE_SHADOW_ID 		0x0798
#define MACH64_CONTEXT_LOAD_CNTL 		0x072c
#define MACH64_CONTEXT_MASK 			0x0720

#define MACH64_CLR_CMP_CLR			0x0700
#define MACH64_CLR_CMP_CNTL			0x0708
#define MACH64_CLR_CMP_MASK			0x0704

#define MACH64_DP_BKGD_CLR			0x06c0
#define MACH64_DP_FOG_CLR			0x06c4
#define MACH64_DP_FGRD_BKGD_CLR			0x06e0
#define MACH64_DP_FRGD_CLR			0x06c4
#define MACH64_DP_FGRD_CLR_MIX			0x06dc

#define MACH64_DP_MIX				0x06d4
#	define BKGD_MIX_NOT_D				(0 << 0)
#	define BKGD_MIX_ZERO				(1 << 0)
#	define BKGD_MIX_ONE				(2 << 0)
#	define MACH64_BKGD_MIX_D			(3 << 0)
#	define BKGD_MIX_NOT_S				(4 << 0)
#	define BKGD_MIX_D_XOR_S				(5 << 0)
#	define BKGD_MIX_NOT_D_XOR_S			(6 << 0)
#	define BKGD_MIX_S				(7 << 0)
#	define BKGD_MIX_NOT_D_OR_NOT_S			(8 << 0)
#	define BKGD_MIX_D_OR_NOT_S			(9 << 0)
#	define BKGD_MIX_NOT_D_OR_S			(10 << 0)
#	define BKGD_MIX_D_OR_S				(11 << 0)
#	define BKGD_MIX_D_AND_S				(12 << 0)
#	define BKGD_MIX_NOT_D_AND_S			(13 << 0)
#	define BKGD_MIX_D_AND_NOT_S			(14 << 0)
#	define BKGD_MIX_NOT_D_AND_NOT_S			(15 << 0)
#	define BKGD_MIX_D_PLUS_S_DIV2			(23 << 0)
#	define FRGD_MIX_NOT_D				(0 << 16)
#	define FRGD_MIX_ZERO				(1 << 16)
#	define FRGD_MIX_ONE				(2 << 16)
#	define FRGD_MIX_D				(3 << 16)
#	define FRGD_MIX_NOT_S				(4 << 16)
#	define FRGD_MIX_D_XOR_S				(5 << 16)
#	define FRGD_MIX_NOT_D_XOR_S			(6 << 16)
#	define MACH64_FRGD_MIX_S			(7 << 16)
#	define FRGD_MIX_NOT_D_OR_NOT_S			(8 << 16)
#	define FRGD_MIX_D_OR_NOT_S			(9 << 16)
#	define FRGD_MIX_NOT_D_OR_S			(10 << 16)
#	define FRGD_MIX_D_OR_S				(11 << 16)
#	define FRGD_MIX_D_AND_S				(12 << 16)
#	define FRGD_MIX_NOT_D_AND_S			(13 << 16)
#	define FRGD_MIX_D_AND_NOT_S			(14 << 16)
#	define FRGD_MIX_NOT_D_AND_NOT_S			(15 << 16)
#	define FRGD_MIX_D_PLUS_S_DIV2			(23 << 16)

#define MACH64_DP_PIX_WIDTH			0x06d0
#	define MACH64_HOST_TRIPLE_ENABLE		(1 << 13)
#	define MACH64_BYTE_ORDER_MSB_TO_LSB		(0 << 24)
#	define MACH64_BYTE_ORDER_LSB_TO_MSB		(1 << 24)

#define MACH64_DP_SRC				0x06d8
#	define MACH64_BKGD_SRC_BKGD_CLR			(0 << 0)
#	define MACH64_BKGD_SRC_FRGD_CLR			(1 << 0)
#	define MACH64_BKGD_SRC_HOST			(2 << 0)
#	define MACH64_BKGD_SRC_BLIT			(3 << 0)
#	define MACH64_BKGD_SRC_PATTERN			(4 << 0)
#	define MACH64_BKGD_SRC_3D			(5 << 0)
#	define MACH64_FRGD_SRC_BKGD_CLR			(0 << 8)
#	define MACH64_FRGD_SRC_FRGD_CLR			(1 << 8)
#	define MACH64_FRGD_SRC_HOST			(2 << 8)
#	define MACH64_FRGD_SRC_BLIT			(3 << 8)
#	define MACH64_FRGD_SRC_PATTERN			(4 << 8)
#	define MACH64_FRGD_SRC_3D			(5 << 8)
#	define MACH64_MONO_SRC_ONE			(0 << 16)
#	define MACH64_MONO_SRC_PATTERN			(1 << 16)
#	define MACH64_MONO_SRC_HOST			(2 << 16)
#	define MACH64_MONO_SRC_BLIT			(3 << 16)

#define MACH64_DP_WRITE_MASK			0x06c8

#define MACH64_DST_CNTL 			0x0530
#	define MACH64_DST_X_RIGHT_TO_LEFT		(0 << 0)
#	define MACH64_DST_X_LEFT_TO_RIGHT		(1 << 0)
#	define MACH64_DST_Y_BOTTOM_TO_TOP		(0 << 1)
#	define MACH64_DST_Y_TOP_TO_BOTTOM		(1 << 1)
#	define MACH64_DST_X_MAJOR			(0 << 2)
#	define MACH64_DST_Y_MAJOR			(1 << 2)
#	define MACH64_DST_X_TILE			(1 << 3)
#	define MACH64_DST_Y_TILE			(1 << 4)
#	define MACH64_DST_LAST_PEL			(1 << 5)
#	define MACH64_DST_POLYGON_ENABLE		(1 << 6)
#	define MACH64_DST_24_ROTATION_ENABLE		(1 << 7)

#define MACH64_DST_HEIGHT_WIDTH 		0x0518
#define MACH64_DST_OFF_PITCH 			0x0500
#define MACH64_DST_WIDTH_HEIGHT 		0x06ec
#define MACH64_DST_X_Y 				0x06e8
#define MACH64_DST_Y_X 				0x050c

#define MACH64_FIFO_STAT			0x0710
#	define MACH64_FIFO_SLOT_MASK			0x0000ffff
#	define MACH64_FIFO_ERR				(1 << 31)

#define MACH64_GUI_CMDFIFO_DEBUG 		0x0170
#define MACH64_GUI_CMDFIFO_DATA 		0x0174
#define MACH64_GUI_CNTL 			0x0178
#define MACH64_GUI_STAT				0x0738
#	define MACH64_GUI_ACTIVE			(1 << 0)
#define MACH64_GUI_TRAJ_CNTL 			0x0730
#define MACH64_HOST_CNTL 			0x0640
#define MACH64_HOST_DATA0 			0x0600

#define MACH64_ONE_OVER_AREA 			0x029c
#define MACH64_ONE_OVER_AREA_UC 		0x0300

#define MACH64_SCALE_3D_CNTL 			0x05fc
#define MACH64_SCRATCH_REG0			0x0480
#define MACH64_SCRATCH_REG1			0x0484
#define MACH64_SETUP_CNTL 			0x0304
#define MACH64_SRC_CNTL 			0x05b4
#define MACH64_SRC_HEIGHT1 			0x0594
#define MACH64_SRC_HEIGHT2 			0x05ac
#define MACH64_SRC_HEIGHT1_WIDTH1 		0x0598
#define MACH64_SRC_HEIGHT2_WIDTH2 		0x05b0
#define MACH64_SRC_OFF_PITCH 			0x0580
#define MACH64_SRC_WIDTH1 			0x0590
#define MACH64_SRC_Y_X 				0x058c

#define MACH64_VERTEX_1_ARGB 			0x0254
#define MACH64_VERTEX_1_S 			0x0240
#define MACH64_VERTEX_1_SECONDARY_S 		0x0328
#define MACH64_VERTEX_1_SECONDARY_T 		0x032c
#define MACH64_VERTEX_1_SECONDARY_W 		0x0330
#define MACH64_VERTEX_1_SPEC_ARGB 		0x024c
#define MACH64_VERTEX_1_T 			0x0244
#define MACH64_VERTEX_1_W 			0x0248
#define MACH64_VERTEX_1_X_Y 			0x0258
#define MACH64_VERTEX_1_Z 			0x0250
#define MACH64_VERTEX_2_ARGB 			0x0274
#define MACH64_VERTEX_2_S 			0x0260
#define MACH64_VERTEX_2_SECONDARY_S 		0x0334
#define MACH64_VERTEX_2_SECONDARY_T 		0x0338
#define MACH64_VERTEX_2_SECONDARY_W 		0x033c
#define MACH64_VERTEX_2_SPEC_ARGB 		0x026c
#define MACH64_VERTEX_2_T 			0x0264
#define MACH64_VERTEX_2_W 			0x0268
#define MACH64_VERTEX_2_X_Y 			0x0278
#define MACH64_VERTEX_2_Z 			0x0270
#define MACH64_VERTEX_3_ARGB 			0x0294
#define MACH64_VERTEX_3_S 			0x0280
#define MACH64_VERTEX_3_SECONDARY_S 		0x02a0
#define MACH64_VERTEX_3_SECONDARY_T 		0x02a4
#define MACH64_VERTEX_3_SECONDARY_W 		0x02a8
#define MACH64_VERTEX_3_SPEC_ARGB 		0x028c
#define MACH64_VERTEX_3_T 			0x0284
#define MACH64_VERTEX_3_W 			0x0288
#define MACH64_VERTEX_3_X_Y 			0x0298
#define MACH64_VERTEX_3_Z 			0x0290

#define MACH64_Z_CNTL 				0x054c
#define MACH64_Z_OFF_PITCH 			0x0548



#define MACH64_DATATYPE_CI8				2
#define MACH64_DATATYPE_ARGB1555			3
#define MACH64_DATATYPE_RGB565				4
#define MACH64_DATATYPE_ARGB8888			6
#define MACH64_DATATYPE_RGB332				7
#define MACH64_DATATYPE_RGB8				9
#define MACH64_DATATYPE_ARGB4444			15



#define MACH64_BASE(reg)	((u32)(dev_priv->mmio->handle))
#define MACH64_ADDR(reg)	(MACH64_BASE(reg) + reg)

#define MACH64_DEREF(reg)	*(__volatile__ u32 *)MACH64_ADDR(reg)
#define MACH64_READ(reg)	MACH64_DEREF(reg)
#define MACH64_WRITE(reg,val)	do { MACH64_DEREF(reg) = val; } while (0)



#define MACH64_VERBOSE		0

#define DMALOCALS

#define DMAGETPTR( dev_priv, n )					\
do {									\
	if ( MACH64_VERBOSE ) {						\
		DRM_INFO( "DMAGETPTR( %d ) in %s\n",			\
			  n, __FUNCTION__ );				\
	}								\
	mach64_do_wait_for_fifo( dev_priv, n );				\
} while (0)

#define DMAOUTREG( reg, val )						\
do {									\
	if ( MACH64_VERBOSE ) {						\
		DRM_INFO( "   DMAOUTREG( 0x%x = 0x%08x )\n",		\
			  reg, val );					\
	}								\
	MACH64_WRITE( reg, val );					\
} while (0)

#define DMAADVANCE( dev_priv )						\
do {									\
	if ( MACH64_VERBOSE ) {						\
		DRM_INFO( "DMAADVANCE() in %s\n", __FUNCTION__ );	\
	}								\
} while (0)

#endif
