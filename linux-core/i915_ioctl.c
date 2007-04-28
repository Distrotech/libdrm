
#include "drmP.h"
#include "drm.h"
#include "i915_drm.h"
#include "i915_drv.h"
/* ioctl wrappers for Linux for i915 */

static int i915_dma_init_ioctl(struct inode *inode, struct file *filp,
			       unsigned int cmd, unsigned long arg)
{
	drm_file_t	*priv	= filp->private_data;
	drm_device_t	*dev	= priv->head->dev;
	drm_i915_init_t init;
	drm_i915_init_t __user *argp = (void __user *)arg;
	int retcode = 0;

	if (copy_from_user(&init, argp, sizeof(init)))
		return -EFAULT;

	retcode = i915_dma_init(dev, &init);

	return retcode;
}

static int i915_flush_ioctl(struct inode *inode, struct file *filp,
			    unsigned int cmd, unsigned long arg)
{
	drm_file_t	*priv	= filp->private_data;
	drm_device_t	*dev	= priv->head->dev;

	LOCK_TEST_WITH_RETURN(dev, filp);

	return i915_quiescent(dev);
}

static int i915_flip_ioctl(struct inode *inode, struct file *filp,
			   unsigned int cmd, unsigned long arg)
{
	drm_file_t	*priv	= filp->private_data;
	drm_device_t	*dev	= priv->head->dev;
	drm_i915_flip_t param;
	drm_i915_flip_t __user *argp = (void __user *)arg;

	DRM_DEBUG("%s\n", __FUNCTION__);

	LOCK_TEST_WITH_RETURN(dev, filp);

	if (copy_from_user(&param, argp, sizeof(param)))
		return -EFAULT;

	if (param.pipes & ~0x3) {
		DRM_ERROR("Invalid pipes 0x%x, only <= 0x3 is valid\n",
			  param.pipes);
		return DRM_ERR(EINVAL);
	}

	i915_dispatch_flip(dev, param.pipes, 0);

	return 0;
}

static int i915_batchbuffer_ioctl(struct inode *inode, struct file *filp,
				  unsigned int cmd, unsigned long arg)
{
	drm_file_t	*priv	= filp->private_data;
	drm_device_t	*dev	= priv->head->dev;
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	drm_i915_batchbuffer_t batch;
	drm_i915_batchbuffer_t __user *argp = (void __user *)arg;
	int ret;

	if (!dev_priv->allow_batchbuffer) {
		DRM_ERROR("Batchbuffer ioctl disabled\n");
		return DRM_ERR(EINVAL);
	}

	if (copy_from_user(&batch, argp, sizeof(batch)))
		return -EFAULT;

	DRM_DEBUG("i915 batchbuffer, start %x used %d cliprects %d\n",
		  batch.start, batch.used, batch.num_cliprects);

	LOCK_TEST_WITH_RETURN(dev, filp);

	if (batch.num_cliprects)
		if (!access_ok(VERIFY_READ, batch.cliprects,
			       batch.num_cliprects *
			       sizeof(drm_clip_rect_t)))
			return -EFAULT;

	ret = i915_dispatch_batchbuffer(dev, &batch);

	return ret;
}

static int i915_cmdbuffer_ioctl(struct inode *inode, struct file *filp,
				unsigned int cmd, unsigned long arg)
{
	drm_file_t	*priv	= filp->private_data;
	drm_device_t	*dev	= priv->head->dev;
	drm_i915_cmdbuffer_t cmdbuf;
	drm_i915_cmdbuffer_t __user *argp = (void __user *)arg;

	if (copy_from_user(&cmdbuf, argp, sizeof(cmdbuf)))
		return -EFAULT;

	DRM_DEBUG("i915 cmdbuffer, buf %p sz %d cliprects %d\n",
		  cmdbuf.buf, cmdbuf.sz, cmdbuf.num_cliprects);

	LOCK_TEST_WITH_RETURN(dev, filp);

	if (cmdbuf.num_cliprects)
		if (!access_ok(VERIFY_READ, cmdbuf.cliprects,
			       cmdbuf.num_cliprects *
			       sizeof(drm_clip_rect_t))) {
		DRM_ERROR("Fault accessing cliprects\n");
		return DRM_ERR(EFAULT);
	}

	return i915_dispatch_cmdbuffer(dev, &cmdbuf);
}



