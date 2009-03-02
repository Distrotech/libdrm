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

#ifndef _VIA_DRV_H_
#define _VIA_DRV_H_

#include "via_verifier.h"
#include "ochr_drm.h"
#include "via_dmablit.h"
#include "ttm/ttm_object.h"
#include "ttm/ttm_fence_driver.h"
#include "ttm/ttm_bo_driver.h"
#include "ttm/ttm_lock.h"
#include "ttm/ttm_memory.h"

#define DRIVER_AUTHOR	        "Tungsten Graphics"
#define DRIVER_NAME		"openchrome"
#define DRIVER_DESC		"VIA Unichrome / Pro / II"

#define VIA_VQ_SIZE             (512*1024)
#define VIA_AGPC_SIZE           (2*1024*1024)
#define VIA_AGPC_MASK           (VIA_AGPC_SIZE -1)
#define VIA_AGPBO_SIZE          (16*1024*1024)

#define DRM_FILE_PAGE_OFFSET (0x100000000ULL >> PAGE_SHIFT)

/*
 * Registers go here.
 */
#define CMDBUF_ALIGNMENT_SIZE   (0x100)
#define CMDBUF_ALIGNMENT_MASK   (0x0ff)

/* defines for VIA 2D registers */
#define VIA_REG_GEMODE          0x004
#define VIA_GEM_8bpp            0x00000000
#define VIA_GEM_16bpp           0x00000100
#define VIA_GEM_32bpp           0x00000300


#define VIA_REG_SRCBASE         0x030
#define VIA_REG_DSTBASE         0x034
#define VIA_REG_PITCH           0x038
#define VIA_PITCH_ENABLE        0x80000000

#define VIA_REG_SRCCOLORKEY     0x01C
#define VIA_REG_KEYCONTROL      0x02C
#define VIA_REG_SRCPOS          0x008
#define VIA_REG_DSTPOS          0x00C

#define VIA_REG_GECMD           0x000
#define VIA_GEC_BLT             0x00000001
#define VIA_GEC_INCX            0x00000000
#define VIA_GEC_DECY            0x00004000
#define VIA_GEC_INCY            0x00000000
#define VIA_GEC_DECX            0x00008000
#define VIA_GEC_FIXCOLOR_PAT    0x00002000

#define VIA_REG_DIMENSION       0x010	/* width and height */
#define VIA_REG_FGCOLOR         0x018

#define VIA_ROP_CLEAR 0x00
#define VIA_ROP_SRC 0xCC
#define VIA_ROP_PAT 0xF0
#define VIA_ROP_SET 0xFF




/* defines for VIA 3D registers */
#define VIA_REG_STATUS	        0x400
#define VIA_REG_TRANSET	        0x43C
#define VIA_REG_TRANSPACE       0x440

/* VIA_REG_STATUS(0x400): Engine Status */
#define VIA_CMD_RGTR_BUSY       0x00000080	/* Command Regulator is busy */
#define VIA_2D_ENG_BUSY	        0x00000002	/* 2D Engine is busy */
#define VIA_3D_ENG_BUSY	        0x00000001	/* 3D Engine is busy */
#define VIA_VR_QUEUE_BUSY       0x00020000	/* Virtual Queue is busy */
#define VIA_PCI_BUF_SIZE 60000
#define VIA_FIRE_BUF_SIZE  1024

#define VIA_FENCE_OFFSET_CMD 0x000

#define VIA_IDLE_TIMEOUT (3*HZ)

/*
 * Extension offsets.
 */

#define DRM_VIA_DEC_FUTEX        0x03
#define DRM_VIA_TTM_EXECBUF      0x04
#define DRM_VIA_PLACEMENT_OFFSET 0x10
#define DRM_VIA_FENCE_OFFSET     0x18

enum via_barriers {
	VIA_BARRIER_HQV0 = 0,
	VIA_BARRIER_HQV1,
	VIA_BARRIER_MPEG0,
	VIA_BARRIER_MPEG1,
	VIA_NUM_BARRIERS
};

struct via_fpriv {
	struct ttm_object_file *tfile;
};

/*
 * Context private stuff. Mainly buffers used
 * for execbuf that we don't want to allocate
 * for each call, and that may be used by
 * a number of contexts at a time.
 */

struct via_sarea {
    struct drm_via_sarea sa;
    struct drm_via_sarea_xvmc sa_xvmc;
};

