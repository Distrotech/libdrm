/*
 * Author: Max Lingua <sunmax@libero.it>
 */

#include <linux/config.h>
#include "s3v.h"
#include "drmP.h"
#include "s3v_drv.h"

#define DRIVER_AUTHOR		"Max Lingua (ladybug)"

#define DRIVER_NAME		"s3v"
#define DRIVER_DESC		"S3 Virge 3D"
#define DRIVER_DATE		"20020207"

#define DRIVER_MAJOR		2
#define DRIVER_MINOR		0
#define DRIVER_PATCHLEVEL	0

#define DRIVER_IOCTLS \
  [DRM_IOCTL_NR(DRM_IOCTL_DMA)]     	      = { s3v_dma,           1, 0 }, \
  [DRM_IOCTL_NR(DRM_IOCTL_S3V_INIT)]          = { s3v_dma_init,      1, 1 }, \
  [DRM_IOCTL_NR(DRM_IOCTL_S3V_RESET)]         = { s3v_reset,         1, 0 }, \
  [DRM_IOCTL_NR(DRM_IOCTL_S3V_SIMPLE_LOCK)]   = { s3v_simple_lock,   1, 0 }, \
  [DRM_IOCTL_NR(DRM_IOCTL_S3V_SIMPLE_FLUSH_LOCK)] = \
                                          { s3v_simple_flush_lock,   1, 0 }, \
  [DRM_IOCTL_NR(DRM_IOCTL_S3V_SIMPLE_UNLOCK)] = { s3v_simple_unlock, 1, 0 }, \
  [DRM_IOCTL_NR(DRM_IOCTL_S3V_STATUS)]        = { s3v_status,        1, 0 }

#define IOCTL_TABLE_NAME    DRM(ioctls)
#define IOCTL_FUNC_NAME     DRM(ioctl)

#define __HAVE_COUNTERS     3
#define __HAVE_COUNTER6     _DRM_STAT_DMA
#define __HAVE_COUNTER7     _DRM_STAT_PRIMARY
#define __HAVE_COUNTER8     _DRM_STAT_SECONDARY


#include "drm_auth.h"
#include "drm_bufs.h"
#include "drm_context.h"
#include "drm_dma.h"
#include "drm_drawable.h"
#include "drm_drv.h"

#ifndef MODULE
/* DRM(options) is called by the kernel to parse command-line options
 * passed via the boot-loader (e.g., LILO).  It calls the insmod option
 * routine, drm_parse_drm.
 */

/* JH- We have to hand expand the string ourselves because of the cpp.  If
 * anyone can think of a way that we can fit into the __setup macro without
 * changing it, then please send the solution my way.
 */
static int __init s3v_options( char *str )
{
	DRM(parse_options)( str );
	return 1;
}

__setup( DRIVER_NAME "=", s3v_options );
#endif

#include "drm_fops.h"
#include "drm_init.h"
#include "drm_ioctl.h"
#include "drm_lock.h"
#include "drm_lists.h"
#include "drm_memory.h"
#include "drm_proc.h"
#include "drm_vm.h"
#include "drm_stub.h"
#include "drm_scatter.h"
