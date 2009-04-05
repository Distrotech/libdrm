/* via_dma.c -- DMA support for the VIA Unichrome/Pro
 *
 * Copyright 2003-2008 Tungsten Graphics, Inc., Cedar Park, Texas.
 * All Rights Reserved.
 * Copyright 2004 Digeo, Inc., Palo Alto, CA, U.S.A.
 * All Rights Reserved.
 * Copyright 2004 The Unichrome project.
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
 *
 * Authors:
 *    Tungsten Graphics,
 *    Erdi Chen,
 *    Thomas Hellstrom.
 */

#include "drmP.h"
#include "drm.h"
#include "ochr_drm.h"
#include "via_drv.h"
#include "via_3d_reg.h"

#define VIA_FENCE_EXTRA (7*8)
#define VIA_PAD_SUBMISSION_SIZE (0x102)

#define VIA_OUT_RING_H1(nReg, nData)				\
	{							\
		iowrite32(((nReg) >> 2) | HALCYON_HEADER1, vb);	\
		iowrite32(nData, vb + 1);			\
		vb += 2;					\
		dev_priv->dma_low += 8;				\
	}

#define via_flush_write_combine() DRM_MEMORYBARRIER()

#define VIA_OUT_RING_QW(w1,w2)			\
	{					\
		iowrite32(w1, vb);		\
		iowrite32(w2, vb + 1);		\
		vb += 2;			\
		dev_priv->dma_low += 8;		\
	}

#define VIA_TRACKER_INTERVAL 0x20000

struct via_dma_tracker {
	struct list_head head;
	uint32_t seq;
	uint32_t loc;
};

static void via_cmdbuf_start(struct drm_via_private *dev_priv);
static void via_cmdbuf_pause(struct drm_via_private *dev_priv);
static void via_cmdbuf_rewind(struct drm_via_private *dev_priv);

/*
 * FIXME: A memory cache for trackers?
 */

static void via_traverse_trackers(struct drm_via_private *dev_priv)
{
	struct via_dma_tracker *tracker, *next;
	uint32_t cur_seq = ioread32(dev_priv->fence_map);

	list_for_each_entry_safe(tracker, next, &dev_priv->dma_trackers, head) {
		if ((cur_seq - tracker->seq) < (1 << 31)) {
			dev_priv->dma_free = tracker->loc;
			list_del(&tracker->head);
			kfree(tracker);
		} else {
			break;
		}
	}
}

/*
 * Fixme: Wait interruptible.
 */

static inline int via_cmdbuf_wait(struct drm_via_private *dev_priv,
				  unsigned int size)
{
	uint32_t cur_addr = dev_priv->dma_low;
	uint32_t next_addr = cur_addr + size + (512 * 1024);

	while (unlikely(dev_priv->dma_free > cur_addr &&
			dev_priv->dma_free <= next_addr)) {
		msleep(1);
		via_traverse_trackers(dev_priv);
	}
	return 0;
}

static bool via_no_tracker(struct drm_via_private *dev_priv)
{
	via_traverse_trackers(dev_priv);
	return (((dev_priv->dma_low - dev_priv->dma_tracker) & VIA_AGPC_MASK) <
		VIA_TRACKER_INTERVAL);
}


static int via_add_tracker(struct drm_via_private *dev_priv, uint32_t sequence)
{
	struct via_dma_tracker *tracker;

	dev_priv->dma_tracker = dev_priv->dma_low;
	tracker = kmalloc(sizeof(*tracker), GFP_KERNEL);

	if (!tracker)
		return -ENOMEM;

	tracker->loc = dev_priv->dma_low;
	tracker->seq = sequence;

	list_add_tail(&tracker->head, &dev_priv->dma_trackers);

	return 0;
}

/*
 * Checks whether buffer head has reach the end. Rewind the ring buffer
 * when necessary.
 *
 * Returns virtual pointer to ring buffer.
 */

static inline uint32_t __iomem *via_check_dma(struct drm_via_private *dev_priv,
					      unsigned int size)
{
	if ((dev_priv->dma_low + size + 4 * CMDBUF_ALIGNMENT_SIZE) >
	    dev_priv->dma_high) {

		/*
		 * Make sure the reader is wrapped before we do this!!
		 */

		via_cmdbuf_wait(dev_priv, size + 4 * CMDBUF_ALIGNMENT_SIZE);
		via_cmdbuf_rewind(dev_priv);
	}
	if (via_cmdbuf_wait(dev_priv, size) != 0) {
		return NULL;
	}

	return (uint32_t __iomem *) (dev_priv->dma_ptr + dev_priv->dma_low);
}

