/* mach64_dma.c -- DMA support for mach64 (Rage Pro) driver -*- linux-c -*-
 * Created: Sun Dec 03 19:20:26 2000 by gareth@valinux.com
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
 *   Gareth Hughes <gareth@valinux.com>
 *   Frank C. Earl <fearl@airmail.net>
 *   Leif Delgass <ldelgass@retinalburn.net>
 */

#include "mach64.h"
#include "drmP.h"
#include "mach64_drv.h"

#include <linux/interrupt.h>	/* For task queue support */
#include <linux/delay.h>
#include <linux/list.h>

int mach64_do_cleanup_dma( drm_device_t *dev );
int mach64_handle_dma( drm_mach64_private_t *dev_priv );
int mach64_do_complete_blit( drm_mach64_private_t *dev_priv );
int mach64_do_wait_for_dma( drm_mach64_private_t *dev_priv );
int mach64_do_release_used_buffers( drm_mach64_private_t *dev_priv );
int mach64_init_freelist( drm_device_t *dev );
void mach64_destroy_freelist( drm_device_t *dev );

static DECLARE_WAIT_QUEUE_HEAD(read_wait);


/* ================================================================
 * Interrupt handler
 */

void mach64_dma_service(int irq, void *device, struct pt_regs *regs)
{
        drm_device_t *dev = (drm_device_t *) device;
	drm_mach64_private_t *dev_priv = (drm_mach64_private_t *)dev->dev_private;
        
        unsigned int flags;

        /* Check to see if we've been interrupted for VBLANK or the BLIT completion
           and ack the interrupt accordingly...  Set flags for the handler to 
           know that it needs to process accordingly... */
        flags = MACH64_READ(MACH64_CRTC_INT_CNTL);
	if (flags & MACH64_CRTC_VBLANK_INT)
        {                
                /* VBLANK -- GUI-master dispatch and polling... */
                MACH64_WRITE(MACH64_CRTC_INT_CNTL, flags | MACH64_CRTC_VBLANK_INT_AK);
                atomic_inc(&dev_priv->do_gui);
	}
	if (flags & MACH64_CRTC_BUSMASTER_EOL_INT)
        {                
                /* Completion of BLIT op */
                MACH64_WRITE(MACH64_CRTC_INT_CNTL, flags | MACH64_CRTC_BUSMASTER_EOL_INT_AK);
                atomic_inc(&dev_priv->do_blit);
        }
        /* Check for an error condition in the engine...  */
        if (MACH64_READ(MACH64_FIFO_STAT) & 0x80000000) 
        {
                /* This would be a failure to maintain FIFO discipline
                   per the SDK sources.   Need to reset... */
                mach64_do_engine_reset(dev_priv);
        }
#if 0
	/* According to reg. ref this bit is BUS_MSTR_RD_LINE and on my
	 * card (LT Pro), it's set by default (LLD)
	 */
        if (MACH64_READ(MACH64_BUS_CNTL) & 0x00200000)
        {
                /* This would be a host data error, per information from
                   Vernon Chiang @ ATI (Thanks, Vernon!).  Need to reset... */
                mach64_do_engine_reset(dev_priv);
        }
#endif
        /* Ok, now that we've gotten that out of the way, schedule the bottom half accordingly... */
	queue_task(&dev->tq, &tq_immediate);
	mark_bh(IMMEDIATE_BH);
        
        return;
}

/* Handle the DMA dispatch/completion */
void mach64_dma_immediate_bh(void *device)
{
        drm_device_t *dev = (drm_device_t *) device;
        drm_mach64_private_t *dev_priv = (drm_mach64_private_t *)dev->dev_private;

        /* Handle the completion of a blit pass... */
        if (atomic_read(&dev_priv->do_blit) > 0)
        {
                atomic_set(&dev_priv->do_blit, 0);
                /*  mach64_do_complete_blit(dev_priv); */
        }        

        /* Check to see if we've been told to handle gui-mastering... */
        if (atomic_read(&dev_priv->do_gui) > 0)
        {
		atomic_set(&dev_priv->do_gui, 0);
                /*  mach64_handle_dma(dev_priv); */
        }
        
        wake_up_interruptible(&read_wait);
        return;
}


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

/* Wait until all DMA requests have been processed... */
int mach64_do_wait_for_dma( drm_mach64_private_t *dev_priv )
{
	int i, ret;

	/* Assume we timeout... */
	ret = -EBUSY;

	for ( i = 0 ; i < dev_priv->usec_timeout; i++ ) 
	{
		if ( list_empty(&dev_priv->dma_queue) )
		{
			ret = mach64_do_wait_for_idle( dev_priv );
			break;
		}
		udelay( 1 );
	}
	
	if (ret != 0)
		DRM_INFO( "do_wait_for_dma failed! GUI_STAT=0x%08x\n", MACH64_READ( MACH64_GUI_STAT ) );
	
	return ret;
}

int mach64_do_dma_idle( drm_mach64_private_t *dev_priv ) {
	int ret;

	/* wait for completion */
	if ( (ret = mach64_do_wait_for_idle( dev_priv )) < 0 ) {
		DRM_ERROR( "%s failed\n", __FUNCTION__ );
		mach64_do_release_used_buffers( dev_priv );
		return ret;
	}

	/* clean up after pass */
	mach64_do_release_used_buffers( dev_priv );
	return 0;
}

