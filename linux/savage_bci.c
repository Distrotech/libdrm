/* savage_bci.c -- BCI support for Savage -*- linux-c -*-
 *
 * Copyright 2000 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Fremont, California.
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
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Kevin E. Martin <martin@valinux.com>
 *    Gareth Hughes <gareth@valinux.com>
 */

#include "savage.h"
#include "drmP.h"
#include "drm.h"
#include "savage_drm.h"
#include "savage_drv.h"

/* ================================================================
 * BCI control, initialization
 */

static int savage_do_init_bci( drm_device_t *dev, drm_savage_init_t *init )
{
	drm_savage_private_t *dev_priv;
	u32 tmp;
	DRM_INFO( "initializing bci ...\n" );

	dev_priv = DRM(alloc)( sizeof(drm_savage_private_t), DRM_MEM_DRIVER );
	if ( dev_priv == NULL ) {
		DRM_ERROR( "memory allocation for dev_priv failed!\n" );
		return DRM_ERR(ENOMEM);
	}

	memset( dev_priv, 0, sizeof(drm_savage_private_t) );

	dev_priv->is_pci = init->is_pci;

	if ( dev_priv->is_pci && !dev->sg ) {
		DRM_ERROR( "PCI GART memory not allocated!\n" );
		dev->dev_private = (void *)dev_priv;
		savage_do_cleanup_bci(dev);
		return DRM_ERR(EINVAL);
	}

	dev_priv->usec_timeout = init->usec_timeout;
	if ( dev_priv->usec_timeout < 1 ||
	     dev_priv->usec_timeout > SAVAGE_MAX_USEC_TIMEOUT ) {
		DRM_ERROR( "TIMEOUT problem!\n" );
		dev->dev_private = (void *)dev_priv;
		savage_do_cleanup_bci(dev);
		return DRM_ERR(EINVAL);
	}

	dev_priv->do_boxes = 0;

	switch ( init->fb_bpp ) {
	case 16:
/* FIXME:		dev_priv->color_fmt = SAVAGE_COLOR_FORMAT_RGB565; */
		break;
	case 32:
	default:
/* FIXME:		dev_priv->color_fmt = SAVAGE_COLOR_FORMAT_ARGB8888; */
		break;
	}
	dev_priv->front_offset	= init->front_offset;
	dev_priv->front_pitch	= init->front_pitch;
	dev_priv->back_offset	= init->back_offset;
	dev_priv->back_pitch	= init->back_pitch;

	switch ( init->depth_bpp ) {
	case 16:
/* FIXME: dev_priv->depth_fmt = SAVAGE_DEPTH_FORMAT_16BIT_INT_Z; */
		break;
	case 32:
	default:
/* FIXME: dev_priv->depth_fmt = SAVAGE_DEPTH_FORMAT_24BIT_INT_Z; */
		break;
	}
	dev_priv->depth_offset	= init->depth_offset;
	dev_priv->depth_pitch	= init->depth_pitch;

	dev_priv->front_pitch_offset = (((dev_priv->front_pitch/64) << 22) |
					(dev_priv->front_offset >> 10));
	dev_priv->back_pitch_offset = (((dev_priv->back_pitch/64) << 22) |
				       (dev_priv->back_offset >> 10));
	dev_priv->depth_pitch_offset = (((dev_priv->depth_pitch/64) << 22) |
					(dev_priv->depth_offset >> 10));

	DRM_INFO( "looking for sarea ...\n" );
	DRM_GETSAREA();
	
	if(!dev_priv->sarea) {
		DRM_ERROR("could not find sarea!\n");
		dev->dev_private = (void *)dev_priv;
		savage_do_cleanup_bci(dev);
		return DRM_ERR(EINVAL);
	}

	DRM_INFO( "looking for framebuffer ...\n" );
	DRM_FIND_MAP( dev_priv->fb, init->fb_offset );
	if(!dev_priv->fb) {
		DRM_ERROR("could not find framebuffer!\n");
		dev->dev_private = (void *)dev_priv;
		savage_do_cleanup_bci(dev);
		return DRM_ERR(EINVAL);
	}

	DRM_INFO( "looking for mmio region ...\n" );
	DRM_FIND_MAP( dev_priv->mmio, init->mmio_offset );
	if(!dev_priv->mmio) {
		DRM_ERROR("could not find mmio region!\n");
		dev->dev_private = (void *)dev_priv;
		savage_do_cleanup_bci(dev);
		return DRM_ERR(EINVAL);
	}

	DRM_INFO( "looking for bci region ...\n" );
	DRM_FIND_MAP( dev_priv->bci, init->bci_offset );
	if(!dev_priv->bci) {
		DRM_ERROR("could not find bci region!\n");
		dev->dev_private = (void *)dev_priv;
		savage_do_cleanup_bci(dev);
		return DRM_ERR(EINVAL);
	}
/* FIXME: we do not support this right now */
#if 0
	DRM_FIND_MAP( dev_priv->buffers, init->buffers_offset );
	if(!dev_priv->buffers) {
		DRM_ERROR("could not find dma buffer region!\n");
		dev->dev_private = (void *)dev_priv;
		savage_do_cleanup_bci(dev);
		return DRM_ERR(EINVAL);
	}

	if ( !dev_priv->is_pci ) {
		DRM_INFO( "looking for agp texture region ...\n" );
		DRM_FIND_MAP( dev_priv->agp_textures,
			      init->agp_textures_offset );
		if(!dev_priv->agp_textures) {
			DRM_ERROR("could not find agp texture region!\n");
			dev->dev_private = (void *)dev_priv;
			savage_do_cleanup_bci(dev);
			return DRM_ERR(EINVAL);
		}
	}

#endif

	dev_priv->sarea_priv =
		(drm_savage_sarea_t *)((u8 *)dev_priv->sarea->handle +
				       init->sarea_priv_offset);

	if ( !dev_priv->is_pci ) {
		/* FIXME: we do not support this right now */
#if 0
		DRM_IOREMAP( dev_priv->bci );
		DRM_IOREMAP( dev_priv->buffers );
		if(!dev_priv->bci->handle ||
		   !dev_priv->buffers->handle) {
			DRM_ERROR("could not find ioremap agp regions!\n");
			dev->dev_private = (void *)dev_priv;
			savage_do_cleanup_bci(dev);
			return DRM_ERR(EINVAL);
		}
#endif
	} else {
		dev_priv->bci->handle =
			(void *)dev_priv->bci->offset;
		dev_priv->buffers->handle = (void *)dev_priv->buffers->offset;

		DRM_DEBUG( "dev_priv->bci->handle %p\n",
			   dev_priv->bci->handle );
		DRM_DEBUG( "dev_priv->buffers->handle %p\n",
			   dev_priv->buffers->handle );
	}


	dev_priv->agp_size = init->agp_size;
/* FIXME:	dev_priv->agp_vm_start = SAVAGE_READ( SAVAGE_CONFIG_APER_SIZE ); */
#if __REALLY_HAVE_AGP
	if ( !dev_priv->is_pci )
		dev_priv->agp_buffers_offset = (dev_priv->buffers->offset
						- dev->agp->base
						+ dev_priv->agp_vm_start);
	else
#endif
		dev_priv->agp_buffers_offset = (dev_priv->buffers->offset
						- dev->sg->handle
						+ dev_priv->agp_vm_start);

	DRM_DEBUG( "dev_priv->agp_size %d\n",
		   dev_priv->agp_size );
	DRM_DEBUG( "dev_priv->agp_vm_start 0x%x\n",
		   dev_priv->agp_vm_start );
	DRM_DEBUG( "dev_priv->agp_buffers_offset 0x%lx\n",
		   dev_priv->agp_buffers_offset );

#if __REALLY_HAVE_SG
	if ( dev_priv->is_pci ) {
		if (!DRM(ati_pcigart_init)( dev, &dev_priv->phys_pci_gart,
					    &dev_priv->bus_pci_gart)) {
			DRM_ERROR( "failed to init PCI GART!\n" );
			dev->dev_private = (void *)dev_priv;
			savage_do_cleanup_bci(dev);
			return DRM_ERR(ENOMEM);
		}
		/* Turn on PCI GART
		 */
		tmp = SAVAGE_READ( SAVAGE_AIC_CNTL )
		      | SAVAGE_PCIGART_TRANSLATE_EN;
		SAVAGE_WRITE( SAVAGE_AIC_CNTL, tmp );

		/* set PCI GART page-table base address
		 */
		SAVAGE_WRITE( SAVAGE_AIC_PT_BASE, dev_priv->bus_pci_gart );

		/* set address range for PCI address translate
		 */
		SAVAGE_WRITE( SAVAGE_AIC_LO_ADDR, dev_priv->agp_vm_start );
		SAVAGE_WRITE( SAVAGE_AIC_HI_ADDR, dev_priv->agp_vm_start
						  + dev_priv->agp_size - 1);

		/* Turn off AGP aperture -- is this required for PCIGART?
		 */
		SAVAGE_WRITE( SAVAGE_MC_AGP_LOCATION, 0xffffffc0 ); /* ?? */
		SAVAGE_WRITE( SAVAGE_AGP_COMMAND, 0 ); /* clear AGP_COMMAND */
	} else {
#endif /* __REALLY_HAVE_SG */
/* FIXME: Turn off PCI GART
		tmp = SAVAGE_READ( SAVAGE_AIC_CNTL )
		      & ~SAVAGE_PCIGART_TRANSLATE_EN;
		SAVAGE_WRITE( SAVAGE_AIC_CNTL, tmp );
*/
#if __REALLY_HAVE_SG
	}
#endif /* __REALLY_HAVE_SG */

	dev_priv->last_buf = 0;

	dev->dev_private = (void *)dev_priv;

/* FIXME:	savage_do_engine_reset( dev ); */

	return 0;
}