void via_dma_initialize(struct drm_via_private *dev_priv)
{
	dev_priv->dma_ptr = (char *)dev_priv->agpc_map;
	dev_priv->dma_low = 0;
	dev_priv->dma_tracker = dev_priv->dma_low;
	dev_priv->dma_high = dev_priv->agpc_bo->num_pages << PAGE_SHIFT;
	dev_priv->dma_free = dev_priv->dma_high;
	dev_priv->dma_wrap = dev_priv->dma_high;
	dev_priv->dma_offset = dev_priv->agpc_bo->offset;
	dev_priv->last_pause_ptr = NULL;
	dev_priv->hw_addr_ptr =
	    (uint32_t __iomem *) (dev_priv->mmio_map + 0x418);

	via_cmdbuf_start(dev_priv);
}

static void via_emit_blit_sequence(struct drm_via_private *dev_priv,
				   uint32_t __iomem * vb,
				   uint32_t offset, uint32_t value)
{
	uint32_t vram_offset = dev_priv->fence_bo->offset + offset;

	VIA_OUT_RING_H1(VIA_REG_GEMODE, VIA_GEM_32bpp);
	VIA_OUT_RING_H1(VIA_REG_FGCOLOR, value);
	VIA_OUT_RING_H1(VIA_REG_DSTBASE, (vram_offset & ~0x1f) >> 3);
	VIA_OUT_RING_H1(VIA_REG_PITCH, VIA_PITCH_ENABLE |
			(4 >> 3) | ((4 >> 3) << 16));
	VIA_OUT_RING_H1(VIA_REG_DSTPOS, (vram_offset & 0x1f) >> 2);
	VIA_OUT_RING_H1(VIA_REG_DIMENSION, 0);
	VIA_OUT_RING_H1(VIA_REG_GECMD, VIA_GEC_BLT | VIA_GEC_FIXCOLOR_PAT
			| (VIA_ROP_PAT << 24));

	atomic_set(&dev_priv->emitted_cmd_seq, value);
}

static void via_blit_sequence(struct drm_via_private *dev_priv,
			      uint32_t offset, uint32_t value)
{
	uint32_t vram_offset = dev_priv->fence_bo->offset + offset;

	VIA_WRITE(VIA_REG_GEMODE, VIA_GEM_32bpp);
	VIA_WRITE(VIA_REG_FGCOLOR, value);
	VIA_WRITE(VIA_REG_DSTBASE, (vram_offset & ~0x1f) >> 3);
	VIA_WRITE(VIA_REG_PITCH, VIA_PITCH_ENABLE |
		  (4 >> 3) | ((4 >> 3) << 16));
	VIA_WRITE(VIA_REG_DSTPOS, (vram_offset & 0x1f) >> 2);
	VIA_WRITE(VIA_REG_DIMENSION, 0);
	VIA_WRITE(VIA_REG_GECMD, VIA_GEC_BLT | VIA_GEC_FIXCOLOR_PAT
		  | (VIA_ROP_PAT << 24));

	atomic_set(&dev_priv->emitted_cmd_seq, value);
}

/*
 * This function is used internally by ring buffer mangement code.
 *
 * Returns virtual pointer to ring buffer.
 */
static inline uint32_t __iomem *via_get_dma(struct drm_via_private *dev_priv)
{
	return (uint32_t __iomem *) (dev_priv->dma_ptr + dev_priv->dma_low);
}

static void via_emit_fence_seq(struct drm_via_private *dev_priv,
			       uint32_t offset, uint32_t value)
{
	via_emit_blit_sequence(dev_priv, via_get_dma(dev_priv), offset, value);
}

void via_emit_fence_seq_standalone(struct drm_via_private *dev_priv,
				   uint32_t offset, uint32_t value)
{
	via_emit_blit_sequence(dev_priv,
			       via_check_dma(dev_priv, VIA_FENCE_EXTRA),
			       offset, value);
	via_cmdbuf_pause(dev_priv);
}

int via_driver_dma_quiescent(struct drm_device *dev)
{
	struct drm_via_private *dev_priv = dev->dev_private;

	via_wait_idle(dev_priv);
	return 0;
}