/* Needs the lock as it touches the ring.
 */
static int i915_irq_emit_ioctl(struct inode *inode, struct file *filp,
			unsigned int cmd, unsigned long arg)
{
	drm_file_t	*priv	= filp->private_data;
	drm_device_t	*dev	= priv->head->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_i915_irq_emit_t emit;
	drm_i915_irq_emit_t __user *argp = (void __user *)arg;
	int result;

	LOCK_TEST_WITH_RETURN(dev, filp);

	if (!dev_priv) {
		DRM_ERROR("%s called with no initialization\n", __FUNCTION__);
		return DRM_ERR(EINVAL);
	}

	if (copy_from_user(&emit, argp, sizeof(emit)))
		return -EFAULT;

	result = i915_emit_irq(dev);

	if (copy_to_user(emit.irq_seq, &result, sizeof(int)))
		return -EFAULT;

	return 0;
}

/* Doesn't need the hardware lock.
 */
static int i915_irq_wait_ioctl(struct inode *inode, struct file *filp,
			       unsigned int cmd, unsigned long arg)
{
	drm_file_t	*priv	= filp->private_data;
	drm_device_t	*dev	= priv->head->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_i915_irq_wait_t irqwait;
	drm_i915_irq_wait_t __user *argp = (void __user *)arg;

	if (!dev_priv) {
		DRM_ERROR("%s called with no initialization\n", __FUNCTION__);
		return DRM_ERR(EINVAL);
	}

	if (copy_from_user(&irqwait, argp, sizeof(irqwait)))
		return -EFAULT;

	return i915_wait_irq(dev, irqwait.irq_seq);
}

static int i915_getparam_ioctl(struct inode *inode, struct file *filp,
			       unsigned int cmd, unsigned long arg)
{
	drm_file_t	*priv	= filp->private_data;
	drm_device_t	*dev	= priv->head->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;	
	drm_i915_getparam_t param;
	drm_i915_getparam_t __user *argp = (void __user *)arg;
	int retcode;
	int value;

	if (!dev_priv) {
		DRM_ERROR("%s called with no initialization\n", __FUNCTION__);
		return -EINVAL;
	}

	if (copy_from_user(&param, argp, sizeof(param)))
		return -EFAULT;

	retcode = i915_getparam(dev, &param, &value);
	if (!retcode) {
		if (copy_to_user(param.value, &value, sizeof(int)))
			return -EFAULT;
	}
	return retcode;
}

static int i915_setparam_ioctl(struct inode *inode, struct file *filp,
			       unsigned int cmd, unsigned long arg)
{
	drm_file_t	*priv	= filp->private_data;
	drm_device_t	*dev	= priv->head->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;	
	drm_i915_setparam_t param;
	drm_i915_setparam_t __user *argp = (void __user *)arg;

	if (!dev_priv) {
		DRM_ERROR("%s called with no initialization\n", __FUNCTION__);
		return -EINVAL;
	}

	if (copy_from_user(&param, argp, sizeof(param)))
		return -EFAULT;

	return i915_setparam(dev, &param);
}

static int i915_mmio_ioctl(struct inode *inode, struct file *filp,
			   unsigned int cmd, unsigned long arg)
{
	drm_file_t	*priv	= filp->private_data;
	drm_device_t	*dev	= priv->head->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_i915_mmio_t mmio;
	drm_i915_mmio_t __user *argp = (void __user *)arg;
	char buf[32];
	int retcode = 0;
	int size;

	if (!dev_priv) {
		DRM_ERROR("%s called with no initialization\n", __FUNCTION__);
		return DRM_ERR(EINVAL);
	}

	if (copy_from_user(&mmio, argp, sizeof(mmio)))
		return -EFAULT;
	
	if (i915_mmio_get_size(dev, &mmio, &size))
		return -EINVAL;

	switch(mmio.read_write) {
	case I915_MMIO_READ:
		retcode = i915_mmio_read(dev, &mmio, buf);
		if (retcode)
			return retcode;
		if (copy_to_user(mmio.data, buf, size))
			return -EFAULT;
		break;
	case I915_MMIO_WRITE:
		if (copy_from_user(buf, mmio.data, size))
			return -EFAULT;
		retcode = i915_mmio_write(dev, &mmio, buf);
	}
	return retcode;
}