int savage_do_cleanup_bci( drm_device_t *dev )
{
	DRM_INFO( "cleaning up bci ...\n" );

	if ( dev->dev_private ) {
		drm_savage_private_t *dev_priv = dev->dev_private;

		if ( !dev_priv->is_pci ) {
			/* FIXME: DRM_IOREMAPFREE( dev_priv->bci ); */
			/* FIXME: DRM_IOREMAPFREE( dev_priv->buffers ); */
		} else {
#if __REALLY_HAVE_SG
			if (!DRM(ati_pcigart_cleanup)( dev,
						dev_priv->phys_pci_gart,
						dev_priv->bus_pci_gart ))
				DRM_ERROR( "failed to cleanup PCI GART!\n" );
#endif /* __REALLY_HAVE_SG */
		}

		DRM(free)( dev->dev_private, sizeof(drm_savage_private_t),
			   DRM_MEM_DRIVER );
		dev->dev_private = NULL;
	}

	return 0;
}

int savage_bci_init( DRM_IOCTL_ARGS )
{
	DRM_DEVICE;
	drm_savage_init_t init;

	DRM_COPY_FROM_USER_IOCTL( init, (drm_savage_init_t *)data, sizeof(init) );

	switch ( init.func ) {
	case SAVAGE_INIT_BCI:
		return savage_do_init_bci( dev, &init );
	case SAVAGE_CLEANUP_BCI:
		return savage_do_cleanup_bci( dev );
	}

	return DRM_ERR(EINVAL);
}