static inline uint32_t *via_align_buffer(struct drm_via_private *dev_priv,
					 uint32_t * vb, int qw_count)
{
	for (; qw_count > 0; --qw_count) {
		VIA_OUT_RING_QW(HC_DUMMY, HC_DUMMY);
	}
	return vb;
}

/*
 * Hooks a segment of data into the tail of the ring-buffer by
 * modifying the pause address stored in the buffer itself. If
 * the regulator has already paused, restart it.
 */
static int via_hook_segment(struct drm_via_private *dev_priv,
			    uint32_t pause_addr_hi, uint32_t pause_addr_lo,
			    int no_pci_fire)
{
	int paused, count;
	uint32_t *paused_at = dev_priv->last_pause_ptr;
	uint32_t reader, ptr;
	uint32_t diff;

	paused = 0;
	via_flush_write_combine();
	(void)ioread32((uint32_t *) (via_get_dma(dev_priv) - 1));
	iowrite32(pause_addr_lo, paused_at);
	via_flush_write_combine();
	(void)ioread32(paused_at);

	reader = ioread32(dev_priv->hw_addr_ptr);
	ptr = ((volatile char *)paused_at - dev_priv->dma_ptr) +
	    dev_priv->dma_offset + 4;

	dev_priv->last_pause_ptr = via_get_dma(dev_priv) - 1;

	/*
	 * If there is a possibility that the command reader will
	 * miss the new pause address and pause on the old one,
	 * In that case we need to program the new start address
	 * using PCI.
	 */

	diff = (uint32_t) (ptr - reader) - dev_priv->dma_diff;
	count = 10000000;

	while ((diff < CMDBUF_ALIGNMENT_SIZE) && count--) {
		paused = (VIA_READ(0x41c) & 0x80000000);
		if (paused)
			break;
		reader = ioread32(dev_priv->hw_addr_ptr);
		diff = (uint32_t) (ptr - reader) - dev_priv->dma_diff;
	}

	paused = VIA_READ(0x41c) & 0x80000000;
	if (paused && !no_pci_fire) {
		reader = ioread32(dev_priv->hw_addr_ptr);
		diff = (uint32_t) (ptr - reader) - dev_priv->dma_diff;
		diff &= (dev_priv->dma_high - 1);
		if (diff < (dev_priv->dma_high >> 1)) {

			if (diff != 0) {
				uint32_t __iomem *rekick;

				DRM_INFO("Paused at incorrect address. "
					 "0x%08x, 0x%08x 0x%08x 0x%08x. Restarting.\n",
					 ptr, reader, VIA_READ(0x40c),
					 dev_priv->dma_diff);
				/*
				 * Obtain the new pause address the command
				 * reader was supposed to pick up.
				 */

				rekick = (uint32_t *)
				    dev_priv->dma_ptr +
				    ((reader - dev_priv->dma_offset +
				      dev_priv->dma_diff - 4) >> 2);
				pause_addr_lo = ioread32(rekick);
				pause_addr_hi = ioread32(--rekick);
				DRM_INFO("Restarting 0x%08x 0x%08x\n",
					 pause_addr_hi, pause_addr_lo);
			}

			/*
			 * There is a concern that these writes may stall the PCI bus
			 * if the GPU is not idle. However, idling the GPU first
			 * doesn't make a difference.
			 */
			VIA_WRITE(VIA_REG_TRANSET, (HC_ParaType_PreCR << 16));
			VIA_WRITE(VIA_REG_TRANSPACE, pause_addr_hi);
			VIA_WRITE(VIA_REG_TRANSPACE, pause_addr_lo);

			/*
			 * Really need to flush PCI posting here,
			 * but some register reads will
			 * flush AGP completely according to docs.
			 * FIXME: Find a suitable register to read.
			 */
		}
	}

	return paused;
}

static inline int via_is_idle(uint32_t status)
{
	return ((status & (VIA_VR_QUEUE_BUSY |
			   VIA_CMD_RGTR_BUSY |
			   VIA_2D_ENG_BUSY |
			   VIA_3D_ENG_BUSY)) == (VIA_VR_QUEUE_BUSY));
}

void via_wait_idle(struct drm_via_private *dev_priv)
{
	unsigned long _end = jiffies + VIA_IDLE_TIMEOUT;
	uint32_t status;

	status = VIA_READ(VIA_REG_STATUS);
	do {
		if (unlikely(!via_is_idle(status))) {
			schedule();
			status = VIA_READ(VIA_REG_STATUS);
		}
	} while (unlikely(!time_after_eq(jiffies, _end) &&
			  !via_is_idle(status)));

	if (unlikely(!via_is_idle(status)))
		DRM_INFO("Warning: Idle timeout.\n");

	return;
}

