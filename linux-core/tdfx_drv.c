/* tdfx_drv.c -- tdfx driver -*- linux-c -*-
 * Created: Thu Oct  7 10:38:32 1999 by faith@precisioninsight.com
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
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
 *    Rickard E. (Rik) Faith <faith@valinux.com>
 *    Daryll Strauss <daryll@valinux.com>
 *    Gareth Hughes <gareth@valinux.com>
 */

#ifdef __linux__
#include <linux/config.h>
#endif

#ifdef __FreeBSD__
#include <sys/types.h>
#include <sys/bus.h>
#include <pci/pcivar.h>
#include <opt_drm_linux.h>
#endif

#include "tdfx.h"
#include "drmP.h"

#define DRIVER_AUTHOR		"VA Linux Systems Inc."

#define DRIVER_NAME		"tdfx"
#define DRIVER_DESC		"3dfx Banshee/Voodoo3+"
#define DRIVER_DATE		"20010216"

#define DRIVER_MAJOR		1
#define DRIVER_MINOR		0
#define DRIVER_PATCHLEVEL	0


#ifdef __FreeBSD__
static int tdfx_probe(device_t dev)
{
	const char *s = 0;

	switch (pci_get_devid(dev)) {
	case 0x0003121a:
		s = "3Dfx Voodoo Banshee graphics accelerator";
		break;
		
	case 0x0005121a:
		s = "3Dfx Voodoo 3 graphics accelerator";
		break;

	case 0x0009121a:
		s = "3Dfx Voodoo 4/5 graphics accelerator";
		break;
	}
	
	if (s) {
		device_set_desc(dev, s);
		return 0;
	}
	
	return ENXIO;
}
#endif

#include "drm_auth.h"
#include "drm_bufs.h"
#include "drm_context.h"
#include "drm_dma.h"
#include "drm_drawable.h"
#include "drm_drv.h"
#include "drm_fops.h"
#include "drm_init.h"
#include "drm_ioctl.h"
#include "drm_lock.h"
#include "drm_memory.h"
#include "drm_proc.h"
#include "drm_vm.h"
#include "drm_stub.h"

#ifdef __FreeBSD__
DRIVER_MODULE(tdfx, pci, tdfx_driver, tdfx_devclass, 0, 0);
#endif
