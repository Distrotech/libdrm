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

typedef struct drm_mach64_freelist {
   	unsigned int age;
   	drm_buf_t *buf;
   	struct drm_mach64_freelist *next;
   	struct drm_mach64_freelist *prev;
} drm_mach64_freelist_t;

typedef struct drm_mach64_private {
	drm_mach64_sarea_t *sarea_priv;

   	drm_mach64_freelist_t *head;
   	drm_mach64_freelist_t *tail;

	int usec_timeout;
	int is_pci;

	struct pci_pool *pool;
	dma_addr_t table_handle;
	void *cpu_addr_table;
	u32 table_addr;

	unsigned int fb_bpp;
	unsigned int front_offset, front_pitch;
	unsigned int back_offset, back_pitch;

	unsigned int depth_bpp;
	unsigned int depth_offset, depth_pitch;

	u32 front_offset_pitch;
	u32 back_offset_pitch;
	u32 depth_offset_pitch;

	drm_map_t *sarea;
	drm_map_t *fb;
	drm_map_t *mmio;
	drm_map_t *buffers;
	drm_map_t *agp_textures;
} drm_mach64_private_t;

typedef struct drm_mach64_buf_priv {
	u32 age;
	int prim;
	int discard;
	int dispatched;
   	drm_mach64_freelist_t *list_entry;
} drm_mach64_buf_priv_t;

				/* mach64_dma.c */
extern int mach64_dma_init( struct inode *inode, struct file *filp,
			    unsigned int cmd, unsigned long arg );
extern int mach64_dma_idle( struct inode *inode, struct file *filp,
			    unsigned int cmd, unsigned long arg );
extern int mach64_engine_reset( struct inode *inode, struct file *filp,
				unsigned int cmd, unsigned long arg );
extern int mach64_dma_buffers( struct inode *inode, struct file *filp,
			       unsigned int cmd, unsigned long arg );

extern void mach64_freelist_reset( drm_device_t *dev );
extern drm_buf_t *mach64_freelist_get( drm_device_t *dev );

extern int mach64_do_wait_for_fifo( drm_mach64_private_t *dev_priv,
				    int entries );
extern int mach64_do_wait_for_idle( drm_mach64_private_t *dev_priv );
extern void mach64_dump_engine_info( drm_mach64_private_t *dev_priv );
extern int mach64_do_engine_reset( drm_device_t *dev );
extern int mach64_do_cleanup_dma( drm_device_t *dev );

				/* mach64_state.c */
extern int mach64_dma_clear( struct inode *inode, struct file *filp,
			     unsigned int cmd, unsigned long arg );
extern int mach64_dma_swap( struct inode *inode, struct file *filp,
			    unsigned int cmd, unsigned long arg );
extern int mach64_dma_vertex( struct inode *inode, struct file *filp,
			      unsigned int cmd, unsigned long arg );


/* ================================================================
 * Registers
 */

#define MACH64_AGP_BASE				0x0148
#define MACH64_AGP_CNTL				0x014c
#define MACH64_ALPHA_TST_CNTL			0x0550


#define MACH64_DSP_CONFIG 			0x0420
#define MACH64_DSP_ON_OFF 			0x0424
#define MACH64_EXT_MEM_CNTL 			0x04ac
#define MACH64_GEN_TEST_CNTL 			0x04d0
#define MACH64_HW_DEBUG 			0x047c
#define MACH64_MEM_ADDR_CONFIG 			0x0434
#define MACH64_MEM_BUF_CNTL 			0x042c
#define MACH64_MEM_CNTL 			0x04b0


#define MACH64_BM_ADDR				0x0648
#define MACH64_BM_COMMAND			0x0188
#define MACH64_BM_DATA				0x0648
#define MACH64_BM_FRAME_BUF_OFFSET		0x0180
#define MACH64_BM_GUI_TABLE			0x01b8
#define MACH64_BM_GUI_TABLE_CMD			0x064c
#	define MACH64_CIRCULAR_BUF_SIZE_16KB		(0 << 0)
#	define MACH64_CIRCULAR_BUF_SIZE_32KB		(1 << 0)
#	define MACH64_CIRCULAR_BUF_SIZE_64KB		(2 << 0)
#	define MACH64_CIRCULAR_BUF_SIZE_128KB		(3 << 0)
#	define MACH64_LAST_DESCRIPTOR			(1 << 31)
#define MACH64_BM_HOSTDATA			0x0644
#define MACH64_BM_STATUS			0x018c
#define MACH64_BM_SYSTEM_MEM_ADDR		0x0184
#define MACH64_BM_SYSTEM_TABLE			0x01bc
#define MACH64_BUS_CNTL				0x04a0
#	define MACH64_BUS_MSTR_RESET			(1 << 1)
#	define MACH64_BUS_APER_REG_DIS			(1 << 4)
#	define MACH64_BUS_FLUSH_BUF			(1 << 2)
#	define MACH64_BUS_MASTER_DIS			(1 << 6)
#	define MACH64_BUS_EXT_REG_EN			(1 << 27)