static uint32_t *via_align_cmd(struct drm_via_private *dev_priv,
			       uint32_t cmd_type, uint32_t addr,
			       uint32_t * cmd_addr_hi, uint32_t * cmd_addr_lo,
			       int skip_wait)
{
	uint32_t agp_base;
	uint32_t cmd_addr, addr_lo, addr_hi;
	uint32_t *vb;
	uint32_t qw_pad_count;

	if (!skip_wait)
		via_cmdbuf_wait(dev_priv, 2 * CMDBUF_ALIGNMENT_SIZE);

	vb = via_get_dma(dev_priv);
	VIA_OUT_RING_QW(HC_HEADER2 | ((VIA_REG_TRANSET >> 2) << 12) |
			(VIA_REG_TRANSPACE >> 2), HC_ParaType_PreCR << 16);

	agp_base = dev_priv->dma_offset;
	qw_pad_count = (CMDBUF_ALIGNMENT_SIZE >> 3) -
	    ((dev_priv->dma_low & CMDBUF_ALIGNMENT_MASK) >> 3);

	cmd_addr = (addr) ? addr :
	    agp_base + dev_priv->dma_low - 4 + (qw_pad_count << 3);
	addr_lo = ((HC_SubA_HAGPBpL << 24) | (cmd_type & HC_HAGPBpID_MASK) |
		   (cmd_addr & HC_HAGPBpL_MASK));
	addr_hi = ((HC_SubA_HAGPBpH << 24) | (cmd_addr >> 24));

	vb = via_align_buffer(dev_priv, vb, qw_pad_count - 1);
	VIA_OUT_RING_QW(*cmd_addr_hi = addr_hi, *cmd_addr_lo = addr_lo);

	return vb;
}

static void via_cmdbuf_start(struct drm_via_private *dev_priv)
{
	uint32_t pause_addr_lo, pause_addr_hi;
	uint32_t start_addr, start_addr_lo;
	uint32_t end_addr, end_addr_lo;
	uint32_t command;
	uint32_t agp_base;
	uint32_t ptr;
	uint32_t reader;
	int count;

	dev_priv->dma_low = 0;

	agp_base = dev_priv->dma_offset;
	start_addr = agp_base;
	end_addr = agp_base + dev_priv->dma_high;

	start_addr_lo = ((HC_SubA_HAGPBstL << 24) | (start_addr & 0xFFFFFF));
	end_addr_lo = ((HC_SubA_HAGPBendL << 24) | (end_addr & 0xFFFFFF));
	command = ((HC_SubA_HAGPCMNT << 24) | (start_addr >> 24) |
		   ((end_addr & 0xff000000) >> 16));

	dev_priv->last_pause_ptr =
	    via_align_cmd(dev_priv, HC_HAGPBpID_PAUSE, 0,
			  &pause_addr_hi, &pause_addr_lo, 1) - 1;

	via_flush_write_combine();
	(void)ioread32((uint32_t *) dev_priv->last_pause_ptr);

	VIA_WRITE(VIA_REG_TRANSET, (HC_ParaType_PreCR << 16));
	VIA_WRITE(VIA_REG_TRANSPACE, command);
	VIA_WRITE(VIA_REG_TRANSPACE, start_addr_lo);
	VIA_WRITE(VIA_REG_TRANSPACE, end_addr_lo);

	VIA_WRITE(VIA_REG_TRANSPACE, pause_addr_hi);
	VIA_WRITE(VIA_REG_TRANSPACE, pause_addr_lo);
	DRM_WRITEMEMORYBARRIER();
	VIA_WRITE(VIA_REG_TRANSPACE, command | HC_HAGPCMNT_MASK);
	VIA_READ(VIA_REG_TRANSPACE);

	dev_priv->dma_diff = 0;

	count = 10000000;
	while (!(VIA_READ(0x41c) & 0x80000000) && count--) ;

	reader = ioread32(dev_priv->hw_addr_ptr);
	ptr = ((volatile char *)dev_priv->last_pause_ptr - dev_priv->dma_ptr) +
	    dev_priv->dma_offset + 4;

	/*
	 * This is the difference between where we tell the
	 * command reader to pause and where it actually pauses.
	 * This differs between hw implementation so we need to
	 * detect it.
	 */

	dev_priv->dma_diff = ptr - reader;
}

