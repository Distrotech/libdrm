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
 * Engine control
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

static void scratch_reg0_test(drm_mach64_private_t *dev_priv )
{
	u32 scratch_reg0;
	scratch_reg0=MACH64_READ( MACH64_SCRATCH_REG0 );

	MACH64_WRITE( MACH64_SCRATCH_REG0, 0x55555555 );
	if ( MACH64_READ( MACH64_SCRATCH_REG0 ) == 0x55555555 ) {
		MACH64_WRITE( MACH64_SCRATCH_REG0, 0xaaaaaaaa );

		if ( MACH64_READ( MACH64_SCRATCH_REG0 ) != 0xaaaaaaaa ) {
			DRM_ERROR( "2nd scratch reg failed!\n" );
		}
	} else {
		DRM_ERROR( "1st scratch reg failed!\n" );
	}
	MACH64_WRITE( MACH64_SCRATCH_REG0, scratch_reg0 );
}

static void dump_engine_info( drm_mach64_private_t *dev_priv )
{
	DRM_INFO( "\n" );
	DRM_INFO( "           AGP_BASE = 0x%08x\n", MACH64_READ( MACH64_AGP_BASE ) );
	DRM_INFO( "           AGP_CNTL = 0x%08x\n", MACH64_READ( MACH64_AGP_CNTL ) );
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


static void bm_dma_test(drm_mach64_private_t *dev_priv)
{
	struct pci_pool *pool;
	dma_addr_t table_handle, data_handle;
	u32 table_addr, data_addr;
	u32 *table, *data;

	void *cpu_addr_table, *cpu_addr_data;
	int i;

	DRM_INFO( "Creating pool ... \n");
	pool = pci_pool_create( "mach64", NULL, 0x4000,
				0x4000, 0x4000, SLAB_ATOMIC );

	if (!pool) {
		DRM_INFO( "pci_pool_create failed!\n" );
		return;
	}

	DRM_INFO( "Allocating table memory ...\n" );
	cpu_addr_table = pci_pool_alloc( pool, SLAB_ATOMIC, &table_handle );
	if (!cpu_addr_table || !table_handle) {
		DRM_INFO( "table-memory allocation failed!\n" );
		return;
	} else {
		table = (u32 *) cpu_addr_table;
		table_addr = (u32) table_handle;
		memset( cpu_addr_table, 0x0, 0x4000 );
	}

	DRM_INFO( "Allocating data memory ...\n" );
	cpu_addr_data = pci_pool_alloc( pool, SLAB_ATOMIC, &data_handle );
	if (!cpu_addr_data || !data_handle) {
		DRM_INFO( "data-memory allocation failed!\n" );
		return;
	} else {
		data = (u32 *) cpu_addr_data;
		data_addr = (u32) data_handle;
	}

	MACH64_WRITE( MACH64_SRC_CNTL, 0x00000000 );
	MACH64_WRITE( MACH64_PAT_REG0, 0x11111111 );

	DRM_INFO( "(Before DMA Transfer) PAT_REG0 = 0x%08x\n",
		  MACH64_READ( MACH64_PAT_REG0 ) );

	data[0] = 0x000000a0;
	data[1] = 0x22222222;
	data[2] = 0x000000a0;
	data[3] = 0x22222222;
	data[4] = 0x000000a0;
	data[5] = 0x22222222;
	data[6] = 0x0000006d;
	data[7] = 0x00000000;

	DRM_INFO( "Preparing table ...\n" );
	table[0] = MACH64_BM_ADDR + APERTURE_OFFSET;
	table[1] = data_addr;
	table[2] = 8 * sizeof( u32 ) | 0x80000000 | 0x40000000;
	table[3] = 0;

	DRM_INFO( "table[0] = 0x%08x\n", table[0] );
	DRM_INFO( "table[1] = 0x%08x\n", table[1] );
	DRM_INFO( "table[2] = 0x%08x\n", table[2] );
	DRM_INFO( "table[3] = 0x%08x\n", table[3] );

	for ( i = 0 ; i < 8 ; i++) {
		DRM_INFO( " data[%d] = 0x%08x\n", i, data[i] );
	}

	mb();

	DRM_INFO( "waiting for idle...\n" );
	mach64_do_wait_for_idle( dev_priv );
	DRM_INFO( "waiting for idle... done.\n" );

	DRM_INFO( "BUS_CNTL = 0x%08x\n", MACH64_READ( MACH64_BUS_CNTL ) );
	DRM_INFO( "SRC_CNTL = 0x%08x\n", MACH64_READ( MACH64_SRC_CNTL ) );
	DRM_INFO( "\n" );
	DRM_INFO( "data  = 0x%08x\n", data_addr );
	DRM_INFO( "table = 0x%08x\n", table_addr );

	DRM_INFO( "starting DMA transfer...\n" );
	MACH64_WRITE( MACH64_BM_GUI_TABLE,
			  table_addr |
			  MACH64_CIRCULAR_BUF_SIZE_16KB );

	MACH64_WRITE( MACH64_SRC_CNTL, 
		      MACH64_SRC_BM_ENABLE | MACH64_SRC_BM_SYNC |
		      MACH64_SRC_BM_OP_SYSTEM_TO_REG );

	/* Kick off the transfer */
	DRM_INFO( "starting DMA transfer... done.\n" );
	MACH64_WRITE( MACH64_DST_HEIGHT_WIDTH, 0 );
	MACH64_WRITE( MACH64_SRC_CNTL, 0 );

	DRM_INFO( "waiting for idle [locked_after_dma??]...\n" );
	if ((i=mach64_do_wait_for_idle( dev_priv ))) {
		DRM_INFO( "mach64_do_wait_for_idle failed (result=%d)\n", i);
		DRM_INFO( "resetting engine ...");
		mach64_do_engine_reset( dev_priv );
	}

	DRM_INFO( "(After DMA Transfer) PAT_REG0 = 0x%08x\n",
		  MACH64_READ( MACH64_PAT_REG0 ) );

	DRM_INFO( "freeing memory.\n" );
	pci_pool_free( pool, cpu_addr_table, table_handle );
	pci_pool_free( pool, cpu_addr_data, data_handle );
	pci_pool_destroy( pool );
	DRM_INFO( "returning ...\n" );
}

int mach64_do_engine_reset( drm_mach64_private_t *dev_priv )
{
	u32 bus_cntl, gen_test_cntl;

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
	dev->dev_private = (void *) dev_priv;

	memset( dev_priv, 0, sizeof(drm_mach64_private_t) );

	dev_priv->fb_bpp		= init->fb_bpp;
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

	dev_priv->sarea_priv = (drm_mach64_sarea_t *)
		((u8 *)dev_priv->sarea->handle +
		 init->sarea_priv_offset);

	DRM_FIND_MAP( dev_priv->fb, init->fb_offset );
	DRM_FIND_MAP( dev_priv->mmio, init->mmio_offset );
	DRM_FIND_MAP( dev_priv->buffers, init->buffers_offset );

	DRM_IOREMAP( dev_priv->buffers );

	DRM_INFO( "init->fb = 0x%08x\n", init->fb_offset );
	DRM_INFO( "init->mmio_offset = 0x%08x\n", init->mmio_offset );
	DRM_INFO( "mmio->offset=0x%08lx mmio->handle=0x%08lx\n",
		  dev_priv->mmio->offset, (unsigned long) dev_priv->mmio->handle );
	DRM_INFO( "buffers->offset=0x%08lx buffers->handle=0x%08lx\n",
		  dev_priv->buffers->offset, (unsigned long) dev_priv->buffers->handle );


	tmp = MACH64_READ( MACH64_BUS_CNTL );
	tmp = ( tmp | 0x08000000 ) & ~MACH64_BUS_MASTER_DIS;
	MACH64_WRITE( MACH64_BUS_CNTL, tmp );

	tmp = MACH64_READ( MACH64_GUI_CNTL );
	MACH64_WRITE( MACH64_GUI_CNTL, ( ( tmp & ~MACH64_CMDFIFO_SIZE_MASK ) \
					 | MACH64_CMDFIFO_SIZE_128 ) );
	DRM_INFO( "GUI_STAT=0x%08x\n", MACH64_READ( MACH64_GUI_STAT ) );
	DRM_INFO( "GUI_CNTL=0x%08x\n", MACH64_READ( MACH64_GUI_CNTL ) );

	bm_dma_test( dev_priv );
	return 0;
}

static int mach64_do_dma_cleanup( drm_device_t *dev )
{
	DRM_DEBUG( "%s\n", __FUNCTION__ );
	dump_engine_info( dev->dev_private );	
	
	if ( dev->dev_private ) {
		drm_mach64_private_t *dev_priv = dev->dev_private;
		
		DRM_IOREMAPFREE( dev_priv->buffers );
		
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