struct drm_via_private {
	struct drm_global_reference mem_global_ref;
	struct drm_device *dev;
	struct ttm_object_device *tdev;
        struct ttm_fence_device fdev;
        struct ttm_bo_device bdev;
	struct ttm_lock ttm_lock;
	struct via_sarea *sarea_priv;
	drm_local_map_t *sarea;
	unsigned long agpAddr;
	wait_queue_head_t decoder_queue[DRM_VIA_NR_XVMC_LOCKS];
	char pci_buf[VIA_PCI_BUF_SIZE];
	char *dma_ptr;
	unsigned int dma_low;
	unsigned int dma_high;
	unsigned int dma_offset;
	uint32_t dma_wrap;
	uint32_t dma_tracker;
	uint32_t dma_free;
	struct list_head dma_trackers;
	uint32_t __iomem *last_pause_ptr;
	uint32_t __iomem *hw_addr_ptr;
	atomic_t vbl_received;
	drm_via_state_t hc_state;
	const uint32_t *fire_offsets[VIA_FIRE_BUF_SIZE];
	uint32_t num_fire_offsets;
	int chipset;
	uint32_t irq_enable_mask;
	uint32_t irq_pending_mask;
	uint32_t irq2_enable_mask;
	uint32_t irq2_pending_mask;
	spinlock_t irq_lock;

	/* Memory manager stuff */

	unsigned long vram_offset;
	unsigned long agp_offset;
	struct drm_via_blitq blit_queues[VIA_NUM_BLIT_ENGINES];
	uint32_t dma_diff;
	atomic_t fence_seq[VIA_NUM_ENGINES];
	atomic_t emitted_cmd_seq;
	uint64_t vram_size;	/* kiB */
	uint64_t vram_start;
	int vram_direct;
	int vram_mtrr;

	uint64_t tt_size;	/* bytes */
	uint64_t tt_start;

	struct ttm_buffer_object *vq_bo;

	struct ttm_buffer_object *fence_bo;
	struct ttm_bo_kmap_obj fence_bmo;
	uint32_t *fence_map;

	struct ttm_buffer_object *agpc_bo;
	struct ttm_bo_kmap_obj agpc_bmo;
	volatile uint32_t *agpc_map;

	/*
	 * Fixed memory region for transient buffer objects.
	 */

	struct ttm_buffer_object *agp_bo;
	struct mutex init_mutex;
	u8 __iomem *mmio_map;
	atomic_t val_seq;
	struct mutex cmdbuf_mutex;
        int has_irq;

	struct hrtimer fence_timer;
	struct ttm_fence_object *barriers[VIA_NUM_BARRIERS];
	uint32_t max_validate_buffers;

	rwlock_t context_lock;
	struct drm_open_hash context_hash;
};

struct via_cpriv {
	struct drm_hash_item hash;
        struct drm_via_private *dev_priv;
        struct kref kref;
        atomic_t in_execbuf;
	void *reloc_buf;
	struct via_validate_buffer *val_bufs;
};

enum via_family {
	VIA_OTHER = 0,		/* Baseline */
	VIA_PRO_GROUP_A,	/* Another video engine and DMA commands */
	VIA_DX9_0		/* Same video as pro_group_a, but 3D is unsupported */
};

/* VIA MMIO register access */

#define VIA_READ(reg)		ioread32((u32 *)(dev_priv->mmio_map + (reg)))
#define VIA_WRITE(reg,val)	iowrite32(val, (u32 *)(dev_priv->mmio_map + (reg)))
#define VIA_READ8(reg)		ioread8(dev_priv->mmio_map + (reg))
#define VIA_WRITE8(reg,val)	iowrite8(val, dev_priv->mmio_map + (reg))

extern int drm_via_disable_verifier;
extern struct drm_fence_driver via_fence_driver;

static inline struct drm_via_private *via_priv(struct drm_device *dev)
{
	return (struct drm_via_private *)dev->dev_private;
}

extern int via_decoder_futex(struct drm_device *dev, void *data,
			     struct drm_file *file_priv);
extern int via_driver_load(struct drm_device *dev, unsigned long chipset);
extern int via_driver_unload(struct drm_device *dev);
extern int via_suspend(struct pci_dev *pdev, pm_message_t state);
extern int via_resume(struct pci_dev *pdev);

extern u32 via_get_vblank_counter(struct drm_device *dev, int crtc);
extern int via_enable_vblank(struct drm_device *dev, int crtc);
extern void via_disable_vblank(struct drm_device *dev, int crtc);

extern irqreturn_t via_driver_irq_handler(DRM_IRQ_ARGS);
extern void via_driver_irq_preinstall(struct drm_device *dev);
extern int via_driver_irq_postinstall(struct drm_device *dev);
extern void via_driver_irq_uninstall(struct drm_device *dev);

extern void via_emit_fence_seq_standalone(struct drm_via_private *dev_priv,
				   uint32_t offset, uint32_t value);