static inline void via_dummy_bitblt(struct drm_via_private *dev_priv)
{
	uint32_t *vb = via_get_dma(dev_priv);
	VIA_OUT_RING_H1(VIA_REG_DSTPOS, (0 | (0 << 16)));
	VIA_OUT_RING_H1(VIA_REG_DIMENSION, 0 | (0 << 16));
	VIA_OUT_RING_H1(VIA_REG_GECMD, VIA_GEC_BLT | VIA_GEC_FIXCOLOR_PAT |
			0xAA000000);
}

static void via_cmdbuf_rewind(struct drm_via_private *dev_priv)
{
	uint32_t agp_base;
	uint32_t pause_addr_lo, pause_addr_hi;
	uint32_t jump_addr_lo, jump_addr_hi, hook_addr;
	uint32_t *hook_ptr;
	uint32_t dma_low_save1, dma_low_save2;

	agp_base = dev_priv->dma_offset;
	hook_ptr = via_align_cmd(dev_priv, HC_HAGPBpID_JUMP, 0,
				 &jump_addr_hi, &jump_addr_lo, 0);
	via_align_cmd(dev_priv, HC_HAGPBpID_JUMP, 0, &jump_addr_hi,
		      &hook_addr, 0);
	iowrite32(hook_addr, --hook_ptr);
	dev_priv->dma_wrap = dev_priv->dma_low;

	/*
	 * Wrap command buffer to the beginning.
	 */

	dev_priv->dma_low = 0;
	if (via_cmdbuf_wait(dev_priv, CMDBUF_ALIGNMENT_SIZE) != 0) {
		DRM_ERROR("via_cmdbuf_jump failed\n");
	}

	via_dummy_bitblt(dev_priv);
	via_dummy_bitblt(dev_priv);

	hook_ptr =
	    via_align_cmd(dev_priv, HC_HAGPBpID_PAUSE, 0, &pause_addr_hi,
			  &pause_addr_lo, 0);
	via_align_cmd(dev_priv, HC_HAGPBpID_PAUSE, 0, &pause_addr_hi,
		      &pause_addr_lo, 0);

	iowrite32(pause_addr_lo, --hook_ptr);
	dma_low_save1 = dev_priv->dma_low;

	/*
	 * Now, set a trap that will pause the regulator if it tries to rerun the old
	 * command buffer. (Which may happen if via_hook_segment detecs a command regulator pause
	 * and reissues the jump command over PCI, while the regulator has already taken the jump
	 * and actually paused at the current buffer end).
	 * There appears to be no other way to detect this condition, since the hw_addr_pointer
	 * does not seem to get updated immediately when a jump occurs.
	 */

	hook_ptr =
	    via_align_cmd(dev_priv, HC_HAGPBpID_PAUSE, 0, &pause_addr_hi,
			  &pause_addr_lo, 0);
	via_align_cmd(dev_priv, HC_HAGPBpID_PAUSE, 0, &pause_addr_hi,
		      &pause_addr_lo, 0);
	iowrite32(pause_addr_lo, --hook_ptr);

	dma_low_save2 = dev_priv->dma_low;
	dev_priv->dma_low = dma_low_save1;
	via_hook_segment(dev_priv, jump_addr_hi, jump_addr_lo, 0);
	dev_priv->dma_low = dma_low_save2;
	via_hook_segment(dev_priv, pause_addr_hi, pause_addr_lo, 0);
}

static void via_cmdbuf_flush(struct drm_via_private *dev_priv,
			     uint32_t cmd_type)
{
	uint32_t pause_addr_lo, pause_addr_hi, hook;
	uint32_t __iomem *hook_addr;

	hook_addr =
	    via_align_cmd(dev_priv, cmd_type, 0, &pause_addr_hi, &pause_addr_lo,
			  0);
	if (cmd_type == HC_HAGPBpID_PAUSE) {
		via_align_cmd(dev_priv, cmd_type, 0, &pause_addr_hi, &hook, 0);
		iowrite32(hook, --hook_addr);
	}
	via_hook_segment(dev_priv, pause_addr_hi, pause_addr_lo, 0);
}

static void via_cmdbuf_pause(struct drm_via_private *dev_priv)
{
	via_cmdbuf_flush(dev_priv, HC_HAGPBpID_PAUSE);

}

