/* mach64_drv.h -- Private header for mach64 driver -*- linux-c -*-
 * Created: Fri Nov 24 22:07:58 2000 by gareth@valinux.com
 *
 * Copyright 2000 Gareth Hughes
 * Copyright 2002 Frank C. Earl
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
 *    Frank C. Earl <fearl@airmail.net>
 *    Leif Delgass <ldelgass@retinalburn.net>
 */

#ifndef __MACH64_DRV_H__
#define __MACH64_DRV_H__

#include <linux/delay.h>
#include <linux/list.h>

#ifndef list_for_each_safe
#define list_for_each_safe(pos, n, head) \
	for (pos = (head)->next, n = pos->next; pos != (head); \
		pos = n, n = pos->next)
#endif

/* Development driver options - FIXME: remove these when not needed */

#define MACH64_USE_BUFFER_AGING   0
#define MACH64_USE_FRAME_AGING    0
#define MACH64_NO_BATCH_DISPATCH  1
#define MACH64_EXTRA_CHECKING     1
#define MACH64_DEFAULT_MODE       MACH64_MODE_DMA_ASYNC


typedef struct drm_mach64_freelist {
	struct list_head  list;			/* Linux LIST structure... */
   	drm_buf_t *buf;
#if MACH64_USE_BUFFER_AGING
	unsigned int age;
#else
	u32 ring_ofs;     /* dword offset in ring of last descriptor for this buffer */
#endif
} drm_mach64_freelist_t;

typedef struct drm_mach64_descriptor_ring {
	dma_addr_t handle;
	void *start;        /* write pointer (cpu address) to start of descriptor ring */
	u32 start_addr;     /* bus address of beginning of descriptor ring */
       	int size;           /* size of ring in bytes */

	u32 head_addr;      /* bus address of descriptor ring head */
	u32 head;           /* dword offset of descriptor ring head */
	u32 tail;           /* dword offset of descriptor ring tail */
	u32 tail_mask;      /* mask used to wrap ring */
	int space;          /* number of free bytes in ring */

	int high_mark;      /* high water mark in bytes */
} drm_mach64_descriptor_ring_t;

