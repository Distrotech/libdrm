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
 */

#include "mach64.h"
#include "drmP.h"
#include "mach64_drv.h"

#include <linux/config.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/pci_ids.h>
#include <linux/compatmac.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/interrupt.h>                /* For task queue support */
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <asm/processor.h>
#include <asm/io.h>

int mach64_do_dma_cleanup( drm_device_t *dev );
int mach64_do_engine_reset( drm_mach64_private_t *dev_priv );
int mach64_handle_dma( drm_mach64_private_t *dev_priv );
int mach64_do_dispatch_dma( drm_mach64_private_t *dev_priv );
int mach64_do_complete_blit( drm_mach64_private_t *dev_priv );
int mach64_do_wait_for_dma( drm_mach64_private_t *dev_priv );
int mach64_do_release_used_buffers( drm_mach64_private_t *dev_priv );
int mach64_init_freelist( drm_mach64_private_t *dev_priv );
int mach64_dma_get_buffers( drm_device_t *dev, drm_dma_t *d );

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
        if (flags & 0x00000004)
        {                
                /* VBLANK -- GUI-master dispatch and polling... */
                MACH64_WRITE(MACH64_CRTC_INT_CNTL, flags | 0x000000004);
                atomic_inc(&dev_priv->do_gui);
        }
        if (flags & 0x02000000)
        {                
                /* Completion of BLIT op */
                MACH64_WRITE(MACH64_CRTC_INT_CNTL, flags | 0x02000000);
                atomic_inc(&dev_priv->do_blit);
        }
                
        /* Check for an error condition in the engine...  */
        if (MACH64_READ(MACH64_FIFO_STAT) & 0x80000000) 
        {
                /* This would be a failure to maintain FIFO discipline
                   per the SDK sources.   Need to reset... */
                mach64_do_engine_reset(dev_priv);
        }
        if (MACH64_READ(MACH64_BUS_CNTL) & 0x00200000)
        {
                /* This would be a host data error, per information from
                   Vernon Chiang @ ATI (Thanks, Vernon!).  Need to reset... */
                mach64_do_engine_reset(dev_priv);
        }

        /* Ok, now that we've gotten that out of the way, schedule the bottom half accordingly... */
	queue_task(&dev->tq, &tq_immediate);
	mark_bh(IMMEDIATE_BH);
        
        return;
}


/* Handle the DMA dispatch/completion */
void DRM(dma_immediate_bh)(void *device)
{
        drm_device_t *dev = (drm_device_t *) device;
        drm_mach64_private_t *dev_priv = (drm_mach64_private_t *)dev->dev_private;

        /* Handle the completion of a blit pass... */
        if (atomic_read(&dev_priv->do_blit) > 0)
        {
                atomic_set(&dev_priv->do_blit, 0);                
                mach64_do_complete_blit(dev_priv);
        }        

        /* Check to see if we've been told to handle gui-mastering... */
        if (atomic_read(&dev_priv->do_gui) > 0)
        {
                atomic_set(&dev_priv->do_gui, 0);                
                mach64_handle_dma(dev_priv);
        }
        
        wake_up_interruptible(&read_wait);
        return;
}


/* ================================================================
 * Engine control
 */

int mach64_do_wait_for_fifo( drm_mach64_private_t *dev_priv, int entries )
{
        int slots = 0, i;

        for ( i = 0 ; i < dev_priv->usec_timeout ; i++ ) 
	{
                slots = (MACH64_READ( MACH64_FIFO_STAT ) &
                         MACH64_FIFO_SLOT_MASK);
                if ( slots <= (0x8000 >> entries) ) return 0;
                udelay( 1 );
        }

        DRM_INFO( "do_wait_for_fifo failed! slots=%d entries=%d\n", slots, entries );
        return -EBUSY;
}

int mach64_do_wait_for_idle( drm_mach64_private_t *dev_priv )
{
        int i, ret;

        ret = mach64_do_wait_for_fifo( dev_priv, 16 );
        if ( ret < 0 ) return ret;

        for ( i = 0 ; i < dev_priv->usec_timeout ; i++ ) 
	{
                if ( !(MACH64_READ( MACH64_GUI_STAT ) & MACH64_GUI_ACTIVE) ) 
		{
                        return 0;
                }
                udelay( 1 );
        }

        DRM_INFO( "do_wait_for_idle failed! GUI_STAT=0x%08x\n", MACH64_READ( MACH64_GUI_STAT ) );
        return -EBUSY;
}

