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

	DRM_INFO( "%s failed! slots=%d entries=%d\n", __FUNCTION__, slots, entries );
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

	DRM_INFO( "%s failed! GUI_STAT=0x%08x\n", __FUNCTION__, 
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

void mach64_dump_engine_info( drm_mach64_private_t *dev_priv )
{
	DRM_INFO( "\n" );
	if ( !dev_priv->is_pci)
	{
		DRM_INFO( "           AGP_BASE = 0x%08x\n", MACH64_READ( MACH64_AGP_BASE ) );
		DRM_INFO( "           AGP_CNTL = 0x%08x\n", MACH64_READ( MACH64_AGP_CNTL ) );
	}
	DRM_INFO( "     ALPHA_TST_CNTL = 0x%08x\n", MACH64_READ( MACH64_ALPHA_TST_CNTL ) );
	DRM_INFO( "\n" );
	DRM_INFO( "         BM_COMMAND = 0x%08x\n", MACH64_READ( MACH64_BM_COMMAND ) );
	DRM_INFO( "BM_FRAME_BUF_OFFSET = 0x%08x\n", MACH64_READ( MACH64_BM_FRAME_BUF_OFFSET ) );
	DRM_INFO( "       BM_GUI_TABLE = 0x%08x\n", MACH64_READ( MACH64_BM_GUI_TABLE ) );
	DRM_INFO( "          BM_STATUS = 0x%08x\n", MACH64_READ( MACH64_BM_STATUS ) );
	DRM_INFO( " BM_SYSTEM_MEM_ADDR = 0x%08x\n", MACH64_READ( MACH64_BM_SYSTEM_MEM_ADDR ) );
	DRM_INFO( "    BM_SYSTEM_TABLE = 0x%08x\n", MACH64_READ( MACH64_BM_SYSTEM_TABLE ) );
	DRM_INFO( "           BUS_CNTL = 0x%08x\n", MACH64_READ( MACH64_BUS_CNTL ) );
	DRM_INFO( "\n" );
	/* DRM_INFO( "         CLOCK_CNTL = 0x%08x\n", MACH64_READ( MACH64_CLOCK_CNTL ) ); */
	DRM_INFO( "        CLR_CMP_CLR = 0x%08x\n", MACH64_READ( MACH64_CLR_CMP_CLR ) );
	DRM_INFO( "       CLR_CMP_CNTL = 0x%08x\n", MACH64_READ( MACH64_CLR_CMP_CNTL ) );
	/* DRM_INFO( "        CLR_CMP_MSK = 0x%08x\n", MACH64_READ( MACH64_CLR_CMP_MSK ) ); */
	DRM_INFO( "     CONFIG_CHIP_ID = 0x%08x\n", MACH64_READ( MACH64_CONFIG_CHIP_ID ) );
	DRM_INFO( "        CONFIG_CNTL = 0x%08x\n", MACH64_READ( MACH64_CONFIG_CNTL ) );
	DRM_INFO( "       CONFIG_STAT0 = 0x%08x\n", MACH64_READ( MACH64_CONFIG_STAT0 ) );
	DRM_INFO( "       CONFIG_STAT1 = 0x%08x\n", MACH64_READ( MACH64_CONFIG_STAT1 ) );
	DRM_INFO( "       CONFIG_STAT2 = 0x%08x\n", MACH64_READ( MACH64_CONFIG_STAT2 ) );
	DRM_INFO( "            CRC_SIG = 0x%08x\n", MACH64_READ( MACH64_CRC_SIG ) );
	DRM_INFO( "  CUSTOM_MACRO_CNTL = 0x%08x\n", MACH64_READ( MACH64_CUSTOM_MACRO_CNTL ) );
	DRM_INFO( "\n" );
	/* DRM_INFO( "           DAC_CNTL = 0x%08x\n", MACH64_READ( MACH64_DAC_CNTL ) ); */
	/* DRM_INFO( "           DAC_REGS = 0x%08x\n", MACH64_READ( MACH64_DAC_REGS ) ); */
	DRM_INFO( "        DP_BKGD_CLR = 0x%08x\n", MACH64_READ( MACH64_DP_BKGD_CLR ) );
	DRM_INFO( "        DP_FRGD_CLR = 0x%08x\n", MACH64_READ( MACH64_DP_FRGD_CLR ) );
	DRM_INFO( "             DP_MIX = 0x%08x\n", MACH64_READ( MACH64_DP_MIX ) );
	DRM_INFO( "       DP_PIX_WIDTH = 0x%08x\n", MACH64_READ( MACH64_DP_PIX_WIDTH ) );
	DRM_INFO( "             DP_SRC = 0x%08x\n", MACH64_READ( MACH64_DP_SRC ) );
	DRM_INFO( "      DP_WRITE_MASK = 0x%08x\n", MACH64_READ( MACH64_DP_WRITE_MASK ) );
	DRM_INFO( "         DSP_CONFIG = 0x%08x\n", MACH64_READ( MACH64_DSP_CONFIG ) );
	DRM_INFO( "         DSP_ON_OFF = 0x%08x\n", MACH64_READ( MACH64_DSP_ON_OFF ) );
	DRM_INFO( "           DST_CNTL = 0x%08x\n", MACH64_READ( MACH64_DST_CNTL ) );
	DRM_INFO( "      DST_OFF_PITCH = 0x%08x\n", MACH64_READ( MACH64_DST_OFF_PITCH ) );
	DRM_INFO( "\n" );
	/* DRM_INFO( "       EXT_DAC_REGS = 0x%08x\n", MACH64_READ( MACH64_EXT_DAC_REGS ) ); */
	DRM_INFO( "       EXT_MEM_CNTL = 0x%08x\n", MACH64_READ( MACH64_EXT_MEM_CNTL ) );
	DRM_INFO( "\n" );
	DRM_INFO( "          FIFO_STAT = 0x%08x\n", MACH64_READ( MACH64_FIFO_STAT ) );
	DRM_INFO( "\n" );
	DRM_INFO( "      GEN_TEST_CNTL = 0x%08x\n", MACH64_READ( MACH64_GEN_TEST_CNTL ) );
	/* DRM_INFO( "              GP_IO = 0x%08x\n", MACH64_READ( MACH64_GP_IO ) ); */
	DRM_INFO( "   GUI_CMDFIFO_DATA = 0x%08x\n", MACH64_READ( MACH64_GUI_CMDFIFO_DATA ) );
	DRM_INFO( "  GUI_CMDFIFO_DEBUG = 0x%08x\n", MACH64_READ( MACH64_GUI_CMDFIFO_DEBUG ) );
	DRM_INFO( "           GUI_CNTL = 0x%08x\n", MACH64_READ( MACH64_GUI_CNTL ) );
	DRM_INFO( "           GUI_STAT = 0x%08x\n", MACH64_READ( MACH64_GUI_STAT ) );
	DRM_INFO( "      GUI_TRAJ_CNTL = 0x%08x\n", MACH64_READ( MACH64_GUI_TRAJ_CNTL ) );
	DRM_INFO( "\n" );
	DRM_INFO( "          HOST_CNTL = 0x%08x\n", MACH64_READ( MACH64_HOST_CNTL ) );
	DRM_INFO( "           HW_DEBUG = 0x%08x\n", MACH64_READ( MACH64_HW_DEBUG ) );
	DRM_INFO( "\n" );
	DRM_INFO( "    MEM_ADDR_CONFIG = 0x%08x\n", MACH64_READ( MACH64_MEM_ADDR_CONFIG ) );
	DRM_INFO( "       MEM_BUF_CNTL = 0x%08x\n", MACH64_READ( MACH64_MEM_BUF_CNTL ) );
	DRM_INFO( "\n" );
	DRM_INFO( "      SCALE_3D_CNTL = 0x%08x\n", MACH64_READ( MACH64_SCALE_3D_CNTL ) );
	DRM_INFO( "       SCRATCH_REG0 = 0x%08x\n", MACH64_READ( MACH64_SCRATCH_REG0 ) );
	DRM_INFO( "       SCRATCH_REG1 = 0x%08x\n", MACH64_READ( MACH64_SCRATCH_REG1 ) );
	DRM_INFO( "         SETUP_CNTL = 0x%08x\n", MACH64_READ( MACH64_SETUP_CNTL ) );
	DRM_INFO( "           SRC_CNTL = 0x%08x\n", MACH64_READ( MACH64_SRC_CNTL ) );
	DRM_INFO( "\n" );
	DRM_INFO( "           TEX_CNTL = 0x%08x\n", MACH64_READ( MACH64_TEX_CNTL ) );
	DRM_INFO( "     TEX_SIZE_PITCH = 0x%08x\n", MACH64_READ( MACH64_TEX_SIZE_PITCH ) );
	DRM_INFO( "       TIMER_CONFIG = 0x%08x\n", MACH64_READ( MACH64_TIMER_CONFIG ) );
	DRM_INFO( "\n" );
	DRM_INFO( "             Z_CNTL = 0x%08x\n", MACH64_READ( MACH64_Z_CNTL ) );
	DRM_INFO( "        Z_OFF_PITCH = 0x%08x\n", MACH64_READ( MACH64_Z_OFF_PITCH ) );
	DRM_INFO( "\n" );
}


static int mach64_bm_dma_test( drm_device_t *dev )
{
	drm_mach64_private_t *dev_priv = dev->dev_private;
	dma_addr_t data_handle;
	void *cpu_addr_data;
	u32 data_addr;
	u32 *table, *data;
	u32 regs[3], expected[3];
	int i;

	DRM_DEBUG( "%s\n", __FUNCTION__ );

	table = (u32 *) dev_priv->cpu_addr_table;

	/* FIXME: get a dma buffer from the freelist here rather than using the pool */
	DRM_DEBUG( "Allocating data memory ...\n" );
	cpu_addr_data = pci_pool_alloc( dev_priv->pool, SLAB_ATOMIC, &data_handle );
	if (!cpu_addr_data || !data_handle) {
		DRM_INFO( "data-memory allocation failed!\n" );
		return -ENOMEM;
	} else {
		data = (u32 *) cpu_addr_data;
		data_addr = (u32) data_handle;
	}

	MACH64_WRITE( MACH64_SRC_CNTL, 0x00000000 );

	MACH64_WRITE( MACH64_VERTEX_1_S, 0x00000000 );
	MACH64_WRITE( MACH64_VERTEX_1_T, 0x00000000 );
	MACH64_WRITE( MACH64_VERTEX_1_W, 0x00000000 );
	
	for (i=0; i < 3; i++) {
		DRM_DEBUG( "(Before DMA Transfer) reg %d = 0x%08x\n", i, 
			   MACH64_READ( (MACH64_VERTEX_1_S + i*4) ) );
	}

	/* 1_90 = VERTEX_1_S, setup 3 sequential reg writes */
	/* use only s,t,w vertex registers so we don't have to mask any results */
	data[0] = cpu_to_le32(0x00020190); 
	data[1] = expected[0] = 0x11111111;
	data[2] = expected[1] = 0x22222222;
	data[3] = expected[2] = 0x33333333;
	data[4] = cpu_to_le32(0x0000006d); /* SRC_CNTL */
	data[5] = 0x00000000;

	DRM_DEBUG( "Preparing table ...\n" );
	table[0] = cpu_to_le32(MACH64_BM_ADDR + APERTURE_OFFSET);
	table[1] = cpu_to_le32(data_addr);
	table[2] = cpu_to_le32(6 * sizeof( u32 ) | 0x80000000 | 0x40000000);
	table[3] = 0;

	DRM_DEBUG( "table[0] = 0x%08x\n", table[0] );
	DRM_DEBUG( "table[1] = 0x%08x\n", table[1] );
	DRM_DEBUG( "table[2] = 0x%08x\n", table[2] );
	DRM_DEBUG( "table[3] = 0x%08x\n", table[3] );

	for ( i = 0 ; i < 6 ; i++) {
		DRM_DEBUG( " data[%d] = 0x%08x\n", i, data[i] );
	}

	mach64_flush_write_combine();

	DRM_DEBUG( "waiting for idle...\n" );
	if ( ( i = mach64_do_wait_for_idle( dev_priv ) ) ) {
		DRM_INFO( "mach64_do_wait_for_idle failed (result=%d)\n", i);
		DRM_INFO( "resetting engine ...\n");
		mach64_do_engine_reset( dev );
		DRM_INFO( "freeing data buffer memory.\n" );
		pci_pool_free( dev_priv->pool, cpu_addr_data, data_handle );
		DRM_INFO( "returning ...\n" );
		return i;
	}
	DRM_DEBUG( "waiting for idle...done\n" );
	
	DRM_DEBUG( "BUS_CNTL = 0x%08x\n", MACH64_READ( MACH64_BUS_CNTL ) );
	DRM_DEBUG( "SRC_CNTL = 0x%08x\n", MACH64_READ( MACH64_SRC_CNTL ) );
	DRM_DEBUG( "\n" );
	DRM_DEBUG( "data bus addr = 0x%08x\n", data_addr );
	DRM_DEBUG( "table bus addr = 0x%08x\n", dev_priv->table_addr );

	DRM_INFO( "starting DMA transfer...\n" );
	MACH64_WRITE( MACH64_BM_GUI_TABLE_CMD,
			  dev_priv->table_addr |
			  MACH64_CIRCULAR_BUF_SIZE_16KB );

	MACH64_WRITE( MACH64_SRC_CNTL, 
		      MACH64_SRC_BM_ENABLE | MACH64_SRC_BM_SYNC |
		      MACH64_SRC_BM_OP_SYSTEM_TO_REG );

	/* Kick off the transfer */
	DRM_DEBUG( "starting DMA transfer... done.\n" );
	MACH64_WRITE( MACH64_DST_HEIGHT_WIDTH, 0 );

	DRM_INFO( "waiting for idle...\n" );
	if ( ( i = mach64_do_wait_for_idle( dev_priv ) ) ) {
		/* engine locked up, dump register state and reset */
		DRM_INFO( "mach64_do_wait_for_idle failed (result=%d)\n", i);
		mach64_dump_engine_info( dev_priv );
		DRM_INFO( "resetting engine ...\n");
		mach64_do_engine_reset( dev );
		DRM_INFO( "freeing data buffer memory.\n" );
		pci_pool_free( dev_priv->pool, cpu_addr_data, data_handle );
		DRM_INFO( "returning ...\n" );
		return i;
	}
	DRM_INFO( "waiting for idle...done\n" );

	/* Check register values to see if the GUI master operation succeeded */
	for ( i = 0; i < 3; i++ ) {
		regs[i] = MACH64_READ( (MACH64_VERTEX_1_S + i*4) );
		DRM_DEBUG( "(After DMA Transfer) reg %d = 0x%08x\n", i, regs[i] );
		if (regs[i] != expected[i])
			return -1; /* GUI master operation failed */
	}

	DRM_DEBUG( "freeing data buffer memory.\n" );
	pci_pool_free( dev_priv->pool, cpu_addr_data, data_handle );
	DRM_DEBUG( "returning ...\n" );
	
	return 0;
}


static int mach64_do_dma_init( drm_device_t *dev, drm_mach64_init_t *init )
{
	drm_mach64_private_t *dev_priv;
	struct list_head *list;
	u32 tmp;
	int ret;

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
	if (!dev_priv->sarea) {
		dev->dev_private = (void *)dev_priv;
	   	mach64_do_cleanup_dma(dev);
	   	DRM_ERROR("can not find sarea!\n");
	   	return -EINVAL;
	}
	DRM_FIND_MAP( dev_priv->fb, init->fb_offset );
	if (!dev_priv->fb) {
		dev->dev_private = (void *)dev_priv;
	   	mach64_do_cleanup_dma(dev);
	   	DRM_ERROR("can not find frame buffer map!\n");
	   	return -EINVAL;
	}
	DRM_FIND_MAP( dev_priv->mmio, init->mmio_offset );
	if (!dev_priv->mmio) {
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
		if ( !dev_priv->buffers ) {
			dev->dev_private = (void *)dev_priv;
			mach64_do_cleanup_dma( dev );
			DRM_ERROR( "can not find dma buffer map!\n" );
			return -EINVAL;
		}
		DRM_IOREMAP( dev_priv->buffers );
		if ( !dev_priv->buffers->handle ) {
			dev->dev_private = (void *) dev_priv;
			mach64_do_cleanup_dma( dev );
			DRM_ERROR( "can not ioremap virtual address for"
				   " dma buffer\n" );
			return -ENOMEM;
		}
		DRM_FIND_MAP( dev_priv->agp_textures,
			      init->agp_textures_offset );
		if (!dev_priv->agp_textures) {
			dev->dev_private = (void *)dev_priv;
			mach64_do_cleanup_dma( dev );
			DRM_ERROR("could not find agp texture region!\n");
			return -EINVAL;
		}
	}

#if MACH64_USE_DMA
	/* enable block 1 registers and bus mastering */
	MACH64_WRITE( MACH64_BUS_CNTL, 
		      ( ( MACH64_READ(MACH64_BUS_CNTL) 
			  | MACH64_BUS_EXT_REG_EN ) 
			& ~MACH64_BUS_MASTER_DIS ) );

	/* changing the FIFO size from the default seems to cause problems with DMA */
	tmp = MACH64_READ( MACH64_GUI_CNTL );
	if ( (tmp & MACH64_CMDFIFO_SIZE_MASK) != MACH64_CMDFIFO_SIZE_128 ) {
		DRM_INFO( "Setting FIFO size to 128 entries\n");
		/* FIFO must be empty to change the FIFO depth */
		if ((ret=mach64_do_wait_for_idle( dev_priv ))) {
			dev->dev_private = (void *)dev_priv;
			mach64_do_cleanup_dma( dev );
			DRM_ERROR("wait for idle failed before changing FIFO depth!\n");
			return ret;
		}
		MACH64_WRITE( MACH64_GUI_CNTL, ( ( tmp & ~MACH64_CMDFIFO_SIZE_MASK ) \
						 | MACH64_CMDFIFO_SIZE_128 ) );
		/* need to read GUI_STAT for proper sync according to register reference */
		if ((ret=mach64_do_wait_for_idle( dev_priv ))) {
			dev->dev_private = (void *)dev_priv;
			mach64_do_cleanup_dma( dev );
			DRM_ERROR("wait for idle failed when changing FIFO depth!\n");
			return ret;
		}
	}

	/* create pci pool for descriptor memory */
	DRM_INFO( "Creating pci pool\n");
	dev_priv->pool = pci_pool_create( "mach64",   /* name */ 
					  NULL,       /* dev */
					  0x4000,     /* size - 16KB */
					  0x4000,     /* align - 16KB */
					  0x4000,     /* alloc - 16KB */
					  SLAB_ATOMIC /* flags */ 
		);

	if (!dev_priv->pool) {
		dev_priv->driver_mode = MACH64_MODE_MMIO;
		DRM_INFO( "pci_pool_create failed, using MMIO mode\n");
		dev->dev_private = (void *) dev_priv;
		return 0;
	}

	/* allocate descriptor memory from pci pool */
	DRM_INFO( "Allocating descriptor table memory\n" );
	dev_priv->cpu_addr_table = pci_pool_alloc( dev_priv->pool, SLAB_ATOMIC, 
						   &dev_priv->table_handle );
	if (!dev_priv->cpu_addr_table || !dev_priv->table_handle) {
		pci_pool_destroy( dev_priv->pool );
		dev_priv->driver_mode = MACH64_MODE_MMIO;
		DRM_INFO( "pci_pool_alloc failed, using MMIO mode\n");
		dev->dev_private = (void *) dev_priv;
		return 0;
	} else {
		dev_priv->table_addr = (u32) dev_priv->table_handle;
		memset( dev_priv->cpu_addr_table, 0x0, 0x4000 );
	}

	DRM_INFO( "descriptor table: cpu addr: 0x%08x, bus addr: 0x%08x\n", 
		  (u32) dev_priv->cpu_addr_table, dev_priv->table_addr );

	/* setup physical address and size of descriptor table */
	MACH64_WRITE( MACH64_BM_GUI_TABLE_CMD, 
		      ( dev_priv->table_addr | MACH64_CIRCULAR_BUF_SIZE_16KB ) );

	/* try a DMA GUI-mastering pass and fall back to MMIO if it fails */
	dev->dev_private = (void *) dev_priv;
	DRM_INFO( "Starting DMA test...\n");
	if ( (ret=mach64_bm_dma_test( dev )) == 0 ) {
		dev_priv->driver_mode = MACH64_MODE_DMA_SYNC;
		DRM_INFO( "DMA test succeeded, using synchronous DMA mode\n");
	} else {
		dev_priv->driver_mode = MACH64_MODE_MMIO;
		DRM_INFO( "DMA test failed (ret=%d), using MMIO mode\n", ret );
	}
#else
	dev_priv->driver_mode = MACH64_MODE_MMIO;
	DRM_INFO( "Using MMIO mode\n");
#endif

	dev->dev_private = (void *) dev_priv;
	
	return 0;
}

int mach64_do_cleanup_dma( drm_device_t *dev )
{
	DRM_DEBUG( "%s\n", __FUNCTION__ );
	
	if ( dev->dev_private ) {
		drm_mach64_private_t *dev_priv = dev->dev_private;
		
		if (dev_priv->buffers) {
			DRM_IOREMAPFREE( dev_priv->buffers );
		}
		DRM(free)( dev_priv, sizeof(drm_mach64_private_t),
			   DRM_MEM_DRIVER );
		dev->dev_private = NULL;

		if ( dev_priv->table_handle ) {
			DRM_INFO( "freeing descriptor table from pci pool\n" );
			pci_pool_free( dev_priv->pool, dev_priv->cpu_addr_table, 
				       dev_priv->table_handle );
		}
		if ( dev_priv->pool ) {
			DRM_INFO( "destroying pci pool\n" );
			pci_pool_destroy( dev_priv->pool );
		}

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

void mach64_freelist_reset( drm_device_t *dev )
{
	drm_device_dma_t *dma = dev->dma;
	int i;

	for ( i = 0 ; i < dma->buf_count ; i++ ) {
		drm_buf_t *buf = dma->buflist[i];
		drm_mach64_buf_priv_t *buf_priv = buf->dev_private;
		buf_priv->age = 0;
	}
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
