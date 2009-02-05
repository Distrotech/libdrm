/*
 * Copyright (c) 2007-2008 Tungsten Graphics, Inc., Cedar Park, TX., USA,
 * All Rights Reserved.
 * Copyright (c) 2009 VMware, Inc., Palo Alto, CA., USA,
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "drmP.h"
#include "ochr_drm.h"
#include "via_drv.h"
#include "drm_pciids.h"

int drm_via_disable_verifier;
MODULE_PARM_DESC(disable_verifier, "Disable GPU command security check. "
		 "DANGEROUS!!");
module_param_named(disable_verifier, drm_via_disable_verifier, int, 0600);

static int dri_library_name(struct drm_device *dev, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "unichrome\n");
}

static struct pci_device_id pciidlist[] = {
	viadrv_PCI_IDS
};

#define DRM_IOCTL_VIA_DEC_FUTEX    \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_VIA_DEC_FUTEX,\
		 struct drm_via_futex)

#define DRM_IOCTL_VIA_TTM_EXECBUF \
	DRM_IOW(DRM_COMMAND_BASE + DRM_VIA_TTM_EXECBUF,\
		struct drm_via_ttm_execbuf_arg)

#define DRM_VIA_TTM_PL_CREATE      (TTM_PL_CREATE + DRM_VIA_PLACEMENT_OFFSET)
#define DRM_VIA_TTM_PL_REFERENCE   (TTM_PL_REFERENCE + DRM_VIA_PLACEMENT_OFFSET)
#define DRM_VIA_TTM_PL_UNREF       (TTM_PL_UNREF + DRM_VIA_PLACEMENT_OFFSET)
#define DRM_VIA_TTM_PL_SYNCCPU     (TTM_PL_SYNCCPU + DRM_VIA_PLACEMENT_OFFSET)
#define DRM_VIA_TTM_PL_WAITIDLE    (TTM_PL_WAITIDLE + DRM_VIA_PLACEMENT_OFFSET)
#define DRM_VIA_TTM_PL_SETSTATUS   (TTM_PL_SETSTATUS + DRM_VIA_PLACEMENT_OFFSET)

#define DRM_VIA_TTM_FENCE_SIGNALED (TTM_FENCE_SIGNALED + DRM_VIA_FENCE_OFFSET)
#define DRM_VIA_TTM_FENCE_FINISH   (TTM_FENCE_FINISH + DRM_VIA_FENCE_OFFSET)
#define DRM_VIA_TTM_FENCE_UNREF    (TTM_FENCE_UNREF + DRM_VIA_FENCE_OFFSET)

#define DRM_IOCTL_VIA_TTM_PL_CREATE    \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_VIA_TTM_PL_CREATE,\
		 union ttm_pl_create_arg)
#define DRM_IOCTL_VIA_TTM_PL_REFERENCE \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_VIA_TTM_PL_REFERENCE,\
		 union ttm_pl_reference_arg)
#define DRM_IOCTL_VIA_TTM_PL_UNREF    \
	DRM_IOW(DRM_COMMAND_BASE + DRM_VIA_TTM_PL_UNREF,\
		struct ttm_pl_reference_req)
#define DRM_IOCTL_VIA_TTM_PL_SYNCCPU    \
	DRM_IOW(DRM_COMMAND_BASE + DRM_VIA_TTM_PL_SYNCCPU,\
		struct ttm_synccpu_arg)
#define DRM_IOCTL_VIA_TTM_PL_WAITIDLE    \
	DRM_IOW(DRM_COMMAND_BASE + DRM_VIA_TTM_PL_WAITIDLE,\
		struct ttm_waitidle_arg)
#define DRM_IOCTL_VIA_TTM_PL_SETSTATUS \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_VIA_TTM_PL_CREATE,\
		 union ttm_pl_setstatus_arg)
#define DRM_IOCTL_VIA_TTM_FENCE_SIGNALED \
	DRM_IOWR (DRM_COMMAND_BASE + DRM_VIA_TTM_FENCE_SIGNALED,	\
		  union ttm_fence_signaled_arg)
#define DRM_IOCTL_VIA_TTM_FENCE_FINISH \
	DRM_IOWR (DRM_COMMAND_BASE + DRM_VIA_TTM_FENCE_FINISH,	\
		 union ttm_fence_finish_arg)
#define DRM_IOCTL_VIA_TTM_FENCE_UNREF \
	DRM_IOW (DRM_COMMAND_BASE + DRM_VIA_TTM_FENCE_UNREF,	\
		 struct ttm_fence_unref_arg)

static struct drm_ioctl_desc via_ioctls[] = {
	DRM_IOCTL_DEF(DRM_VIA_VT, via_vt_ioctl, DRM_AUTH | DRM_MASTER),
	DRM_IOCTL_DEF(DRM_VIA_GET_PARAM, via_getparam_ioctl, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_VIA_EXTENSION, via_extension_ioctl, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_VIA_DEC_FUTEX, via_decoder_futex, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_VIA_TTM_EXECBUF, via_execbuffer, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_VIA_TTM_PL_CREATE, via_pl_create_ioctl,
		      DRM_AUTH),
	DRM_IOCTL_DEF(DRM_VIA_TTM_PL_REFERENCE, via_pl_reference_ioctl,
		      DRM_AUTH),
	DRM_IOCTL_DEF(DRM_VIA_TTM_PL_UNREF, via_pl_unref_ioctl,
		      DRM_AUTH),
	DRM_IOCTL_DEF(DRM_VIA_TTM_PL_SYNCCPU, via_pl_synccpu_ioctl,
		      DRM_AUTH),
	DRM_IOCTL_DEF(DRM_VIA_TTM_PL_WAITIDLE, via_pl_waitidle_ioctl,
		      DRM_AUTH),
	DRM_IOCTL_DEF(DRM_VIA_TTM_PL_SETSTATUS, via_pl_setstatus_ioctl,
		      DRM_AUTH),
	DRM_IOCTL_DEF(DRM_VIA_TTM_FENCE_SIGNALED,
		      via_fence_signaled_ioctl, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_VIA_TTM_FENCE_FINISH, via_fence_finish_ioctl,
		      DRM_AUTH),
	DRM_IOCTL_DEF(DRM_VIA_TTM_FENCE_UNREF, via_fence_unref_ioctl,
		      DRM_AUTH)
};

static long via_unlocked_ioctl(struct file *filp, unsigned int cmd,
			      unsigned long arg)
{
	struct drm_file *file_priv = filp->private_data;
	struct drm_device *dev = file_priv->minor->dev;
	unsigned int nr = DRM_IOCTL_NR(cmd);
	long ret;

	/*
	 * The driver private ioctls and TTM ioctls should be
	 * thread-safe.
	 */

	if ((nr >= DRM_COMMAND_BASE) && (nr < DRM_COMMAND_END)
	    && (nr < DRM_COMMAND_BASE + dev->driver->num_ioctls))
		return drm_unlocked_ioctl(filp, cmd, arg);

	/*
	 * Not all old drm ioctls are thread-safe.
	 */

	lock_kernel();
	ret = drm_unlocked_ioctl(filp, cmd, arg);
	unlock_kernel();
	return ret;
}

