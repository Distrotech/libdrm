/*
 * Author: Max Lingua <sunmax@libero.it>
 */

#ifndef _S3V_H
#define _S3V_H

/* This remains constant for all DRM template files.
 */
#define DRM(x) s3v_##x

/* #define __FAST 0 */
/* General customization:
 */
#define __HAVE_MTRR		1
/* #define __HAVE_SG       	1 */
#define __HAVE_AGP          	1
#define __MUST_HAVE_AGP         0
/* #define __HAVE_CTX_BITMAP	1 */

/* DMA customization:
 */
#define	__HAVE_DMA			1
#define __HAVE_PCI_DMA      		1
#define __HAVE_OLD_DMA			1
/* "GH: This is a big hack for now..."    *
 * Gareth? Is it you? What does that mean */
#define __HAVE_DMA_FLUSH        	0
#define __HAVE_DMA_SCHEDULE 		0
#define __HAVE_DMA_WAITQUEUE    	0
#define __HAVE_MULTIPLE_DMA_QUEUES  	0
/* #define __HAVE_DMA_QUEUE		1 */ /* check */
#define __HAVE_DMA_WAITLIST     	1
#define __HAVE_DMA_FREELIST		1
#define __HAVE_DMA_IRQ          	0

#define __HAVE_DMA_READY        	0
#define DRIVER_DMA_READY() do {     \
 /*   s3v_dma_ready(dev); */        \
} while (0)

/* Driver customization:
 */

#define DRIVER_PREINSTALL() do {					\
	drm_s3v_private_t *dev_priv =					\
		(drm_s3v_private_t *)dev->dev_private;		\
	/* write something in regs where needed */		\
} while (0)

#define DRIVER_POSTINSTALL() do {					\
	drm_s3v_private_t *dev_priv =					\
		(drm_s3v_private_t *)dev->dev_private;		\
	/* write something in regs where needed */      \
} while (0)

#define DRIVER_UNINSTALL() do {						\
	drm_s3v_private_t *dev_priv =					\
		(drm_s3v_private_t *)dev->dev_private;		\
	/* write something in regs where needed */      \
} while (0)

/* Buffer customization:
 */

#define DRIVER_BUF_PRIV_T	drm_s3v_buf_priv_t

/* needed from DRM(mapbufs) even if you don't habe agp ; ( */
#define DRIVER_AGP_BUFFERS_MAP( dev )                   \
    ((drm_s3v_private_t *)((dev)->dev_private))->buffer_map
										/* ... ->buffers */
#endif
