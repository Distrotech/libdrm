/* r128_drv.c -- ATI Rage 128 driver -*- linux-c -*-
 * Created: Mon Dec 13 09:47:27 1999 by faith@precisioninsight.com
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
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Rickard E. (Rik) Faith <faith@valinux.com>
 *    Gareth Hughes <gareth@valinux.com>
 */


#include <sys/types.h>

#include "mach64.h"
#include "drmP.h"
#include "drm.h"
#include "mach64_drm.h"
#include "mach64_drv.h"

/* List acquired from http://www.yourvote.com/pci/pcihdr.h and xc/xc/programs/Xserver/hw/xfree86/common/xf86PciInfo.h
 * Please report to eta@lclark.edu inaccuracies or if a chip you have works that is marked unsupported here.
 */
drm_chipinfo_t DRM(devicelist)[] = {
	{0x1002, 0x4742, 1, "ATI 3D Rage Pro AGP 1x/2x"},
	{0x1002, 0x4744, 1, "ATI 3D Rage Pro AGP 1x"},
	{0x1002, 0x4747, 1, "ATI 3D Rage Pro"},
	{0x1002, 0x4749, 1, "ATI 3D Rage Pro"},
	{0x1002, 0x474c, 1, "ATI Rage XC"},
	{0x1002, 0x474d, 1, "ATI Rage XL AGP 2x"},
	{0x1002, 0x474e, 1, "ATI Rage XC AGP"},
	{0x1002, 0x474f, 1, "ATI Rage XL"},
	{0x1002, 0x4752, 1, "ATI Rage XL"},
	{0x1002, 0x4753, 1, "ATI Rage XC"},
	{0x1002, 0x4c42, 1, "ATI 3D Rage LT Pro AGP-133"},
	{0x1002, 0x4c44, 1, "ATI 3D Rage LT Pro AGP-66"},
	{0x1002, 0x4c49, 1, "ATI 3D Rage LT Pro AGP-66"},
	{0x1002, 0x4c4d, 1, "ATI Rage Mobility P/M AGP 2x"},
	{0x1002, 0x4c4e, 1, "ATI Rage Mobility L"},
	{0x1002, 0x4c50, 1, "ATI 3D Rage LT Pro"},
	{0x1002, 0x4c51, 1, "ATI 3D Rage LT Pro"},
	{0, 0, 0, NULL}
};

#include "drm_agpsupport.h"
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
#include "drm_pci.h"
#include "drm_sysctl.h"
#include "drm_vm.h"

DRIVER_MODULE(mach64, pci, mach64_driver, mach64_devclass, 0, 0);