static int probe(struct pci_dev *pdev, const struct pci_device_id *ent);
static struct drm_driver driver = {
	.driver_features =
	    DRIVER_USE_AGP | DRIVER_USE_MTRR | DRIVER_HAVE_IRQ |
	    DRIVER_IRQ_SHARED,
	.load = via_driver_load,
	.unload = via_driver_unload,
	.context_ctor = via_context_ctor,
	.context_dtor = via_context_dtor,
	.get_vblank_counter = via_get_vblank_counter,
	.enable_vblank = via_enable_vblank,
	.disable_vblank = via_disable_vblank,
	.irq_preinstall = via_driver_irq_preinstall,
	.irq_postinstall = via_driver_irq_postinstall,
	.irq_uninstall = via_driver_irq_uninstall,
	.irq_handler = via_driver_irq_handler,
	.dma_quiescent = NULL,
	.dri_library_name = dri_library_name,
	.reclaim_buffers = drm_core_reclaim_buffers,
	.reclaim_buffers_locked = NULL,
	.firstopen = via_firstopen,
	.reclaim_buffers_idlelocked = NULL,
	.lastclose = via_lastclose,
	.get_map_ofs = drm_core_get_map_ofs,
	.get_reg_ofs = drm_core_get_reg_ofs,
	.ioctls = via_ioctls,
	.num_ioctls = DRM_ARRAY_SIZE(via_ioctls),
	.fops = {
		 .owner = THIS_MODULE,
		 .open = via_open,
		 .release = via_release,
		 .unlocked_ioctl = via_unlocked_ioctl,
		 .mmap = via_mmap,
		 .poll = drm_poll,
		 .fasync = drm_fasync,
		 .read = via_ttm_read,
		 .write = via_ttm_write},
	.pci_driver = {
		       .name = DRIVER_NAME,
		       .id_table = pciidlist,
		       .probe = probe,
		       .remove = __devexit_p(drm_cleanup_pci),
		       .resume = via_resume,
		       .suspend = via_suspend,
		       },
	.fence_driver = NULL,
	.bo_driver = NULL,
	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRM_VIA_DRIVER_DATE,
	.major = DRM_VIA_DRIVER_MAJOR,
	.minor = DRM_VIA_DRIVER_MINOR,
	.patchlevel = DRM_VIA_DRIVER_PATCHLEVEL
};

static int probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	return drm_get_dev(pdev, ent, &driver);
}

static int __init via_init(void)
{
	via_init_command_verifier();
	return drm_init(&driver, pciidlist);
}

static void __exit via_exit(void)
{
	drm_exit(&driver);
}

module_init(via_init);
module_exit(via_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL and additional rights");
