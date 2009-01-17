#
# Makefile for the drm device driver.  This driver provides support for the
# Direct Rendering Infrastructure (DRI) in XFree86 4.1.0 and higher.

#ccflags-m := -I..
openchrome-y    := via_buffer.o via_context.o via_dmablit.o via_dma.o \
	via_drv.o via_execbuf.o via_init.o via_irq.o via_ttm_fence.o \
	via_ttm_glue.o via_verifier.o via_video.o \
	ttm_agp_backend.o ttm_bo.o ttm_bo_util.o ttm_bo_vm.o \
	ttm_execbuf_util.o ttm_fence.o ttm_fence_user.o ttm_lock.o \
	ttm_memory.o ttm_object.o ttm_pat_compat.o ttm_placement_user.o \
	ttm_tt.o

obj-m += openchrome.o