#define MACH64_CLR_CMP_CLR			0x0700
#define MACH64_CLR_CMP_CNTL			0x0708
#define MACH64_CLR_CMP_MASK			0x0704
#define MACH64_CONFIG_CHIP_ID 			0x04e0
#define MACH64_CONFIG_CNTL 			0x04dc
#define MACH64_CONFIG_STAT0 			0x04e4
#define MACH64_CONFIG_STAT1 			0x0494
#define MACH64_CONFIG_STAT2 			0x0498
#define MACH64_CONTEXT_LOAD_CNTL		0x072c
#define MACH64_CONTEXT_MASK			0x0720
#define MACH64_COMPOSITE_SHADOW_ID		0x0798
#define MACH64_CRC_SIG 				0x04e8
#define MACH64_CUSTOM_MACRO_CNTL 		0x04d4

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
#	define MACH64_BKGD_MIX_S			(7 << 0)
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

#define MACH64_DST_CNTL				0x0530
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

#define MACH64_DST_HEIGHT_WIDTH			0x0518
#define MACH64_DST_OFF_PITCH			0x0500
#define MACH64_DST_WIDTH_HEIGHT			0x06ec
#define MACH64_DST_X_Y				0x06e8
#define MACH64_DST_Y_X				0x050c

#define MACH64_FIFO_STAT			0x0710
#	define MACH64_FIFO_SLOT_MASK			0x0000ffff
#	define MACH64_FIFO_ERR				(1 << 31)

#define MACH64_GEN_TEST_CNTL			0x04d0
#	define MACH64_GUI_ENGINE_ENABLE			(1 << 8)
#define MACH64_GUI_CMDFIFO_DEBUG		0x0170
#define MACH64_GUI_CMDFIFO_DATA			0x0174
#define MACH64_GUI_CNTL				0x0178
#       define MACH64_CMDFIFO_SIZE_MASK                 0x00000003ul
#       define MACH64_CMDFIFO_SIZE_192                  0x00000000ul
#       define MACH64_CMDFIFO_SIZE_128                  0x00000001ul
#       define MACH64_CMDFIFO_SIZE_64                   0x00000002ul
#define MACH64_GUI_STAT				0x0738
#	define MACH64_GUI_ACTIVE			(1 << 0)
#define MACH64_GUI_TRAJ_CNTL			0x0730

#define MACH64_HOST_CNTL			0x0640
#define MACH64_HOST_DATA0			0x0600

#define MACH64_ONE_OVER_AREA			0x029c
#define MACH64_ONE_OVER_AREA_UC			0x0300

#define MACH64_PAT_REG0				0x0680

#define MACH64_SC_LEFT_RIGHT                    0x06a8
#define MACH64_SC_TOP_BOTTOM                    0x06b4

#define MACH64_SCALE_3D_CNTL			0x05fc
#define MACH64_SCRATCH_REG0			0x0480
#define MACH64_SCRATCH_REG1			0x0484
#define MACH64_SECONDARY_TEX_OFF		0x0778
#define MACH64_SETUP_CNTL			0x0304
#define MACH64_SRC_CNTL				0x05b4
#	define MACH64_SRC_BM_ENABLE			(1 << 8)
#	define MACH64_SRC_BM_SYNC			(1 << 9)
#	define MACH64_SRC_BM_OP_FRAME_TO_SYSTEM		(0 << 10)
#	define MACH64_SRC_BM_OP_SYSTEM_TO_FRAME		(1 << 10)
#	define MACH64_SRC_BM_OP_REG_TO_SYSTEM		(2 << 10)
#	define MACH64_SRC_BM_OP_SYSTEM_TO_REG		(3 << 10)
#define MACH64_SRC_HEIGHT1			0x0594
#define MACH64_SRC_HEIGHT2			0x05ac
#define MACH64_SRC_HEIGHT1_WIDTH1		0x0598
#define MACH64_SRC_HEIGHT2_WIDTH2		0x05b0
#define MACH64_SRC_OFF_PITCH			0x0580
#define MACH64_SRC_WIDTH1			0x0590
#define MACH64_SRC_Y_X				0x058c

#define MACH64_TEX_0_OFF			0x05c0
#define MACH64_TEX_CNTL				0x0774
#define MACH64_TEX_SIZE_PITCH			0x0770
#define MACH64_TIMER_CONFIG 			0x0428