void via_dma_takedown(struct drm_via_private *dev_priv)
{
	via_cmdbuf_flush(dev_priv, HC_HAGPBpID_STOP);
	via_wait_idle(dev_priv);
	while (!list_empty(&dev_priv->dma_trackers))
		via_traverse_trackers(dev_priv);
}

int via_copy_cmdbuf(struct drm_via_private *dev_priv,
		    uint64_t cmd_buffer,
		    uint32_t size,
		    uint32_t mechanism, uint32_t ** cmdbuf_addr, int *is_iomem)
{
	void __user *commands = (void __user *)(unsigned long)cmd_buffer;
	uint32_t *vb;
	int ret;

	if (unlikely(size > VIA_PCI_BUF_SIZE)) {
		DRM_ERROR("Command buffer too large.\n");
		return -ENOMEM;
	}

	if (mechanism == _VIA_MECHANISM_AGP && drm_via_disable_verifier) {
		uint32_t check_size = size + VIA_FENCE_EXTRA;

		if (check_size < VIA_PAD_SUBMISSION_SIZE)
			check_size = VIA_PAD_SUBMISSION_SIZE;

		vb = via_check_dma(dev_priv, check_size);
		if (unlikely(!vb)) {
			DRM_ERROR("No space in AGP ring buffer.\n");
			return -EBUSY;
		}
		*is_iomem = 1;
	} else {
		vb = (uint32_t *) dev_priv->pci_buf;
		*is_iomem = 0;
	}

	ret = copy_from_user(vb, commands, size);
	if (unlikely(ret != 0)) {
		DRM_ERROR("Failed copying command "
			  "buffer from user space.\n");
		return ret;
	}
	*cmdbuf_addr = vb;
	return 0;
}

static void via_pad_submission(struct drm_via_private *dev_priv, int qwords)
{
	uint32_t *vb;

	if (unlikely(qwords == 0))
		return;
	vb = via_get_dma(dev_priv);
	VIA_OUT_RING_QW(HC_HEADER2, HC_ParaType_NotTex << 16);
	if (unlikely(qwords == 1))
		return;
	via_align_buffer(dev_priv, vb, qwords);
}


int via_dispatch_commands(struct drm_device *dev, unsigned long size,
			  uint32_t mechanism, bool emit_seq)
{
	struct drm_via_private *dev_priv = via_priv(dev);
	uint32_t *vb;
	uint32_t seq;
	int ret;

	switch (mechanism) {
	case _VIA_MECHANISM_AGP:
		if (!drm_via_disable_verifier) {
		        unsigned long check_size =
				size + VIA_FENCE_EXTRA;

			if (check_size < VIA_PAD_SUBMISSION_SIZE)
				check_size = VIA_PAD_SUBMISSION_SIZE;

			vb = via_check_dma(dev_priv, check_size);
			if (unlikely(!vb)) {
				DRM_ERROR("No space in AGP ring buffer.\n");
				return -EBUSY;
			}
			memcpy_toio(vb, dev_priv->pci_buf, size);
		}
		dev_priv->dma_low += size;
		seq =
		    atomic_add_return(1, &dev_priv->fence_seq[VIA_ENGINE_CMD]);

		if (emit_seq || !via_no_tracker(dev_priv)) {
			via_emit_fence_seq(dev_priv, VIA_FENCE_OFFSET_CMD, seq);
			via_add_tracker(dev_priv, seq);
			size += VIA_FENCE_EXTRA;
		}

		if (size < VIA_PAD_SUBMISSION_SIZE)
		    via_pad_submission(dev_priv, (VIA_PAD_SUBMISSION_SIZE - size) >> 3);

		via_cmdbuf_pause(dev_priv);
		return 0;
	case _VIA_MECHANISM_PCI:
		via_wait_idle(dev_priv);
		ret = via_parse_command_stream(dev,
					       (uint32_t *) dev_priv->pci_buf,
					       size);
		seq =
		    atomic_add_return(1, &dev_priv->fence_seq[VIA_ENGINE_CMD]);
		if (emit_seq) {
			via_wait_idle(dev_priv);
			via_blit_sequence(dev_priv, VIA_FENCE_OFFSET_CMD, seq);
			via_wait_idle(dev_priv);
		}
		return ret;
	default:
		return -EINVAL;
	}

	return -EINVAL;
}