static int i915_mem_alloc_ioctl(struct inode *inode, struct file *filp,
				unsigned int cmd, unsigned long arg)
{
	drm_file_t	*priv	= filp->private_data;
	drm_device_t	*dev	= priv->head->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_i915_mem_alloc_t alloc;
	drm_i915_mem_alloc_t __user *argp = (void __user *)arg;
	int retcode;
	int start;
	if (!dev_priv) {
		DRM_ERROR("%s called with no initialization\n", __FUNCTION__);
		return -EINVAL;
	}

	if(copy_from_user(&alloc, argp, sizeof(alloc)))
		return -EFAULT;

	retcode = i915_mem_alloc(dev, &alloc, filp, &start);

	if (!retcode) {
		if (copy_to_user(alloc.region_offset, &start, sizeof(int)))
			return -EFAULT;
	}
	return retcode;
}

static int i915_mem_free_ioctl(struct inode *inode, struct file *filp,
			       unsigned int cmd, unsigned long arg)
{
	drm_file_t	*priv	= filp->private_data;
	drm_device_t	*dev	= priv->head->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_i915_mem_free_t memfree;
	drm_i915_mem_free_t __user *argp = (void __user *)arg;
	
	if (!dev_priv) {
		DRM_ERROR("%s called with no initialization\n", __FUNCTION__);
		return DRM_ERR(EINVAL);
	}

	if (copy_from_user(&memfree, argp, sizeof(memfree)))
		return -EFAULT;

	return i915_mem_free(dev, &memfree, filp);
}

static int i915_mem_init_heap_ioctl(struct inode *inode, struct file *filp,
				    unsigned int cmd, unsigned long arg)
{
	drm_file_t	*priv	= filp->private_data;
	drm_device_t	*dev	= priv->head->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_i915_mem_init_heap_t initheap;
	drm_i915_mem_init_heap_t __user *argp = (void __user *)arg;

	if (!dev_priv) {
		DRM_ERROR("%s called with no initialization\n", __FUNCTION__);
		return DRM_ERR(EINVAL);
	}
	
	if (copy_from_user(&initheap, argp, sizeof(initheap)))
		return -EFAULT;

	return i915_mem_init_heap(dev, &initheap);
}

static int i915_mem_destroy_heap_ioctl(struct inode *inode, struct file *filp,
				       unsigned int cmd, unsigned long arg)
{
	drm_file_t	*priv	= filp->private_data;
	drm_device_t	*dev	= priv->head->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_i915_mem_destroy_heap_t destroyheap;
	drm_i915_mem_destroy_heap_t __user *argp = (void __user *)arg;

	if (!dev_priv) {
		DRM_ERROR("%s called with no initialization\n", __FUNCTION__);
		return DRM_ERR(EINVAL);
	}

	if (copy_from_user(&destroyheap, argp, sizeof(destroyheap)))
		return -EFAULT;

	return i915_mem_destroy_heap(dev, &destroyheap);
}

static int i915_vblank_pipe_set_ioctl(struct inode *inode, struct file *filp,
				       unsigned int cmd, unsigned long arg)
{
	drm_file_t	*priv	= filp->private_data;
	drm_device_t	*dev	= priv->head->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_i915_vblank_pipe_t pipe;
	drm_i915_vblank_pipe_t __user *argp = (void __user *)arg;

	if (!dev_priv) {
		DRM_ERROR("%s called with no initialization\n", __FUNCTION__);
		return DRM_ERR(EINVAL);
	}

	if (copy_from_user(&pipe, argp, sizeof(pipe)))
		return -EFAULT;

	return i915_vblank_pipe_set(dev, &pipe);
}