drm_buf_t *savage_freelist_get( drm_device_t *dev )
{
	drm_device_dma_t *dma = dev->dma;
	drm_savage_private_t *dev_priv = dev->dev_private;
	drm_savage_buf_priv_t *buf_priv;
	drm_buf_t *buf;
	int i, t;
	int start;

	if ( ++dev_priv->last_buf >= dma->buf_count )
		dev_priv->last_buf = 0;

	start = dev_priv->last_buf;

	for ( t = 0 ; t < dev_priv->usec_timeout ; t++ ) {
		u32 done_age = 0; /* FIXME: GET_SCRATCH( 1 ); */
		DRM_DEBUG("done_age = %d\n",done_age);
		for ( i = start ; i < dma->buf_count ; i++ ) {
			buf = dma->buflist[i];
			buf_priv = buf->dev_private;
			if ( buf->pid == 0 || (buf->pending && 
					       buf_priv->age <= done_age) ) {
				dev_priv->stats.requested_bufs++;
				buf->pending = 0;
				return buf;
			}
			start = 0;
		}

		if (t) {
			DRM_UDELAY( 1 );
			dev_priv->stats.freelist_loops++;
		}
	}

	DRM_ERROR( "returning NULL!\n" );
	return NULL;
}

static int savage_bci_get_buffers( drm_device_t *dev, drm_dma_t *d )
{
	int i;
	drm_buf_t *buf;

	for ( i = d->granted_count ; i < d->request_count ; i++ ) {
		buf = savage_freelist_get( dev );
		if ( !buf ) return DRM_ERR(EBUSY); /* NOTE: broken client */

		buf->pid = DRM_CURRENTPID;

		if ( DRM_COPY_TO_USER( &d->request_indices[i], &buf->idx,
				   sizeof(buf->idx) ) )
			return DRM_ERR(EFAULT);
		if ( DRM_COPY_TO_USER( &d->request_sizes[i], &buf->total,
				   sizeof(buf->total) ) )
			return DRM_ERR(EFAULT);

		d->granted_count++;
	}
	return 0;
}

int savage_bci_buffers( DRM_IOCTL_ARGS )
{
	DRM_DEVICE;
	drm_device_dma_t *dma = dev->dma;
	int ret = 0;
	drm_dma_t d;

/* FIXME:	LOCK_TEST_WITH_RETURN( dev ); */

	DRM_COPY_FROM_USER_IOCTL( d, (drm_dma_t *)data, sizeof(d) );

	/* Please don't send us buffers.
	 */
	if ( d.send_count != 0 ) {
		DRM_ERROR( "Process %d trying to send %d buffers via drmDMA\n",
			   DRM_CURRENTPID, d.send_count );
		return DRM_ERR(EINVAL);
	}

	/* We'll send you buffers.
	 */
	if ( d.request_count < 0 || d.request_count > dma->buf_count ) {
		DRM_ERROR( "Process %d trying to get %d buffers (of %d max)\n",
			   DRM_CURRENTPID, d.request_count, dma->buf_count );
		return DRM_ERR(EINVAL);
	}

	d.granted_count = 0;

	if ( d.request_count ) {
		ret = savage_bci_get_buffers( dev, &d );
	}

	DRM_COPY_TO_USER_IOCTL( (drm_dma_t *)data, d, sizeof(d) );

	return ret;
}