extern int via_dma_cleanup(struct drm_device *dev);
extern void via_init_command_verifier(void);
extern int via_driver_dma_quiescent(struct drm_device *dev);
extern void via_init_futex(struct drm_via_private *dev_priv);
extern void via_release_futex(struct drm_via_private *dev_priv, int context);
extern int via_firstopen(struct drm_device *dev);
extern void via_lastclose(struct drm_device *dev);

extern int via_dmablit_bo(struct ttm_buffer_object *bo,
			  struct ttm_mem_reg *new_mem,
			  struct page **pages,
			  int *fence_class);

extern struct ttm_backend *via_create_ttm_backend_entry(struct ttm_bo_device
							*bdev);
extern int via_invalidate_caches(struct ttm_bo_device *bdev,
				 uint32_t buffer_flags);
extern int via_init_mem_type(struct ttm_bo_device *dev, uint32_t type,
			     struct ttm_mem_type_manager *man);
extern uint32_t via_evict_flags(struct ttm_buffer_object *bo);
extern int via_bo_move(struct ttm_buffer_object *bo, bool evict,
		       bool interruptible, bool no_wait, struct ttm_mem_reg *new_mem);
extern void via_dma_initialize(struct drm_via_private *dev_priv);
extern void via_dma_takedown(struct drm_via_private *dev_priv);
extern void via_wait_idle(struct drm_via_private *dev_priv);
int via_copy_cmdbuf(struct drm_via_private *dev_priv,
		    uint64_t cmd_buffer,
		    uint32_t size,
		    uint32_t mechanism,
		    uint32_t ** cmdbuf_addr, int *is_iomem);

extern int via_vt_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv);
extern int via_execbuffer(struct drm_device *dev, void *data,
			  struct drm_file *file_priv);
extern int via_dispatch_commands(struct drm_device *dev,
				 unsigned long size, uint32_t mechanism,
				 bool emit_seq);
extern void via_ttm_signal_fences(struct drm_via_private *dev_priv);
extern void via_ttm_fence_cmd_handler(struct drm_via_private *dev_priv, uint32_t signal_types);
extern void via_ttm_fence_dmablit_handler(struct drm_via_private *dev_priv, int engine);
extern enum hrtimer_restart via_ttm_fence_timer_func(struct hrtimer *timer);
extern int via_ttm_fence_device_init(struct drm_via_private *dev_priv);

extern int via_getparam_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *file_priv);
extern int via_extension_ioctl(struct drm_device *dev, void *data,
			       struct drm_file *file_priv);

extern int via_release(struct inode *inode, struct file *filp);
extern int via_open(struct inode *inode, struct file *filp);

extern int via_context_ctor(struct drm_device *dev, int context);
extern int via_context_dtor(struct drm_device *dev, int context);

struct via_cpriv *via_context_lookup(struct drm_via_private *dev_priv,
				     int context);
extern void via_context_unref(struct via_cpriv **cpriv);

static inline struct via_fpriv *via_fpriv(struct drm_file *file_priv)
{
	return (struct via_fpriv *) file_priv->driver_priv;
}

extern int via_fence_signaled_ioctl(struct drm_device *dev, void *data,
				    struct drm_file *file_priv);
extern int via_fence_finish_ioctl(struct drm_device *dev, void *data,
				  struct drm_file *file_priv);
extern int via_fence_unref_ioctl(struct drm_device *dev, void *data,
				 struct drm_file *file_priv);
extern int via_pl_waitidle_ioctl(struct drm_device *dev, void *data,
				 struct drm_file *file_priv);
extern int via_pl_setstatus_ioctl(struct drm_device *dev, void *data,
				  struct drm_file *file_priv);
extern int via_pl_synccpu_ioctl(struct drm_device *dev, void *data,
				struct drm_file *file_priv);
extern int via_pl_unref_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *file_priv);
extern int via_pl_reference_ioctl(struct drm_device *dev, void *data,
				  struct drm_file *file_priv);
extern int via_pl_create_ioctl(struct drm_device *dev, void *data,
			       struct drm_file *file_priv);
extern int via_mmap(struct file *filp, struct vm_area_struct *vma);
extern int via_verify_access(struct ttm_buffer_object *bo,
			     struct file *filp);
extern ssize_t via_ttm_read(struct file *filp, char __user *buf,
			    size_t count, loff_t *f_pos);
extern ssize_t via_ttm_write(struct file *filp, const char __user *buf,
			     size_t count, loff_t *f_pos);
extern int via_ttm_global_init(struct drm_via_private *dev_priv);
extern void via_ttm_global_release(struct drm_via_private *dev_priv);
#endif