static int i915_vblank_pipe_get_ioctl(struct inode *inode, struct file *filp,
				       unsigned int cmd, unsigned long arg)
{
	drm_file_t	*priv	= filp->private_data;
	drm_device_t	*dev	= priv->head->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_i915_vblank_pipe_t pipe;
	drm_i915_vblank_pipe_t __user *argp = (void __user *)arg;
	int retcode;

	if (!dev_priv) {
		DRM_ERROR("%s called with no initialization\n", __FUNCTION__);
		return DRM_ERR(EINVAL);
	}

	retcode = i915_vblank_pipe_get(dev, &pipe);

	if (copy_to_user(argp, &pipe, sizeof(pipe)))
		return -EFAULT;
	return retcode;
}

static int i915_vblank_swap_ioctl(struct inode *inode, struct file *filp,
				  unsigned int cmd, unsigned long arg)
{
	drm_file_t	*priv	= filp->private_data;
	drm_device_t	*dev	= priv->head->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_i915_vblank_swap_t swap;
	drm_i915_vblank_swap_t __user *argp = (void __user *)arg;
	int retcode;

	if (!dev_priv) {
		DRM_ERROR("%s called with no initialization\n", __func__);
		return -EINVAL;
	}

	if (copy_from_user(&swap, argp, sizeof(swap)))
		return -EFAULT;

	retcode = i915_vblank_swap(dev, &swap, filp);
	
	if (!retcode) {
		if (copy_to_user(argp, &swap, sizeof(swap)))
			return -EFAULT;
	}

	return retcode;
}

drm_ioctl_desc_t i915_ioctls[] = {
	[DRM_IOCTL_NR(DRM_I915_INIT)] = {i915_dma_init_ioctl, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY},
	[DRM_IOCTL_NR(DRM_I915_FLUSH)] = {i915_flush_ioctl, DRM_AUTH},
	[DRM_IOCTL_NR(DRM_I915_FLIP)] = {i915_flip_ioctl, DRM_AUTH},
	[DRM_IOCTL_NR(DRM_I915_BATCHBUFFER)] = {i915_batchbuffer_ioctl, DRM_AUTH},
	[DRM_IOCTL_NR(DRM_I915_IRQ_EMIT)] = {i915_irq_emit_ioctl, DRM_AUTH},
	[DRM_IOCTL_NR(DRM_I915_IRQ_WAIT)] = {i915_irq_wait_ioctl, DRM_AUTH},
	[DRM_IOCTL_NR(DRM_I915_GETPARAM)] = {i915_getparam_ioctl, DRM_AUTH},
	[DRM_IOCTL_NR(DRM_I915_SETPARAM)] = {i915_setparam_ioctl, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY},
	[DRM_IOCTL_NR(DRM_I915_ALLOC)] = {i915_mem_alloc_ioctl, DRM_AUTH},
	[DRM_IOCTL_NR(DRM_I915_FREE)] = {i915_mem_free_ioctl, DRM_AUTH},
	[DRM_IOCTL_NR(DRM_I915_INIT_HEAP)] = {i915_mem_init_heap_ioctl, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY},
	[DRM_IOCTL_NR(DRM_I915_CMDBUFFER)] = {i915_cmdbuffer_ioctl, DRM_AUTH},
	[DRM_IOCTL_NR(DRM_I915_DESTROY_HEAP)] = { i915_mem_destroy_heap_ioctl, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY },
	[DRM_IOCTL_NR(DRM_I915_SET_VBLANK_PIPE)] = { i915_vblank_pipe_set_ioctl, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY },
	[DRM_IOCTL_NR(DRM_I915_GET_VBLANK_PIPE)] = { i915_vblank_pipe_get_ioctl, DRM_AUTH },
	[DRM_IOCTL_NR(DRM_I915_VBLANK_SWAP)] = {i915_vblank_swap_ioctl, DRM_AUTH},
	[DRM_IOCTL_NR(DRM_I915_MMIO)] = {i915_mmio_ioctl, DRM_AUTH},
};

int i915_max_ioctl = ARRAY_SIZE(i915_ioctls);