/* Reset the engine.  This will stop the DMA if it is running.
 */
int mach64_do_engine_reset( drm_mach64_private_t *dev_priv )
{
	u32 tmp;

	DRM_DEBUG( "%s\n", __FUNCTION__ );

	/* Kill off any outstanding DMA transfers.
	 */
	tmp = MACH64_READ( MACH64_BUS_CNTL );
	MACH64_WRITE( MACH64_BUS_CNTL,
				  tmp | MACH64_BUS_MASTER_DIS );

	/* Reset the GUI engine (high to low transition).
	 */
	tmp = MACH64_READ( MACH64_GEN_TEST_CNTL );
	MACH64_WRITE( MACH64_GEN_TEST_CNTL,
				  tmp & ~MACH64_GUI_ENGINE_ENABLE );
	/* Enable the GUI engine
	 */
	tmp = MACH64_READ( MACH64_GEN_TEST_CNTL );
	MACH64_WRITE( MACH64_GEN_TEST_CNTL,
				  tmp | MACH64_GUI_ENGINE_ENABLE );

	/* ensure engine is not locked up by clearing any FIFO or HOST errors
	*/
	tmp = MACH64_READ( MACH64_BUS_CNTL );
	MACH64_WRITE( MACH64_BUS_CNTL, tmp | 0x00a00000 );
	
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
	DRM_INFO( "           PAT_REG0 = 0x%08x\n", MACH64_READ( MACH64_PAT_REG0 ) );
	DRM_INFO( "           PAT_REG1 = 0x%08x\n", MACH64_READ( MACH64_PAT_REG1 ) );
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

/* ================================================================
 * DMA test and initialization
 */

static int mach64_bm_dma_test( drm_device_t *dev )
{
	drm_mach64_private_t *dev_priv = dev->dev_private;
	dma_addr_t data_handle;
	void *cpu_addr_data;
	u32 data_addr;
	u32 *table, *data;
	u32 regs[3], expected[3];
	int i, count;

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

	MACH64_WRITE( MACH64_SRC_CNTL, 0 );

	MACH64_WRITE( MACH64_VERTEX_1_S, 0x00000000 );
	MACH64_WRITE( MACH64_VERTEX_1_T, 0x00000000 );
	MACH64_WRITE( MACH64_VERTEX_1_W, 0x00000000 );

	for (i=0; i < 3; i++) {
		DRM_DEBUG( "(Before DMA Transfer) reg %d = 0x%08x\n", i, 
			   MACH64_READ( (MACH64_VERTEX_1_S + i*4) ) );
	}

	/* use only s,t,w vertex registers so we don't have to mask any results */
	/* fill up a buffer with sets of 3 consecutive writes starting with VERTEX_1_S */
	count = 0;

	data[count++] = cpu_to_le32(0x00020190); /* 1_90 = VERTEX_1_S */
	data[count++] = expected[0] = 0x11111111;
	data[count++] = expected[1] = 0x22222222;
	data[count++] = expected[2] = 0x33333333;

	while (count < 1020) {
		data[count++] = cpu_to_le32(0x00020190);
		data[count++] = 0x11111111;
		data[count++] = 0x22222222;
		data[count++] = 0x33333333;
	}
	data[count++] = cpu_to_le32(0x0000006d); /* SRC_CNTL */
	data[count++] = 0;

	DRM_DEBUG( "Preparing table ...\n" );
	table[0] = cpu_to_le32(MACH64_BM_ADDR + APERTURE_OFFSET);
	table[1] = cpu_to_le32(data_addr);
	table[2] = cpu_to_le32(count * sizeof( u32 ) | 0x80000000 | 0x40000000);
	table[3] = 0;

	DRM_DEBUG( "table[0] = 0x%08x\n", table[0] );
	DRM_DEBUG( "table[1] = 0x%08x\n", table[1] );
	DRM_DEBUG( "table[2] = 0x%08x\n", table[2] );
	DRM_DEBUG( "table[3] = 0x%08x\n", table[3] );

	for ( i = 0 ; i < count ; i++) {
		DRM_DEBUG( " data[%d] = 0x%08x\n", i, data[i] );
	}

	mach64_flush_write_combine();

	DRM_DEBUG( "waiting for idle...\n" );
	if ( ( i = mach64_do_wait_for_idle( dev_priv ) ) ) {
		DRM_INFO( "mach64_do_wait_for_idle failed (result=%d)\n", i);
		DRM_INFO( "resetting engine ...\n");
		mach64_do_engine_reset( dev_priv );
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
		mach64_do_engine_reset( dev_priv );
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

	dev->dev_private = (void *) dev_priv;

	if ( !init->pseudo_dma ) {
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
		dev_priv->table_size = 0x4000;
		dev_priv->pool = pci_pool_create( "mach64",   /* name */ 
						  NULL,       /* dev */
						  dev_priv->table_size,     /* size - 16KB */
						  dev_priv->table_size,     /* align - 16KB */
						  dev_priv->table_size,     /* alloc - 16KB */
						  SLAB_ATOMIC /* flags */ 
			);

		if (!dev_priv->pool) {
			dev_priv->driver_mode = MACH64_MODE_MMIO;
			DRM_INFO( "pci_pool_create failed, using pseudo-DMA mode\n");
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
			DRM_INFO( "pci_pool_alloc failed, using pseudo-DMA mode\n");
			dev->dev_private = (void *) dev_priv;
			return 0;
		} else {
			dev_priv->table_addr = (u32) dev_priv->table_handle;
			memset( dev_priv->cpu_addr_table, 0x0, 0x4000 );
		}

		DRM_INFO( "descriptor table: cpu addr: 0x%08x, bus addr: 0x%08x\n", 
			  (u32) dev_priv->cpu_addr_table, dev_priv->table_addr );

		/* try a DMA GUI-mastering pass and fall back to MMIO if it fails */
		dev->dev_private = (void *) dev_priv;
		DRM_INFO( "Starting DMA test...\n");
		if ( (ret=mach64_bm_dma_test( dev )) == 0 ) {
#if (MACH64_DEFAULT_MODE == MACH64_MODE_DMA_ASYNC)
			dev_priv->driver_mode = MACH64_MODE_DMA_ASYNC;
			DRM_INFO( "DMA test succeeded, using asynchronous DMA mode\n");
#else
			dev_priv->driver_mode = MACH64_MODE_DMA_SYNC;
			DRM_INFO( "DMA test succeeded, using synchronous DMA mode\n");
#endif
		} else {
			dev_priv->driver_mode = MACH64_MODE_MMIO;
			DRM_INFO( "DMA test failed (ret=%d), using pseudo-DMA mode\n", ret );
		}

		/* setup offsets for physical address of table start and end */
		dev_priv->table_start = dev_priv->table_addr;
		dev_priv->table_end = dev_priv->table_start;

		/* setup write pointer to descriptor table */
		dev_priv->table_wptr = ((u32 *) dev_priv->cpu_addr_table) + 4;
		/* setup physical address and size of descriptor table */
		MACH64_WRITE( MACH64_BM_GUI_TABLE_CMD, 
			      ( dev_priv->table_addr | MACH64_CIRCULAR_BUF_SIZE_16KB ) );

	} else {
		dev_priv->driver_mode = MACH64_MODE_MMIO;
		DRM_INFO( "Forcing pseudo-DMA mode\n");
	}

#if MACH64_USE_FRAME_AGING
	dev_priv->sarea_priv->last_frame = 0;
	MACH64_WRITE( MACH64_LAST_FRAME_REG, dev_priv->sarea_priv->last_frame );
#endif
#if MACH64_USE_BUFFER_AGING
	dev_priv->sarea_priv->last_dispatch = 0;
	MACH64_WRITE( MACH64_LAST_DISPATCH_REG, dev_priv->sarea_priv->last_dispatch );
#endif

	/* Set up the freelist, empty (placeholder), pending, and DMA request queues... */
	INIT_LIST_HEAD(&dev_priv->free_list);
	INIT_LIST_HEAD(&dev_priv->empty_list);
	INIT_LIST_HEAD(&dev_priv->pending);
	INIT_LIST_HEAD(&dev_priv->dma_queue);

	mach64_init_freelist( dev );

#if 0
        /* Set up for interrupt handling proper- clear state on the handler
	 * The handler is enabled by the DDX via the DRM(control) ioctl once we return 
	 */
        atomic_set(&dev_priv->do_gui, 0);                
        atomic_set(&dev_priv->do_blit, 0);                
        atomic_set(&dev_priv->dma_timeout, -1);
#endif

	dev->dev_private = (void *) dev_priv;
	
	return 0;
}

/* ================================================================
 * Primary DMA stream management
 */

/*
	Manage the GUI-Mastering operations of the chip.  Since the GUI-Master
	operation is slightly less intelligent than the BLIT operation (no interrupt
	for completion), we have to provide the completion detection, etc. in 
	a state engine.
*/
int mach64_handle_dma( drm_mach64_private_t *dev_priv )
{
	int timeout;
		
	timeout = atomic_read(&dev_priv->dma_timeout);
	
	/* Check for engine idle... */
	if (!(MACH64_READ(MACH64_GUI_STAT) & MACH64_GUI_ACTIVE))
	{
		/* Check to see if we had a DMA pass going... */
		if ( timeout > -1)
		{
			/* Ok, do the clean up for the previous pass... */
			mach64_do_release_used_buffers(dev_priv);
			atomic_set(&dev_priv->dma_timeout, -1);
		}
		
		/* Now, check for queued buffers... */	
		if (!list_empty(&dev_priv->dma_queue))
		{
			atomic_set(&dev_priv->dma_timeout, 0);
		}
		
		/* Check to see if we've got a DMA pass set up */
		if (atomic_read(&dev_priv->dma_timeout) == 0)
		{
			/* Make sure we're locked and fire off the prepped pass */
			mach64_do_dma_flush(dev_priv);
		}
	}
	else
	{
		/* Check to see if we've got a GUI-Master going... */
		if ((timeout > -1) && (MACH64_READ( MACH64_SRC_CNTL ) & MACH64_SRC_BM_ENABLE))
		{
			/* Check for DMA timeout */
			if (timeout > MACH64_DMA_TIMEOUT)
			{
				DRM_INFO("%s, dma timed out at: %d", __FUNCTION__, timeout);
				/* Assume the engine's hung bigtime...  */
				mach64_do_engine_reset(dev_priv);				
				mach64_do_release_used_buffers(dev_priv);
				atomic_set(&dev_priv->dma_timeout, -1);
			}
			else
			{
				atomic_inc(&dev_priv->dma_timeout);
			}			
		}		
	}
	
        return 0;
}


/*
	Perform the clean-up for the blit operation- turn off DMA
	operation (not support) and unlock the DRM.
*/
int mach64_do_complete_blit( drm_mach64_private_t *dev_priv )
{
	/* Turn off DMA mode -- we don't have anything going because the chip
	   tells us that it completed in this case (Why didn't they do this for
	   GUI Master operation?!)  */	
        MACH64_WRITE( MACH64_BUS_CNTL, MACH64_READ( MACH64_BUS_CNTL ) | MACH64_BUS_MASTER_DIS );
	
        return 0;
}


/*
 * Take the pending list and build up a descriptor table for
 * GUI-Master use, then fire off the DMA engine with the list.
 * (We add a register reset buffer that the DRM only controls)
 */
static int mach64_do_dispatch_real_dma( drm_mach64_private_t *dev_priv )
{
	u32 *table_ptr;
	u32 table_start, table_end;
	int wrapped = 0;
	struct list_head *ptr, *tmp;	
	drm_mach64_freelist_t *entry;
	drm_buf_t *buf;
	int bytes, pages, remainder, tableDwords;
	u32 address, page, end_flag;
	u32 *p;
	u32 reg;
	int ret, i;

	table_ptr = dev_priv->table_wptr;
	table_start = dev_priv->table_end + (sizeof(u32)*4);
	/* Need to wrap ? */
	if ( table_start >= (dev_priv->table_addr + dev_priv->table_size) ) {
		table_start = dev_priv->table_addr;
		table_ptr = (u32 *)dev_priv->cpu_addr_table;
		wrapped = 1;
	}

	table_end = table_start;
	tableDwords = 0;

#if MACH64_USE_BUFFER_AGING
	/* bump the counter for buffer aging */
	dev_priv->sarea_priv->last_dispatch++;
#endif

	/* Iterate the queue and build a descriptor table accordingly... */	
	list_for_each(ptr, &dev_priv->dma_queue)
	{
		entry = list_entry(ptr, drm_mach64_freelist_t, list);
		buf = entry->buf;
		bytes = buf->used;

		if (dev_priv->is_pci) {
			address = (u32) virt_to_bus((void *)buf->address);
			p = (u32 *) buf->address;
		} else {
			address = (u32) buf->bus_address;
			p = (u32 *)((char *)dev_priv->buffers->handle + 
				    buf->offset);
		}

		if (ptr->next == &dev_priv->dma_queue) {
			int idx = 0;
			int start = 0;
			/* FIXME: Make sure we don't overflow */
#if MACH64_USE_BUFFER_AGING
			if (MACH64_BUFFER_SIZE - buf->used < (sizeof(u32)*6)) {
#else
			if (MACH64_BUFFER_SIZE - buf->used < (sizeof(u32)*4)) {
#endif
				DRM_ERROR("buffer overflow\n");
				return 0;
			}
			start = idx = (bytes/sizeof(u32));
#if MACH64_USE_BUFFER_AGING
			p[idx++] = cpu_to_le32(DMAREG(MACH64_LAST_DISPATCH_REG));
			p[idx++] = cpu_to_le32(dev_priv->sarea_priv->last_dispatch);
#endif
			reg = MACH64_READ( MACH64_BUS_CNTL );
			reg |= MACH64_BUS_MASTER_DIS | MACH64_BUS_EXT_REG_EN;
			p[idx++] = cpu_to_le32(DMAREG(MACH64_BUS_CNTL));
			p[idx++] = cpu_to_le32(reg);
			p[idx++] = cpu_to_le32(DMAREG(MACH64_SRC_CNTL));
			p[idx++] = cpu_to_le32(0);
			bytes += (idx-start)*sizeof(u32);
		}

		pages = (bytes + DMA_CHUNKSIZE - 1) / DMA_CHUNKSIZE;
		for ( i = 0 ; i < pages-1 ; i++ ) {
			page = address + i * DMA_CHUNKSIZE;

			/* Check to see if we caught up to the last pass */
			if (mach64_wait_ring( dev_priv, table_end ) < 0) {
				/* We timed out */
				DRM_ERROR( "%s: ring wait failed pre-dispatch, resetting.\n", 
					   __FUNCTION__);
				mach64_dump_engine_info( dev_priv );
				mach64_do_engine_reset( dev_priv );
				mach64_do_release_used_buffers( dev_priv );
				return -EBUSY;
			}
			table_ptr[DMA_FRAME_BUF_OFFSET] = cpu_to_le32(MACH64_BM_ADDR + 
								      APERTURE_OFFSET);
			table_ptr[DMA_SYS_MEM_ADDR] = cpu_to_le32(page);
			table_ptr[DMA_COMMAND] = cpu_to_le32(DMA_CHUNKSIZE | 0x40000000);
			table_ptr[DMA_RESERVED] = 0;

			tableDwords += 4;
			table_ptr += sizeof(u32);
			table_end += sizeof(u32)*4;
			/* Need to wrap ? */
			if ( table_end >= (dev_priv->table_addr + dev_priv->table_size) ) {
				table_end = dev_priv->table_addr;
				table_ptr = (u32 *)dev_priv->cpu_addr_table;
				wrapped = 1;
			}
		}
		/* Check to see if we caught up to the last pass */
		if (mach64_wait_ring( dev_priv, table_end ) < 0) {
			/* We timed out */
			DRM_ERROR( "%s: ring wait failed pre-dispatch, resetting.\n", 
				  __FUNCTION__);
			mach64_dump_engine_info( dev_priv );
			mach64_do_engine_reset( dev_priv );
			mach64_do_release_used_buffers( dev_priv );
			return -EBUSY;
		}

		/* if this is the last buffer, we need to set the final descriptor flag */
		end_flag = (ptr->next == &dev_priv->dma_queue) ? 0x80000000 : 0;

                /* generate the final descriptor for any remaining commands in this buffer */
		page = address + i * DMA_CHUNKSIZE;
		remainder = bytes - i * DMA_CHUNKSIZE;
		table_ptr[DMA_FRAME_BUF_OFFSET] = cpu_to_le32(MACH64_BM_ADDR + APERTURE_OFFSET);
		table_ptr[DMA_SYS_MEM_ADDR] = cpu_to_le32(page);
		table_ptr[DMA_COMMAND] = cpu_to_le32(remainder | end_flag | 0x40000000);
		table_ptr[DMA_RESERVED] = 0;

#if !MACH64_USE_BUFFER_AGING
		/* Save physical address of last descriptor for this buffer.
		 * This is needed to check for completion of the buffer in freelist_get
		 */
		entry->descr_addr = table_end;
#endif

		tableDwords += 4;
		table_ptr += sizeof(u32);
		if (!end_flag) {
			table_end += sizeof(u32)*4;
			/* Need to wrap ? */
			if ( table_end >= (dev_priv->table_addr + dev_priv->table_size) ) {
				table_end = dev_priv->table_addr;
				table_ptr = (u32 *)dev_priv->cpu_addr_table;
				wrapped = 1;
			}
		}
	}

	dev_priv->table_wptr = table_ptr; /* points one descriptor past table_end */
	dev_priv->table_start = table_start;
	dev_priv->table_end = table_end;

	/* Now, dispatch the whole lot to the gui-master engine */

	/* flush write combining */
	mach64_flush_write_combine();

	/* Ensure last pass completed without locking up */
	if ((ret=mach64_do_wait_for_idle( dev_priv )) < 0) {
		DRM_ERROR( "%s: idle failed before dispatch, resetting engine\n", 
			  __FUNCTION__);
		mach64_dump_engine_info( dev_priv );
		mach64_do_engine_reset( dev_priv );
		mach64_do_release_used_buffers( dev_priv );
		return ret;
	}

	/* release completed buffers from the last pass */
	mach64_do_release_used_buffers( dev_priv );

	/* Move everything in the queue to the pending list */
	i = 0;
	list_for_each_safe(ptr, tmp, &dev_priv->dma_queue)
	{
		entry = list_entry(ptr, drm_mach64_freelist_t, list);
#if MACH64_USE_BUFFER_AGING
		entry->age = dev_priv->sarea_priv->last_dispatch;
#endif
		list_del(ptr);
		entry->buf->waiting = 0;
		entry->buf->pending = 1;
		list_add_tail(ptr, &dev_priv->pending);
		i++;
	}

	/* enable bus mastering and block 1 registers */
	MACH64_WRITE( MACH64_BUS_CNTL, 
		      ( MACH64_READ(MACH64_BUS_CNTL) & ~MACH64_BUS_MASTER_DIS ) 
		      | MACH64_BUS_EXT_REG_EN );

	/* reset descriptor table ring head */
	MACH64_WRITE( MACH64_BM_GUI_TABLE_CMD, ( dev_priv->table_start
						 | MACH64_CIRCULAR_BUF_SIZE_16KB ) );

	/* enable GUI-master operation */
	MACH64_WRITE( MACH64_SRC_CNTL, 
		      MACH64_SRC_BM_ENABLE | MACH64_SRC_BM_SYNC |
		      MACH64_SRC_BM_OP_SYSTEM_TO_REG );
	/* kick off the transfer */
	MACH64_WRITE( MACH64_DST_HEIGHT_WIDTH, 0 );

	DRM_DEBUG( "%s: dispatched %d buffers\n", __FUNCTION__, i );
	DRM_DEBUG( "%s: table start:0x%08x end:0x%08x wptr:0x%08x %s\n", __FUNCTION__, 
		   dev_priv->table_start, 
		   dev_priv->table_end, 
		   (u32) dev_priv->table_wptr,
		   wrapped ? "wrapped" : "");

	if ( dev_priv->driver_mode == MACH64_MODE_DMA_SYNC ) {
		if ( (ret = mach64_do_wait_for_idle( dev_priv )) < 0 ) {
			DRM_ERROR( "%s: idle failed after dispatch, resetting engine\n", 
				  __FUNCTION__);
			mach64_dump_engine_info( dev_priv );
			mach64_do_engine_reset( dev_priv );
			mach64_do_release_used_buffers( dev_priv );
			return ret;
		}
		mach64_do_release_used_buffers( dev_priv );
	}

        return 0;
}

static int mach64_do_dispatch_pseudo_dma( drm_mach64_private_t *dev_priv )
{
	struct list_head 	*ptr;
	struct list_head 	*tmp;
	drm_mach64_freelist_t 	*entry;
	drm_buf_t *buf;
	u32 *p;
	u32 used, fifo;
	int ret;

	if ( (ret=mach64_do_wait_for_idle( dev_priv )) < 0) {
		DRM_INFO( "%s: idle failed before dispatch, resetting engine\n", 
			  __FUNCTION__);
		mach64_dump_engine_info( dev_priv );
		mach64_do_engine_reset( dev_priv );
		mach64_do_release_used_buffers( dev_priv );
		return ret;
	}

	list_for_each_safe(ptr, tmp, &dev_priv->dma_queue)
	{
		entry = list_entry(ptr, drm_mach64_freelist_t, list);
		buf = entry->buf;

		/* Hand feed the buffer to the card via MMIO, waiting for the fifo 
		 * every 16 writes 
		 */
		used = buf->used >> 2;
		fifo = 0;

		if (dev_priv->is_pci) {
			p = (u32 *) buf->address;
		} else {
			p = (u32 *)((char *)dev_priv->buffers->handle + buf->offset);
		}

		while ( used ) {
			u32 reg, count;

			reg = le32_to_cpu(*p++);
			used--;

			count = (reg >> 16) + 1;
			reg = reg & 0xffff;
			reg = MMSELECT( reg );
			while ( count && used ) {
				if ( !fifo ) {
					if ( (ret=mach64_do_wait_for_fifo( dev_priv, 16 )) < 0 ) {
						return ret;
					}
					
					fifo = 16;
				}
					--fifo;
				/* data is already little-endian */
				MACH64_WRITE(reg, le32_to_cpu(*p++));
				used--;
				
				reg += 4;
				count--;
			}
		}

		list_del(ptr);
		entry->buf->waiting = 0;
		entry->buf->pending = 1;
		list_add_tail(ptr, &dev_priv->pending);

	}

	/* free the "pending" list, since we're done */
	mach64_do_release_used_buffers( dev_priv );

	DRM_DEBUG( "%s completed\n", __FUNCTION__ );
	return 0;
}

int mach64_do_dma_flush( drm_mach64_private_t *dev_priv )
{
	DRM_DEBUG("%s\n", __FUNCTION__);

	if (list_empty(&dev_priv->dma_queue))
		return 0;

	dev_priv->sarea_priv->dirty |= (MACH64_UPLOAD_CONTEXT |
					MACH64_UPLOAD_MISC);
	

	if (dev_priv->driver_mode == MACH64_MODE_MMIO)
		return mach64_do_dispatch_pseudo_dma( dev_priv );
	else
		return mach64_do_dispatch_real_dma( dev_priv );
}

/* IMPORTANT: This function should only be called when the engine is idle or locked up, 
 * as it assumes all buffers in the pending list have been completed by the hardware.
 */
int mach64_do_release_used_buffers( drm_mach64_private_t *dev_priv )
{
	struct list_head *ptr;
	struct list_head *tmp;
	drm_mach64_freelist_t *entry;
	int i;

	if ( list_empty(&dev_priv->pending) )
		return 0;

	/* Iterate the pending list and move all buffers into the freelist... */
	i = 0;
	list_for_each_safe(ptr, tmp, &dev_priv->pending)
	{
		entry = list_entry(ptr, drm_mach64_freelist_t, list);
		entry->buf->pending = 0;
		list_del(ptr);
		list_add_tail(ptr, &dev_priv->free_list);
		i++;
	}

	DRM_DEBUG( "%s: released %d buffers from pending list\n", __FUNCTION__, i );

        return 0;
}


/* ================================================================
 * DMA cleanup
 */

int mach64_do_cleanup_dma( drm_device_t *dev )
{
	DRM_DEBUG( "%s\n", __FUNCTION__ );

	if ( dev->dev_private ) {
		drm_mach64_private_t *dev_priv = dev->dev_private;

		/* Discard the allocations for the descriptor table... */
		if ( (dev_priv->pool != NULL) && 
		     (dev_priv->cpu_addr_table != NULL) && dev_priv->table_handle ) {
			DRM_INFO( "freeing descriptor table from pci pool\n" );
			pci_pool_free( dev_priv->pool, dev_priv->cpu_addr_table, 
				       dev_priv->table_handle );
		}
		if ( dev_priv->pool != NULL ) {
			DRM_INFO( "destroying pci pool\n" );
			pci_pool_destroy( dev_priv->pool );
		}

		if ( dev_priv->buffers ) {
			DRM_IOREMAPFREE( dev_priv->buffers );
		}

		DRM_INFO( "destroying dma buffer freelist\n" );
		mach64_destroy_freelist( dev );

		DRM(free)( dev_priv, sizeof(drm_mach64_private_t),
			   DRM_MEM_DRIVER );
		dev->dev_private = NULL;
	}
	
	return 0;
}

/* ================================================================
 * IOCTL handlers
 */

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

	return mach64_do_dma_idle( dev_priv );
}

int mach64_dma_flush( struct inode *inode, struct file *filp,
		     unsigned int cmd, unsigned long arg )
{
        drm_file_t *priv = filp->private_data;
        drm_device_t *dev = priv->dev;
	drm_mach64_private_t *dev_priv = dev->dev_private;

	DRM_DEBUG( "%s\n", __FUNCTION__ );

	LOCK_TEST_WITH_RETURN( dev );

	VB_AGE_TEST_WITH_RETURN( dev_priv );

	return mach64_do_dma_flush( dev_priv );
}

int mach64_engine_reset( struct inode *inode, struct file *filp,
			 unsigned int cmd, unsigned long arg )
{
        drm_file_t *priv = filp->private_data;
        drm_device_t *dev = priv->dev;
	drm_mach64_private_t *dev_priv = dev->dev_private;
	
	DRM_DEBUG( "%s\n", __FUNCTION__ );

	LOCK_TEST_WITH_RETURN( dev );

	return mach64_do_engine_reset( dev_priv );
}


/* ================================================================
 * Freelist management
 */

int mach64_init_freelist( drm_device_t *dev )
{
	drm_device_dma_t *dma = dev->dma;
	drm_mach64_private_t *dev_priv = dev->dev_private;
	drm_mach64_freelist_t *entry;
	struct list_head *ptr;
	int i;

	DRM_DEBUG("%s: adding %d buffers to freelist\n", __FUNCTION__, dma->buf_count);

	for ( i = 0 ; i < dma->buf_count ; i++ ) {
		if ((entry = (drm_mach64_freelist_t *) DRM(alloc)(sizeof(drm_mach64_freelist_t), DRM_MEM_BUFLISTS)) == NULL)
			return -ENOMEM;
		memset( entry, 0, sizeof(drm_mach64_freelist_t) );
		entry->buf = dma->buflist[i];
#if MACH64_USE_BUFFER_AGING
		entry->age = 0;
#endif
		ptr = &entry->list;
		list_add_tail(ptr, &dev_priv->free_list);
	}

	return 0;
}

void mach64_destroy_freelist( drm_device_t *dev )
{
	drm_mach64_private_t *dev_priv = dev->dev_private;
	drm_mach64_freelist_t *entry;
	struct list_head *ptr;
	struct list_head *tmp;

	DRM_DEBUG("%s\n", __FUNCTION__);

	list_for_each_safe(ptr, tmp, &dev_priv->pending)
	{
		list_del(ptr);
		entry = list_entry(ptr, drm_mach64_freelist_t, list);
		DRM(free)(entry, sizeof(*entry), DRM_MEM_BUFLISTS);
	}

	list_for_each_safe(ptr, tmp, &dev_priv->dma_queue)
	{
		list_del(ptr);
		entry = list_entry(ptr, drm_mach64_freelist_t, list);
		DRM(free)(entry, sizeof(*entry), DRM_MEM_BUFLISTS);
	}

	list_for_each_safe(ptr, tmp, &dev_priv->empty_list)
	{
		list_del(ptr);
		entry = list_entry(ptr, drm_mach64_freelist_t, list);
		DRM(free)(entry, sizeof(*entry), DRM_MEM_BUFLISTS);
	}

	list_for_each_safe(ptr, tmp, &dev_priv->free_list)
	{
		list_del(ptr);
		entry = list_entry(ptr, drm_mach64_freelist_t, list);
		DRM(free)(entry, sizeof(*entry), DRM_MEM_BUFLISTS);
	}
}

drm_buf_t *mach64_freelist_get( drm_mach64_private_t *dev_priv )
{
	drm_mach64_freelist_t *entry;
	struct list_head *ptr;
	struct list_head *tmp;
	int t;

	if ( list_empty(&dev_priv->free_list) ) {
#if !MACH64_USE_BUFFER_AGING
		u32 address, start, end;
#else
		u32 done_age = 0;
#endif
		if ( list_empty( &dev_priv->pending ) ) {
			/* All 3 lists should never be empty - this is here for debugging */
			if ( list_empty( &dev_priv->dma_queue ) ) {
				DRM_ERROR( "Couldn't get buffer - all lists empty\n" );
				return NULL;
			} else {
				/* There's nothing to recover, so flush the queue */
				DRM_DEBUG("Flushing queue in freelist_get\n");
				mach64_do_dma_flush( dev_priv );
			}
		}
#if !MACH64_USE_BUFFER_AGING
		end = dev_priv->table_end;
#endif
		for ( t = 0 ; t < dev_priv->usec_timeout ; t++ ) {
#if !MACH64_USE_BUFFER_AGING
			GET_RING_HEAD( dev_priv );
			start = dev_priv->table_start;

			if ( start == end ) {
				/* If this is the last descriptor, need to check for idle */
				if (!(MACH64_READ(MACH64_GUI_STAT) & MACH64_GUI_ACTIVE) ) {
					/* last pass is complete, so release everything */
					mach64_do_release_used_buffers( dev_priv );
					DRM_DEBUG( "%s: idle engine, freed all buffers.\n", __FUNCTION__ );
					goto _freelist_entry_found;
				}
			}
#else
			done_age = MACH64_READ( MACH64_LAST_DISPATCH_REG );
#endif
			/* Look for a completed buffer and bail out of the loop 
			 * as soon as we find one -- don't waste time trying
			 * to free extra bufs here, leave that to do_release_used_buffers
			 */
			list_for_each_safe(ptr, tmp, &dev_priv->pending) {
				entry = list_entry(ptr, drm_mach64_freelist_t, list);
#if !MACH64_USE_BUFFER_AGING
				address = entry->descr_addr;
				if ( (start < end && (address < start || address > end)) ||
				     (start > end && (address < start && address > end)) ) {
					/* found a processed buffer */
					entry->buf->pending = 0;
					list_del(ptr);
					list_add_tail(ptr, &dev_priv->free_list);
					DRM_DEBUG( "%s: freed processed buffer (start=0x%08x end=0x%08x address=0x%08x).\n", __FUNCTION__, start, end, address );
					goto _freelist_entry_found;
				}
#else
				if (entry->age <= done_age && done_age > 0) {
					/* found a processed buffer */
					entry->buf->pending = 0;
					list_del(ptr);
					list_add_tail(ptr, &dev_priv->free_list);
					DRM_DEBUG( "%s: freed processed buffer (buffer age: %d last dispatch reg: %d last_dispatch: %d\n", __FUNCTION__, entry->age, done_age, dev_priv->sarea_priv->last_dispatch );
					goto _freelist_entry_found;
				}
#endif
			}
			udelay( 1 );
		}
#if !MACH64_USE_BUFFER_AGING
		DRM_ERROR( "timeout waiting for buffers: table start: 0x%08x table_end: 0x%08x\n", dev_priv->table_start, dev_priv->table_end );
#else
		DRM_ERROR( "timeout waiting for buffers: last dispatch reg: %d last_dispatch: %d\n", done_age, dev_priv->sarea_priv->last_dispatch );
#endif
		return NULL;
	}

_freelist_entry_found:
	ptr = dev_priv->free_list.next;
	list_del(ptr);
	entry = list_entry(ptr, drm_mach64_freelist_t, list);
	entry->buf->used = 0;
	list_add_tail(ptr, &dev_priv->empty_list);
	return entry->buf;
}

#if MACH64_USE_BUFFER_AGING

/* Engine must be idle and buffers recleaimed before calling this */
void mach64_freelist_reset( drm_mach64_private_t *dev_priv )
{
	drm_mach64_freelist_t *entry;
	struct list_head *ptr;

	DRM_DEBUG("%s\n", __FUNCTION__);

	list_for_each(ptr, &dev_priv->dma_queue)
	{
		entry = list_entry(ptr, drm_mach64_freelist_t, list);
		entry->age = 0;
	}

	list_for_each(ptr, &dev_priv->empty_list)
	{
		entry = list_entry(ptr, drm_mach64_freelist_t, list);
		entry->age = 0;
	}

	list_for_each(ptr, &dev_priv->free_list)
	{
		entry = list_entry(ptr, drm_mach64_freelist_t, list);
		entry->age = 0;
	}

	/* Pending list should be empty if engine is idle and buffers were released */
	if (!list_empty(&dev_priv->pending)) {
		DRM_ERROR("Pending list not empty in freelist_reset!\n");
	}

}

#endif /* MACH64_USE_BUFFER_AGING */

/* ================================================================
 * DMA buffer request and submission IOCTL handler
 */

static int mach64_dma_get_buffers( drm_device_t *dev, drm_dma_t *d )
{
	int i;
	drm_buf_t *buf;
	drm_mach64_private_t *dev_priv = dev->dev_private;

	for ( i = d->granted_count ; i < d->request_count ; i++ ) {
		buf = mach64_freelist_get( dev_priv );
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

/*
        Through some pretty thorough testing, it has been found that the 
        RagePRO engine will pretty much ignore any "commands" sent
        via the gui-master pathway that aren't gui operations (the register
        gets set, but the actions that are normally associated with the setting
        of those said registers doesn't happen.).  So, it's safe to send us
        buffers of gui commands from userspace (altering the buffer in mid-
        execution will at worst scribble all over the screen and pushing
        bogus commands will have no apparent effect...)

        FCE (03-08-2002)
*/
int mach64_dma_buffers( struct inode *inode, struct file *filp,
			unsigned int cmd, unsigned long arg )
{
        drm_file_t *priv = filp->private_data;
        drm_device_t *dev = priv->dev;
	drm_mach64_private_t *dev_priv = dev->dev_private;
        drm_device_dma_t *dma = dev->dma;
	drm_dma_t d;
        int ret = 0;
	int i;
	drm_buf_t *buf;

        LOCK_TEST_WITH_RETURN( dev );

        if ( copy_from_user( &d, (drm_dma_t *)arg, sizeof(d) ) )
        {
                return -EFAULT;
        }

        /* Queue up buffers sent to us...
        */
        if ( d.send_count > 0 ) 
        {
		for (i = 0; i < d.send_count ; i++)
		{
			buf = dma->buflist[d.send_indices[i]];
			if (buf->pending) {
				DRM_ERROR( "sending pending buffer %d\n", d.send_indices[i] );
				return -EINVAL;
			}
			/* Add buf to queue */
			DMAADVANCE( dev_priv );
		}
        }
        else
        {
                /* Send the caller as many as they ask for, so long as we
                   have them in hand to give...
                */
                if ( d.request_count < 0 || d.request_count > dma->buf_count ) 
                {
                        DRM_ERROR( "Process %d trying to get %d buffers (of %d max)\n",
                                current->pid, d.request_count, dma->buf_count );
                        ret = -EINVAL;
                }
                else
                {
                        d.granted_count = 0;

                        if ( d.request_count ) 
                        {
                                ret = mach64_dma_get_buffers( dev, &d );
                        }

                        if ( copy_to_user( (drm_dma_t *)arg, &d, sizeof(d) ) )
                        {
                                ret = -EFAULT;
                        }
                }
        }
        
        return ret;
}