typedef struct drm_mach64_private {
	drm_mach64_sarea_t *sarea_priv;

	int is_pci;
	enum {
		MACH64_MODE_MMIO,
		MACH64_MODE_DMA_SYNC,
		MACH64_MODE_DMA_ASYNC
	} driver_mode;

	int usec_timeout;      /* Number of microseconds to wait in the idle functions */

	atomic_t dma_timeout;  /* Number of interrupt dispatches since last DMA dispatch */
	atomic_t do_gui;       /* Flag for the bottom half to know what to do */
	atomic_t do_blit;      /* Flag for the bottom half to know what to do */

	int ring_running;
	
	/* DMA descriptor table (ring buffer) */
	struct pci_pool *pool;   /* DMA memory pool */
	drm_mach64_descriptor_ring_t ring;

	struct list_head free_list;     /* Free-list head  */
	struct list_head placeholders;  /* Free-list placeholder list  */
	struct list_head pending;    	/* Pending submission placeholder  */
	struct list_head dma_queue;     /* Submission queue head  */

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

				/* mach64_dma.c */
extern int mach64_dma_init( struct inode *inode, struct file *filp,
			    unsigned int cmd, unsigned long arg );
extern int mach64_dma_idle( struct inode *inode, struct file *filp,
			    unsigned int cmd, unsigned long arg );
extern int mach64_dma_flush( struct inode *inode, struct file *filp,
			    unsigned int cmd, unsigned long arg );
extern int mach64_engine_reset( struct inode *inode, struct file *filp,
				unsigned int cmd, unsigned long arg );
extern int mach64_dma_buffers( struct inode *inode, struct file *filp,
			       unsigned int cmd, unsigned long arg );

extern int mach64_init_freelist( drm_device_t *dev );
extern void mach64_destroy_freelist( drm_device_t *dev );
#ifdef MACH64_USE_BUFFER_AGING
extern void mach64_freelist_reset( drm_mach64_private_t *dev_priv );
#endif
extern drm_buf_t *mach64_freelist_get( drm_mach64_private_t *dev_priv );

extern int mach64_do_wait_for_fifo( drm_mach64_private_t *dev_priv,
				    int entries );
extern int mach64_do_wait_for_idle( drm_mach64_private_t *dev_priv );
extern int mach64_wait_ring( drm_mach64_private_t *dev_priv, int n );
extern int mach64_do_release_used_buffers( drm_mach64_private_t *dev_priv );
extern void mach64_dump_engine_info( drm_mach64_private_t *dev_priv );
extern void mach64_dump_ring_info( drm_mach64_private_t *dev_priv );
extern int mach64_do_engine_reset( drm_mach64_private_t *dev_priv );

extern int mach64_do_dma_idle( drm_mach64_private_t *dev_priv );
extern int mach64_do_dma_flush( drm_mach64_private_t *dev_priv );
extern int mach64_do_cleanup_dma( drm_device_t *dev );

				/* mach64_state.c */
extern int mach64_dma_clear( struct inode *inode, struct file *filp,
			     unsigned int cmd, unsigned long arg );
extern int mach64_dma_swap( struct inode *inode, struct file *filp,
			    unsigned int cmd, unsigned long arg );
extern int mach64_dma_vertex( struct inode *inode, struct file *filp,
			      unsigned int cmd, unsigned long arg );
extern int mach64_dma_blit( struct inode *inode, struct file *filp,
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
#define MACH64_PAT_REG1				0x0684

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

#define MACH64_CRTC_INT_CNTL			0x0418
#	define MACH64_CRTC_VBLANK_INT_EN		(1 << 1)
#	define MACH64_CRTC_VBLANK_INT			(1 << 2)
#	define MACH64_CRTC_VBLANK_INT_AK		(1 << 2)
#	define MACH64_CRTC_VLINE_INT_EN			(1 << 3)
#	define MACH64_CRTC_VLINE_INT			(1 << 4)
#	define MACH64_CRTC_VLINE_INT_AK			(1 << 4)
#	define MACH64_CRTC_VLINE_SYNC			(1 << 5)
#	define MACH64_CRTC_FRAME			(1 << 6)
#	define MACH64_CRTC_SNAPSHOT_INT_EN		(1 << 7)
#	define MACH64_CRTC_SNAPSHOT_INT			(1 << 8)
#	define MACH64_CRTC_SNAPSHOT_INT_AK		(1 << 8)
#	define MACH64_CRTC_BUSMASTER_EOL_INT_EN		(1 << 24)
#	define MACH64_CRTC_BUSMASTER_EOL_INT		(1 << 25)
#	define MACH64_CRTC_BUSMASTER_EOL_INT_AK		(1 << 25)
#	define MACH64_CRTC_GP_INT_EN			(1 << 26)
#	define MACH64_CRTC_GP_INT			(1 << 27)
#	define MACH64_CRTC_GP_INT_AK			(1 << 27)
#	define MACH64_CRTC_VBLANK2_INT			(1 << 31)
#	define MACH64_CRTC_VBLANK2_INT_AK		(1 << 31)

#define MACH64_DATATYPE_CI8				2
#define MACH64_DATATYPE_ARGB1555			3
#define MACH64_DATATYPE_RGB565				4
#define MACH64_DATATYPE_ARGB8888			6
#define MACH64_DATATYPE_RGB332				7
#define MACH64_DATATYPE_RGB8				9
#define MACH64_DATATYPE_ARGB4444			15

/* Constants */
#define MACH64_LAST_FRAME_REG			MACH64_PAT_REG0
#define MACH64_LAST_DISPATCH_REG		MACH64_PAT_REG1
#define MACH64_MAX_VB_AGE		0x7fffffff

#define MACH64_BASE(reg)	((u32)(dev_priv->mmio->handle))

#define MACH64_ADDR(reg)	(MACH64_BASE(reg) + reg)

#define MACH64_READ(reg)	readl(MACH64_ADDR(reg))
#define MACH64_WRITE(reg,val)	writel((val), MACH64_ADDR(reg))


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

static inline void mach64_ring_start( drm_mach64_private_t *dev_priv )
{
	drm_mach64_descriptor_ring_t *ring = &dev_priv->ring;
	
	DRM_DEBUG( "%s: head_addr: 0x%08x head: %d tail: %d space: %d\n",
		   __FUNCTION__, 
		   ring->head_addr, ring->head, ring->tail, ring->space );

	if ( mach64_do_wait_for_idle( dev_priv ) < 0 ) {
		mach64_do_engine_reset( dev_priv );
	}

	/* enable bus mastering and block 1 registers */
	MACH64_WRITE( MACH64_BUS_CNTL, 
		      ( MACH64_READ(MACH64_BUS_CNTL) & 	~MACH64_BUS_MASTER_DIS ) 
		      | MACH64_BUS_EXT_REG_EN );

	mach64_do_wait_for_idle( dev_priv );
	
	/* reset descriptor table ring head */
	MACH64_WRITE( MACH64_BM_GUI_TABLE_CMD, ring->head_addr | MACH64_CIRCULAR_BUF_SIZE_16KB );
	
	dev_priv->ring_running = 1;
}

static inline void mach64_ring_resume( drm_mach64_private_t *dev_priv, drm_mach64_descriptor_ring_t *ring )
{
	DRM_DEBUG( "%s: head_addr: 0x%08x head: %d tail: %d space: %d\n",
		   __FUNCTION__, 
		   ring->head_addr, ring->head, ring->tail, ring->space );

	/* reset descriptor table ring head */
	MACH64_WRITE( MACH64_BM_GUI_TABLE_CMD, ring->head_addr | MACH64_CIRCULAR_BUF_SIZE_16KB );
	
	/* enable GUI bus mastering, and sync the bus master to the GUI */
	MACH64_WRITE( MACH64_SRC_CNTL, 
		      MACH64_SRC_BM_ENABLE | MACH64_SRC_BM_SYNC |
		      MACH64_SRC_BM_OP_SYSTEM_TO_REG );

	/* kick off the transfer */
	MACH64_WRITE( MACH64_DST_HEIGHT_WIDTH, 0 );
}

static inline void mach64_ring_tick( drm_mach64_private_t *dev_priv, drm_mach64_descriptor_ring_t *ring )
{
	DRM_DEBUG( "%s: head_addr: 0x%08x head: %d tail: %d space: %d\n",
		   __FUNCTION__, 
		   ring->head_addr, ring->head, ring->tail, ring->space );

	if ( !dev_priv->ring_running ) {
		mach64_ring_start( dev_priv );
		
		if ( ring->head != ring->tail ) {
			mach64_ring_resume( dev_priv, ring );
		}
	} else {
		/* GUI_ACTIVE must be read before BM_GUI_TABLE to correctly determine the ring head */
		int gui_active = MACH64_READ(MACH64_GUI_STAT) & MACH64_GUI_ACTIVE;
		
		ring->head_addr = MACH64_READ(MACH64_BM_GUI_TABLE) & 0xfffffff0;
		
		if ( gui_active ) {
			/* If not idle, BM_GUI_TABLE points one descriptor past the current head */
			if ( ring->head_addr == ring->start_addr ) {
				ring->head_addr += ring->size;
			}
			ring->head_addr -= 4 * sizeof(u32);
		}

		if( ring->head_addr < ring->start_addr || ring->head_addr >= ring->start_addr + ring->size ) {
			DRM_ERROR( "bad ring head address: 0x%08x\n", ring->head_addr );
			mach64_dump_ring_info( dev_priv );
			mach64_do_engine_reset( dev_priv );
			return;
		}
	
		ring->head = (ring->head_addr - ring->start_addr) / sizeof(u32);
		
		if ( !gui_active && ring->head != ring->tail ) {
			mach64_ring_resume( dev_priv, ring );
		}
	}
}

static inline void mach64_ring_stop( drm_mach64_private_t *dev_priv )
{
	DRM_DEBUG( "%s: head_addr: 0x%08x head: %d tail: %d space: %d\n",
		   __FUNCTION__, 
		   dev_priv->ring.head_addr, dev_priv->ring.head, dev_priv->ring.tail, dev_priv->ring.space );

	/* restore previous SRC_CNTL to disable busmastering */
	mach64_do_wait_for_fifo( dev_priv, 1 );
	MACH64_WRITE( MACH64_SRC_CNTL, 0 );

	/* disable busmastering but keep the block 1 registers enabled */ 
	mach64_do_wait_for_idle( dev_priv );
	MACH64_WRITE( MACH64_BUS_CNTL, MACH64_READ( MACH64_BUS_CNTL ) | MACH64_BUS_MASTER_DIS | MACH64_BUS_EXT_REG_EN );
		
	dev_priv->ring_running = 0;
}

static inline void
mach64_update_ring_snapshot( drm_mach64_private_t *dev_priv )
{
	drm_mach64_descriptor_ring_t *ring = &dev_priv->ring;

	DRM_DEBUG( "%s\n", __FUNCTION__ );
	
	mach64_ring_tick( dev_priv, ring );

	ring->space = (ring->head - ring->tail) * sizeof(u32);
	if ( ring->space <= 0 ) {
		ring->space += ring->size;
	}
}

#define LOCK_TEST_WITH_RETURN( dev )					\
do {									\
	if ( !_DRM_LOCK_IS_HELD( dev->lock.hw_lock->lock ) ||		\
	     dev->lock.pid != current->pid ) {				\
		DRM_ERROR( "%s called without lock held\n",		\
			   __FUNCTION__ );				\
		return -EINVAL;						\
	}								\
} while (0)

/* FIXME: right now this is needed to ensure free buffers for state emits */
/* CHECKME: I've disabled this as it isn't necessary - we already wait for free buffers */
#define RING_SPACE_TEST_WITH_RETURN( dev_priv )						 

#define RING_SPACE_TEST_WITH_RETURN_( dev_priv )					\
do {											\
	drm_mach64_descriptor_ring_t *ring = &dev_priv->ring; int i;			\
	if ( ring->space < ring->high_mark ) {						\
		for ( i = 0 ; i < dev_priv->usec_timeout ; i++ ) {			\
			mach64_update_ring_snapshot( dev_priv );			\
			if ( ring->space >= ring->high_mark )				\
				goto __ring_space_done;					\
			udelay( 1 );							\
		}									\
		DRM_ERROR( "ring space check failed!\n" );				\
		DRM_INFO( "ring: head addr: 0x%08x head: %d tail: %d space: %d\n", 	\
			ring->head_addr, ring->head, ring->tail, ring->space );		\
		return -EBUSY;								\
	}										\
 __ring_space_done:									\
} while (0)

#if MACH64_USE_BUFFER_AGING

#define VB_AGE_TEST_WITH_RETURN( dev_priv )				\
do {									\
	drm_mach64_sarea_t *sarea_priv = dev_priv->sarea_priv;		\
	if ( sarea_priv->last_dispatch >= MACH64_MAX_VB_AGE ) {		\
		int __ret = mach64_do_dma_idle( dev_priv );		\
		if ( __ret < 0 ) return __ret;				\
		sarea_priv->last_dispatch = 0;				\
		mach64_freelist_reset( dev_priv );			\
	}								\
} while (0)

#else

#define VB_AGE_TEST_WITH_RETURN( dev_priv )

#endif

/* ================================================================
 * DMA constants
 */

#define DMA_FRAME_BUF_OFFSET	0
#define DMA_SYS_MEM_ADDR	1
#define DMA_COMMAND		2
#define DMA_RESERVED		3
#define DMA_HOLD_OFFSET         (1<<30)
#define DMA_EOL                 (1<<31)

#define MACH64_DMA_TIMEOUT      10          /* 10 vertical retraces should be enough */
#define MACH64_DMA_SIZE         96          /* Queue high water mark (number of buffers) */
#define DMA_CHUNKSIZE		0x1000      /* 4kB per DMA descriptor */
#define APERTURE_OFFSET		0x7ff800


#define MACH64_VERBOSE		0

#define mach64_flush_write_combine()	mb()

/* ================================================================
 * DMA descriptor ring macros
 */

#define RING_LOCALS	int tail, write; unsigned int mask; volatile u32 *ring

#define RING_WRITE_OFS write

#define BEGIN_RING( n ) 								\
do {											\
	if ( MACH64_VERBOSE ) {								\
		DRM_INFO( "BEGIN_RING( %d ) in %s\n",					\
			   (n), __FUNCTION__ );						\
	}										\
	if ( dev_priv->ring.space <= (n) * sizeof(u32) ) {				\
		int ret;								\
		if ((ret=mach64_wait_ring( dev_priv, (n) * sizeof(u32))) < 0 ) {	\
			DRM_ERROR( "wait_ring failed, resetting engine\n");		\
			mach64_dump_engine_info( dev_priv );				\
			mach64_do_engine_reset( dev_priv );				\
			return ret;							\
		}									\
	}										\
	dev_priv->ring.space -= (n) * sizeof(u32);					\
	ring = (u32 *) dev_priv->ring.start;						\
	tail = write = dev_priv->ring.tail;						\
	mask = dev_priv->ring.tail_mask;						\
} while (0)

#define OUT_RING( x )						\
do {								\
	if ( MACH64_VERBOSE ) {					\
		DRM_INFO( "   OUT_RING( 0x%08x ) at 0x%x\n",	\
			   (unsigned int)(x), write );		\
	}							\
	ring[write++] = cpu_to_le32( x );			\
	write &= mask;						\
} while (0)


static __inline__ void mach64_clear_dma_eol( volatile u32 * addr )
{
#if defined(__i386__)
	int nr = 31;
	
	/* Taken from include/asm-i386/bitops.h linux header */
        __asm__ __volatile__( "lock;"
                "btrl %1,%0"
                :"=m" (*addr)
                :"Ir" (nr));
#elif defined(__powerpc__)
	u32 old;
	u32 mask = cpu_to_le32( DMA_EOL );

	/* Taken from the include/asm-ppc/bitops.h linux header */
	__asm__ __volatile__("\n\
1:	lwarx	%0,0,%3 \n\
	andc	%0,%0,%2 \n\
	stwcx.	%0,0,%3 \n\
	bne-	1b"
	: "=&r" (old), "=m" (*addr)
	: "r" (mask), "r" (addr), "m" (*addr)
	: "cc");
#else
	u32 mask = cpu_to_le32( ~DMA_EOL );

	*addr &= mask;
#endif
}

#define ADVANCE_RING() 							\
do {									\
	if ( MACH64_VERBOSE ) {						\
		DRM_INFO( "ADVANCE_RING() wr=0x%06x tail=0x%06x\n",	\
			  write, tail );				\
	}								\
	mach64_flush_write_combine();					\
	mach64_clear_dma_eol( &ring[(tail - 2) & mask] );		\
	mach64_flush_write_combine();					\
	dev_priv->ring.tail = write;					\
	mach64_ring_tick( dev_priv, &(dev_priv)->ring );		\
} while (0)


/* ================================================================
 * DMA macros
 */

#define DMALOCALS  drm_buf_t *buf = NULL; u32 *p; int outcount = 0

#define GETBUFPTR( _buf )							\
((dev_priv->is_pci) ? ((u32 *)(_buf)->address) : ((u32 *)((char *)dev_priv->buffers->handle + (_buf)->offset)))

#define GETBUFADDR( _buf )							\
((dev_priv->is_pci) ? ((u32)virt_to_bus((void *)(_buf)->address)) : ((u32)(_buf)->bus_address))

/* FIXME: use a private set of smaller buffers for state emits, clears, and swaps? */
#define DMAGETPTR( dev_priv, n )					\
do {									\
	if ( MACH64_VERBOSE ) {						\
		DRM_INFO( "DMAGETPTR( %d ) in %s\n",			\
			  n, __FUNCTION__ );				\
	}								\
	buf = mach64_freelist_get( dev_priv );				\
	if (buf == NULL) {						\
		DRM_ERROR("%s: couldn't get buffer in DMAGETPTR\n",	\
			   __FUNCTION__ );				\
		return -EAGAIN;						\
	}								\
	buf->pid = current->pid;					\
	outcount = 0;							\
									\
        p = GETBUFPTR( buf );						\
} while (0)

#define DMAOUTREG( reg, val )					\
do {								\
	if ( MACH64_VERBOSE ) {					\
		DRM_INFO( "   DMAOUTREG( 0x%x = 0x%08x )\n",	\
			  reg, val );				\
	}							\
	p[outcount++] = cpu_to_le32(DMAREG(reg));		\
	p[outcount++] = cpu_to_le32((val));			\
	buf->used += 8;						\
} while (0)

#define DMAADVANCE( dev_priv )								\
do {											\
	struct list_head *ptr;								\
	drm_mach64_freelist_t *entry;							\
	RING_LOCALS;									\
											\
	if ( MACH64_VERBOSE ) {								\
		DRM_INFO( "DMAADVANCE() in %s\n", __FUNCTION__ );			\
	}										\
											\
	if (list_empty(&dev_priv->placeholders)) {					\
		DRM_ERROR( "%s: empty placeholder list in DMAADVANCE()\n",		\
			   __FUNCTION__ );						\
		return -EFAULT;								\
	}										\
											\
        ptr = dev_priv->placeholders.next;						\
	list_del(ptr);									\
	entry = list_entry(ptr, drm_mach64_freelist_t, list);				\
	entry->buf = buf;								\
	entry->buf->pending = 1;							\
	list_add_tail(ptr, &dev_priv->pending);						\
											\
	ADD_BUF_TO_RING( dev_priv, entry );						\
} while (0)

#define ADD_BUF_TO_RING( dev_priv, entry )						\
do {											\
	int bytes, pages, remainder;							\
	drm_buf_t *buf = entry->buf;							\
	u32 address, page;								\
	int i;										\
											\
	bytes = buf->used;								\
	address = GETBUFADDR( buf );							\
											\
	pages = (bytes + DMA_CHUNKSIZE - 1) / DMA_CHUNKSIZE;				\
											\
	BEGIN_RING( pages * 4 );							\
											\
	for ( i = 0 ; i < pages-1 ; i++ ) {						\
		page = address + i * DMA_CHUNKSIZE;					\
		OUT_RING( APERTURE_OFFSET + MACH64_BM_ADDR );				\
		OUT_RING( page );							\
		OUT_RING( DMA_CHUNKSIZE | DMA_HOLD_OFFSET );				\
		OUT_RING( 0 );								\
	}										\
											\
	/* generate the final descriptor for any remaining commands in this buffer */	\
	page = address + i * DMA_CHUNKSIZE;						\
	remainder = bytes - i * DMA_CHUNKSIZE;						\
											\
	/* Save dword offset of last descriptor for this buffer.			\
	 * This is needed to check for completion of the buffer in freelist_get		\
	 */										\
	entry->ring_ofs = RING_WRITE_OFS;						\
											\
	OUT_RING( APERTURE_OFFSET + MACH64_BM_ADDR );					\
	OUT_RING( page );								\
	OUT_RING( remainder | DMA_HOLD_OFFSET | DMA_EOL );				\
	OUT_RING( 0 );									\
											\
	ADVANCE_RING();									\
} while(0)

#define DMAADVANCEHOSTDATA( dev_priv )							\
do {											\
	struct list_head *ptr;								\
	drm_mach64_freelist_t *entry;							\
	RING_LOCALS;									\
											\
	if ( MACH64_VERBOSE ) {								\
		DRM_INFO( "DMAADVANCE() in %s\n", __FUNCTION__ );			\
	}										\
											\
	if (list_empty(&dev_priv->placeholders)) {					\
		DRM_ERROR( "%s: empty placeholder list in DMAADVANCE()\n",		\
			   __FUNCTION__ );						\
		return -EFAULT;								\
	}										\
											\
        ptr = dev_priv->placeholders.next;						\
	list_del(ptr);									\
	entry = list_entry(ptr, drm_mach64_freelist_t, list);				\
	entry->buf = buf;								\
	entry->buf->pending = 1;							\
	list_add_tail(ptr, &dev_priv->pending);						\
											\
	ADD_HOSTDATA_BUF_TO_RING( dev_priv, entry );					\
} while (0)

#define ADD_HOSTDATA_BUF_TO_RING( dev_priv, entry )					 \
do {											 \
	int bytes, pages, remainder;							 \
	drm_buf_t *buf = entry->buf;							 \
	u32 address, page;								 \
	int i;										 \
											 \
	bytes = buf->used - MACH64_HOSTDATA_BLIT_OFFSET;				 \
	pages = (bytes + DMA_CHUNKSIZE - 1) / DMA_CHUNKSIZE;				 \
	address = GETBUFADDR( buf );							 \
											 \
	BEGIN_RING( 4 + pages * 4 );							 \
											 \
	OUT_RING( APERTURE_OFFSET + MACH64_BM_ADDR );					 \
	OUT_RING( address );								 \
	OUT_RING( MACH64_HOSTDATA_BLIT_OFFSET | DMA_HOLD_OFFSET );			 \
	OUT_RING( 0 );									 \
											 \
	address += MACH64_HOSTDATA_BLIT_OFFSET;						 \
											 \
	for ( i = 0 ; i < pages-1 ; i++ ) {						 \
		page = address + i * DMA_CHUNKSIZE;					 \
		OUT_RING( APERTURE_OFFSET + MACH64_BM_HOSTDATA );			 \
		OUT_RING( page );							 \
		OUT_RING( DMA_CHUNKSIZE | DMA_HOLD_OFFSET );				 \
		OUT_RING( 0 );								 \
	}										 \
											 \
	/* generate the final descriptor for any remaining commands in this buffer */	 \
	page = address + i * DMA_CHUNKSIZE;						 \
	remainder = bytes - i * DMA_CHUNKSIZE;						 \
											 \
	/* Save dword offset of last descriptor for this buffer.			 \
	 * This is needed to check for completion of the buffer in freelist_get		 \
	 */										 \
	entry->ring_ofs = RING_WRITE_OFS;						 \
											 \
	OUT_RING( APERTURE_OFFSET + MACH64_BM_HOSTDATA );				 \
	OUT_RING( page );								 \
	OUT_RING( remainder | DMA_HOLD_OFFSET | DMA_EOL );				 \
	OUT_RING( 0 );									 \
											 \
	ADVANCE_RING();									 \
} while(0)

#endif /* __MACH64_DRV_H__ */