/* Wait until all DMA requests have been processed... */
int mach64_do_wait_for_dma( drm_mach64_private_t *dev_priv )
{
	int i, ret;

	/* Assume we timeout... */
	ret = -EBUSY;

	for ( i = 0 ; i < dev_priv->usec_timeout ; i++ ) 
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

void dump_engine_info( drm_mach64_private_t *dev_priv )
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


static void bm_dma_test(drm_mach64_private_t *dev_priv)
{
        dma_addr_t data_handle;
        u32 data_addr;
        u32 *data;

        void *cpu_addr_data;
        int i;


        DRM_INFO( "Allocating data memory ...\n" );
        cpu_addr_data = pci_pool_alloc( dev_priv->pool, SLAB_ATOMIC, &data_handle );
        if (!cpu_addr_data || !data_handle) 
        {
                DRM_INFO( "data-memory allocation failed!\n" );
                return;
        } 
        else 
        {
                data = (u32 *) cpu_addr_data;
                data_addr = (u32) data_handle;
        }

        MACH64_WRITE( MACH64_SRC_CNTL, 0x00000000 );
        MACH64_WRITE( MACH64_PAT_REG0, 0x11111111 );

        DRM_INFO( "(Before DMA Transfer) PAT_REG0 = 0x%08x\n", MACH64_READ( MACH64_PAT_REG0 ) );

        data[0] = 0x000000a0;
        data[1] = 0x22222222;
        data[2] = 0x000000a0;
        data[3] = 0x22222222;
        data[4] = 0x000000a0;
        data[5] = 0x22222222;
        data[6] = 0x0000006d;
        data[7] = 0x00000000;

        DRM_INFO( "Preparing table ...\n" );
        memset( dev_priv->cpu_addr_table, 0x0, 0x4000 );
        dev_priv->table[0] = MACH64_BM_ADDR + APERTURE_OFFSET;
        dev_priv->table[1] = data_addr;
        dev_priv->table[2] = 8 * sizeof( u32 ) | 0x80000000 | 0x40000000;
        dev_priv->table[3] = 0;

        DRM_INFO( "table[0] = 0x%08x\n", dev_priv->table[0] );
        DRM_INFO( "table[1] = 0x%08x\n", dev_priv->table[1] );
        DRM_INFO( "table[2] = 0x%08x\n", dev_priv->table[2] );
        DRM_INFO( "table[3] = 0x%08x\n", dev_priv->table[3] );

        for ( i = 0 ; i < 8 ; i++) 
        {
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
        DRM_INFO( "table = 0x%08x\n", dev_priv->table_addr );

        DRM_INFO( "starting DMA transfer...\n" );
        MACH64_WRITE( MACH64_BM_GUI_TABLE,
                         dev_priv->table_addr |
                      MACH64_CIRCULAR_BUF_SIZE_16KB );

        MACH64_WRITE( MACH64_SRC_CNTL, 
                      MACH64_SRC_BM_ENABLE | MACH64_SRC_BM_SYNC |
                      MACH64_SRC_BM_OP_SYSTEM_TO_REG );

        /* Kick off the transfer */
        DRM_INFO( "starting DMA transfer... done.\n" );
        MACH64_WRITE( MACH64_DST_HEIGHT_WIDTH, 0 );
        MACH64_WRITE( MACH64_SRC_CNTL, 0 );

        DRM_INFO( "waiting for idle [locked_after_dma??]...\n" );
        if ((i=mach64_do_wait_for_idle( dev_priv ))) 
        {
                DRM_INFO( "mach64_do_wait_for_idle failed (result=%d)\n", i);
                DRM_INFO( "resetting engine ...");
                mach64_do_engine_reset( dev_priv );
        }

        DRM_INFO( "(After DMA Transfer) PAT_REG0 = 0x%08x\n", MACH64_READ( MACH64_PAT_REG0 ) );

        DRM_INFO( "freeing memory.\n" );
        pci_pool_free( dev_priv->pool, cpu_addr_data, data_handle );
        DRM_INFO( "returning ...\n" );
}

int mach64_do_engine_reset( drm_mach64_private_t *dev_priv )
{
        u32 tmp;

        DRM_DEBUG( "%s\n", __FUNCTION__ );

        /* Kill off any outstanding DMA transfers.
         */
        tmp = MACH64_READ( MACH64_BUS_CNTL );
        MACH64_WRITE( MACH64_BUS_CNTL, tmp | MACH64_BUS_MASTER_DIS );

        /* Reset the GUI engine (high to low transition).
         */
        tmp = MACH64_READ( MACH64_GEN_TEST_CNTL );
        MACH64_WRITE( MACH64_GEN_TEST_CNTL, tmp & ~MACH64_GUI_ENGINE_ENABLE );
        
        /* Enable the GUI engine
         */
        tmp = MACH64_READ( MACH64_GEN_TEST_CNTL );
        MACH64_WRITE( MACH64_GEN_TEST_CNTL, tmp | MACH64_GUI_ENGINE_ENABLE );

        /* ensure engine is not locked up by clearing any FIFO or HOST errors
        */
        tmp = MACH64_READ( MACH64_BUS_CNTL );
        MACH64_WRITE( MACH64_BUS_CNTL, tmp | 0x00a00000 );

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

        dev_priv->is_pci        = init->is_pci;
                
        dev_priv->fb_bpp        = init->fb_bpp;
        dev_priv->front_offset  = init->front_offset;
        dev_priv->front_pitch   = init->front_pitch;
        dev_priv->back_offset   = init->back_offset;
        dev_priv->back_pitch    = init->back_pitch;

        dev_priv->depth_bpp     = init->depth_bpp;
        dev_priv->depth_offset  = init->depth_offset;
        dev_priv->depth_pitch   = init->depth_pitch;

        dev_priv->front_offset_pitch        = (((dev_priv->front_pitch/8) << 22) |
                                           (dev_priv->front_offset >> 3));
        dev_priv->back_offset_pitch        = (((dev_priv->back_pitch/8) << 22) |
                                           (dev_priv->back_offset >> 3));
        dev_priv->depth_offset_pitch        = (((dev_priv->depth_pitch/8) << 22) |
                                           (dev_priv->depth_offset >> 3));

        dev_priv->usec_timeout                = 1000000;

        list_for_each(list, &dev->maplist->head) {
                drm_map_list_t *r_list = (drm_map_list_t *)list;
                if( r_list->map && 
                    r_list->map->type == _DRM_SHM &&
                    r_list->map->flags & _DRM_CONTAINS_LOCK ) {
                        dev_priv->sarea = r_list->map;
                        break;
                }
        }
        if(!dev_priv->sarea) {
                dev->dev_private = (void *)dev_priv;
                   mach64_do_dma_cleanup(dev);
                   DRM_ERROR("can not find sarea!\n");
                   return -EINVAL;
        }
        DRM_FIND_MAP( dev_priv->fb, init->fb_offset );
        if(!dev_priv->fb) {
                dev->dev_private = (void *)dev_priv;
                   mach64_do_dma_cleanup(dev);
                   DRM_ERROR("can not find frame buffer map!\n");
                   return -EINVAL;
        }
        DRM_FIND_MAP( dev_priv->mmio, init->mmio_offset );
        if(!dev_priv->mmio) {
                dev->dev_private = (void *)dev_priv;
                   mach64_do_dma_cleanup(dev);
                   DRM_ERROR("can not find mmio map!\n");
                   return -EINVAL;
        }

        dev_priv->sarea_priv = (drm_mach64_sarea_t *)
                ((u8 *)dev_priv->sarea->handle +
                 init->sarea_priv_offset);

        if( !dev_priv->is_pci ) {
                DRM_FIND_MAP( dev_priv->buffers, init->buffers_offset );
                if( !dev_priv->buffers ) {
                        dev->dev_private = (void *)dev_priv;
                        mach64_do_dma_cleanup( dev );
                        DRM_ERROR( "can not find dma buffer map!\n" );
                        return -EINVAL;
                }
                DRM_IOREMAP( dev_priv->buffers );
                if( !dev_priv->buffers->handle ) {
                        dev->dev_private = (void *) dev_priv;
                        mach64_do_dma_cleanup( dev );
                        DRM_ERROR( "can not ioremap virtual address for"
                                   " dma buffer\n" );
                        return -ENOMEM;
                }
        }

        DRM_FIND_MAP( dev_priv->fb, init->fb_offset );
        DRM_FIND_MAP( dev_priv->mmio, init->mmio_offset );
        DRM_FIND_MAP( dev_priv->buffers, init->buffers_offset );

        /* Set up our global descriptor table for DMA operation */
        DRM_INFO( "Creating pool ... \n");
        dev_priv->pool = pci_pool_create( "mach64", NULL, 0x4000, 0x4000, 0x4000, SLAB_ATOMIC );
        if (dev_priv->pool != NULL) 
        {
                DRM_INFO( "pci_pool_create failed!\n" );
                return -ENOMEM;
        }
        DRM_INFO( "Allocating desctriptor table memory ...\n" );
        dev_priv->cpu_addr_table = pci_pool_alloc( dev_priv->pool, SLAB_ATOMIC, &dev_priv->table_handle );
        if ((dev_priv->cpu_addr_table != NULL) || !dev_priv->table_handle) 
        {
                DRM_INFO( "Descriptor table memory allocation failed!\n" );
                pci_pool_destroy( dev_priv->pool );
                return -ENOMEM;
        } 
        else 
        {
                dev_priv->table = (u32 *) dev_priv->cpu_addr_table;
                dev_priv->table_addr = (u32) dev_priv->table_handle;
        }
        
        /* Turn on bus-mastering support in the GPU... */
        tmp = MACH64_READ( MACH64_BUS_CNTL );
        tmp = ( tmp | MACH64_BUS_EXT_REG_EN ) & ~MACH64_BUS_MASTER_DIS;
        MACH64_WRITE( MACH64_BUS_CNTL, tmp );

        /* For now, forcibly change the FIFO size back to the stock value- something about
           the changes made by the XAA driver mess up DMA support badly (Locking up the 
	   X server's NOT good, if you ask me...). */
        tmp = MACH64_READ( MACH64_GUI_CNTL );
        MACH64_WRITE(MACH64_GUI_CNTL, ((tmp & ~MACH64_CMDFIFO_SIZE_MASK) | MACH64_CMDFIFO_SIZE_128));
        DRM_INFO( "GUI_STAT=0x%08x\n", MACH64_READ( MACH64_GUI_STAT ) );
        DRM_INFO( "GUI_CNTL=0x%08x\n", MACH64_READ( MACH64_GUI_CNTL ) );

	/* Set up the freelist, empty (placeholder), pending, and DMA request queues... */
	INIT_LIST_HEAD(&dev_priv->free_list);
	INIT_LIST_HEAD(&dev_priv->empty_list);
	INIT_LIST_HEAD(&dev_priv->pending);
	INIT_LIST_HEAD(&dev_priv->dma_queue);
	mach64_init_freelist( dev_priv );	
	        
        /* Set up for interrupt handling proper- clear state on the handler and tell
           the GPU that we want interrupts for VBLANK, BLIT, and Host data errors... */
        atomic_set(&dev_priv->do_gui, 0);                
        atomic_set(&dev_priv->do_blit, 0);                
        atomic_set(&dev_priv->dma_timeout, -1);	
	
        /* Run a quick and dirty test of the engine */
        bm_dma_test( dev_priv );
        
        return 0;
}


/*
	Manage the GUI-Mastering operations of the chip.  Since the GUI-Master
	operation is slightly less intelligent than the BLIT operation (no interrupt
	for completion), we have to provide the completion detection, etc. in 
	a state engine.
*/
int mach64_handle_dma( drm_mach64_private_t *dev_priv )
{
	struct list_head 		*ptr;
	int 				i;
	int				timeout;
		
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
			ptr = dev_priv->dma_queue.next;
			for(i = 0; i < MACH64_DMA_SIZE && !list_empty(&dev_priv->dma_queue); i++)
			{
				/* FIXME -- We REALLY need to be doing this based off of not just
				   a DMA-able size that's tolerable, but also rounding up/down by
				   what was submitted to us- if the client's submitting 3 buffer
				   submits, we really want to push all three at the same time to
				   the DMA channel. */
				list_del(ptr);
				list_add_tail(&dev_priv->pending, ptr);
			}
			atomic_set(&dev_priv->dma_timeout, 0);
		}
		
		/* Check to see if we've got a DMA pass set up */
		if (atomic_read(&dev_priv->dma_timeout) == 0)
		{
			/* Make sure we're locked and fire off the prepped pass */
			mach64_do_dispatch_dma(dev_priv);
		}
	}
	else
	{
		/* Check to see if we've got a GUI-Master going... */
		if ((timeout > -1) && (MACH64_READ( MACH64_BUS_CNTL ) & ~ MACH64_BUS_MASTER_DIS))
		{
			/* Check for DMA timeout */
			if (timeout > MACH64_DMA_TIMEOUT)
			{
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
	Take the pending list and build up a descriptor table for
	GUI-Master use, then fire off the DMA engine with the list.
	(The list includes a register reset buffer that the DRM
	only controls)
*/
int mach64_do_dispatch_dma( drm_mach64_private_t *dev_priv )
{
	struct list_head 	*ptr;	
	drm_mach64_freelist_t 	entry;
	
	/* Iterate the pending list build a descriptor table accordingly... */	
	list_for_each(ptr, &dev_priv->pending)
	{
		
	}
	
	/* Now, dispatch the whole lot to the gui-master engine */

        return 0;
}


/* 
	Release completed, releaseable buffers to the freelist, currently 
	ignore flags for buffers that aren't flagged for release (shouldn't 
	be any, but you never know what someone's going to do to us...).
*/
int mach64_do_release_used_buffers( drm_mach64_private_t *dev_priv )
{
	struct list_head *ptr;	
	struct list_head *tmp;	
	
	/* Iterate the pending list and shove the whole lot into the freelist... */	
	list_for_each_safe(ptr, tmp, &dev_priv->pending)
	{
		list_del(ptr);
		list_add_tail(&dev_priv->free_list, ptr);
	}
	
        return 0;
}


int mach64_do_dma_cleanup( drm_device_t *dev )
{
        DRM_DEBUG( "%s\n", __FUNCTION__ );
        
        if ( dev->dev_private ) 
        {
                drm_mach64_private_t *dev_priv = dev->dev_private;
                
                /* First things, first- kill GPU interrupt support */
                
                /* Discard the allocations for the descriptor table... */
                if ((dev_priv->cpu_addr_table != NULL) && !dev_priv->table_handle)
                {
                        pci_pool_free( dev_priv->pool, dev_priv->cpu_addr_table, dev_priv->table_handle );
                }
                if (dev_priv->pool != NULL)
                {
                        pci_pool_destroy( dev_priv->pool );
                }
                
		if (dev_priv->buffers)
		{
			DRM_IOREMAPFREE( dev_priv->buffers );
		}               
		
                DRM(free)( dev_priv, sizeof(drm_mach64_private_t), DRM_MEM_DRIVER );
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
                
        DRM_DEBUG( "%s\n", __FUNCTION__ );

        if ( copy_from_user( &init, (drm_mach64_init_t *)arg, sizeof(init) ) )
                return -EFAULT;

        switch ( init.func ) 
	{
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
             dev->lock.pid != current->pid ) 
	{
                DRM_ERROR( "%s called without lock held\n", __FUNCTION__ );
                return -EINVAL;
        }
                
        return mach64_do_wait_for_idle( dev_priv );
}


/* 
	This is pretty simple- either the requested number of buffers exist in the
	freelist or not.  Fetch as many as available
*/
int mach64_dma_get_buffers( drm_device_t *dev, drm_dma_t *d )
{
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
        drm_device_dma_t *dma = dev->dma;
	struct list_head *ptr;
	drm_mach64_freelist_t *entry;
        drm_mach64_private_t *dev_priv = (drm_mach64_private_t *)dev->dev_private;
        drm_dma_t d;
        int ret = 0;
	int i;

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
			if (!list_empty(&dev_priv->empty_list))
			{
				ptr = dev_priv->empty_list.next;
				list_del(ptr);
				entry = list_entry(ptr, drm_mach64_freelist_t, list);
				entry->buf = dma->buflist[d.send_indices[i]];
				list_add_tail(&dev_priv->dma_queue, ptr);
			}
			else
			{
				return -EFAULT;
			}
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