#define MACH64_VERTEX_1_ARGB			0x0254
#define MACH64_VERTEX_1_S			0x0240
#define MACH64_VERTEX_1_SECONDARY_S		0x0328
#define MACH64_VERTEX_1_SECONDARY_T		0x032c
#define MACH64_VERTEX_1_SECONDARY_W		0x0330
#define MACH64_VERTEX_1_SPEC_ARGB		0x024c
#define MACH64_VERTEX_1_T			0x0244
#define MACH64_VERTEX_1_W			0x0248
#define MACH64_VERTEX_1_X_Y			0x0258
#define MACH64_VERTEX_1_Z			0x0250
#define MACH64_VERTEX_2_ARGB			0x0274
#define MACH64_VERTEX_2_S			0x0260
#define MACH64_VERTEX_2_SECONDARY_S		0x0334
#define MACH64_VERTEX_2_SECONDARY_T		0x0338
#define MACH64_VERTEX_2_SECONDARY_W		0x033c
#define MACH64_VERTEX_2_SPEC_ARGB		0x026c
#define MACH64_VERTEX_2_T			0x0264
#define MACH64_VERTEX_2_W			0x0268
#define MACH64_VERTEX_2_X_Y			0x0278
#define MACH64_VERTEX_2_Z			0x0270
#define MACH64_VERTEX_3_ARGB			0x0294
#define MACH64_VERTEX_3_S			0x0280
#define MACH64_VERTEX_3_SECONDARY_S		0x02a0
#define MACH64_VERTEX_3_SECONDARY_T		0x02a4
#define MACH64_VERTEX_3_SECONDARY_W		0x02a8
#define MACH64_VERTEX_3_SPEC_ARGB		0x028c
#define MACH64_VERTEX_3_T			0x0284
#define MACH64_VERTEX_3_W			0x0288
#define MACH64_VERTEX_3_X_Y			0x0298
#define MACH64_VERTEX_3_Z			0x0290

#define MACH64_Z_CNTL				0x054c
#define MACH64_Z_OFF_PITCH			0x0548



#define MACH64_DATATYPE_CI8				2
#define MACH64_DATATYPE_ARGB1555			3
#define MACH64_DATATYPE_RGB565				4
#define MACH64_DATATYPE_ARGB8888			6
#define MACH64_DATATYPE_RGB332				7
#define MACH64_DATATYPE_RGB8				9
#define MACH64_DATATYPE_ARGB4444			15

#define MACH64_CRTC_INT_CNTL     0x0418

/* Constants */
#define MACH64_LAST_FRAME_REG		MACH64_SCRATCH_REG0
#define MACH64_LAST_DISPATCH_REG	MACH64_SCRATCH_REG1


#define MACH64_BASE(reg)	((u32)(dev_priv->mmio->handle))

#define MACH64_ADDR(reg)	(MACH64_BASE(reg) + reg)

#define MACH64_DEREF(reg)	*(volatile u32 *)MACH64_ADDR(reg)
#define MACH64_READ(reg)	le32_to_cpu(MACH64_DEREF(reg))
#define MACH64_WRITE(reg,val)	do { MACH64_DEREF(reg) = cpu_to_le32(val); } while (0)


#define DWMREG0		0x0400
#define DWMREG0_END	0x07ff
#define DWMREG1		0x0000
#define DWMREG1_END	0x03ff

#define ISREG0(r)	(((r) >= DWMREG0) && ((r) <= DWMREG0_END))
#define DMAREG0(r)	(((r) - DWMREG0) >> 2)
#define DMAREG1(r)	((((r) - DWMREG1) >> 2 ) | 0x0100)
#define DMAREG(r)	(ISREG0(r) ? DMAREG0(r) : DMAREG1(r))

#define MMREG0		0x0000
#define MMREG0_END	0x00ff

#define ISMMREG0(r)	(((r) >= MMREG0) && ((r) <= MMREG0_END))
#define MMSELECT0(r)	(((r) << 2) + DWMREG0)
#define MMSELECT1(r)	(((((r) & 0xff) << 2) + DWMREG1))
#define MMSELECT(r)	(ISMMREG0(r) ? MMSELECT0(r) : MMSELECT1(r))

/* ================================================================
 * Misc helper macros
 */

#define LOCK_TEST_WITH_RETURN( dev )					\
do {									\
	if ( !_DRM_LOCK_IS_HELD( dev->lock.hw_lock->lock ) ||		\
	     dev->lock.pid != current->pid ) {				\
		DRM_ERROR( "%s called without lock held\n",		\
			   __FUNCTION__ );				\
		return -EINVAL;						\
	}								\
} while (0)

#define VB_AGE_TEST_WITH_RETURN( dev_priv )				\
do {									\
	drm_mach64_sarea_t *sarea_priv = dev_priv->sarea_priv;		\
	if ( sarea_priv->last_dispatch >= MACH64_MAX_VB_AGE ) {		\
		int __ret = mach64_do_dma_idle( dev_priv );		\
		if ( __ret < 0 ) return __ret;				\
		sarea_priv->last_dispatch = 0;				\
		mach64_freelist_reset( dev );				\
	}								\
} while (0)


/* ================================================================
 * DMA macros
 */

#define MACH64_USE_DMA		0

#define DMA_FRAME_BUF_OFFSET	0
#define DMA_SYS_MEM_ADDR	1
#define DMA_COMMAND		2
#define DMA_RESERVED		3

#define DMA_CHUNKSIZE		0x1000
#define APERTURE_OFFSET		0x7ff800


#define MACH64_VERBOSE		0

#define mach64_flush_write_combine()	mb()

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
