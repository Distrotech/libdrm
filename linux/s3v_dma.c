/*
 * Author: Max Lingua <sunmax@libero.it>
 */

#include "s3v.h"
#include "drmP.h"
#include "s3v_drm.h"
#include "s3v_drv.h"

#include <linux/timex.h>
#include <linux/interrupt.h>	/* For task queue support */
#include <linux/delay.h>
#include <linux/tqueue.h>
#include <linux/wait.h>

#if 0
	#define S3V_DEBUG(fmt, arg...) \
	do { \
		printk(KERN_DEBUG \
			"[" DRM_NAME ":" __FUNCTION__ "] " fmt , ##arg); \
	} while (0)
#else
	#define S3V_DEBUG(fmt, arg...)       do { } while (0)
#endif

/* Uncomment next line if you are using a Virge DX */
/* It should not hurt on other Virges though */
/* #define _VIRGEDX */
#define S3V_UDELAY 1 /* 1000 */

struct tq_struct s3v_dma_task;
struct semaphore s3v_buf_sem;
struct semaphore s3v_gfx_sem;

/* FIXME: no global */
int _got, _sent, _freed, _reset;
volatile int _check;

void s3v_do_reset(drm_device_t *dev)
{
	drm_s3v_private_t *dev_priv =
		(drm_s3v_private_t *)dev->dev_private;
	u32 tmp, wp, rp;

	printk(KERN_ERR "*** s3v_do_reset: #%i\n", _reset); 
	_reset++;

	if (!s3v_dma_is_ready(dev))
		printk(KERN_ERR "and !s3v_dma_is_ready: -BAD-\n");

        S3V_FIFOSPACE(3);
		S3V_WRITE(S3V_CMD_DMA_ENABLE_REG, 0x0);
                S3V_WRITE(0x850C, (0x1 << 1));
                S3V_WRITE(0x850C, (0x0 << 1));


	S3V_FIFOSPACE(2);
		S3V_WRITE(S3V_CMD_DMA_WRITEP_REG, (0x0000 << 2));
		S3V_WRITE(S3V_CMD_DMA_READP_REG, (0x0000 << 2));

#if 1
	outb_p(0x66, 0x3d4);
	tmp = inb_p(0x3d5);
	outb_p(tmp | 0x02, 0x3d5);
	outb_p(tmp & ~0x02, 0x3d5);
#endif

	wp = (S3V_READ(S3V_CMD_DMA_WRITEP_REG) & 0xFFFC) >> 2;
	rp = (S3V_READ(S3V_CMD_DMA_READP_REG) & 0xFFFC) >> 2;

	S3V_FIFOSPACE(1);
		S3V_WRITE(S3V_CMD_DMA_ENABLE_REG, 0x1);

/*	printk(KERN_ERR "wp = 0x%x; rp = 0x%x\n", wp, rp); */
	S3V_DEBUG("wp = 0x%x; rp = 0x%x\n", wp, rp);

	_got = _sent = _freed = 0;
}

int s3v_reset( struct inode *inode, struct file *filp,
	unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	
	s3v_do_reset(dev);

	return 0;
}

int s3v_simple_lock( struct inode *inode, struct file *filp,
        unsigned int cmd, unsigned long arg )
{
/*
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_device_dma_t *dma = dev->dma;
*/
	/* printk(KERN_ERR "s3v_simple_lock\n"); */

	if (down_interruptible(&s3v_buf_sem))
		return -ERESTARTSYS;
	else
		return 0;
}

int s3v_simple_flush_lock( struct inode *inode, struct file *filp,
        unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_device_dma_t *dma = dev->dma;

	/* printk(KERN_ERR "s3v_simple_flush_lock\n"); */

	/* we flush bufs already on queue */

	/* FIXME: do we need the following? */
	/*
	if (dma->next_buffer) {
		wake_up_interruptible(&dma->waiting);
		printk(KERN_ERR "*** dma->next_buffer ***\n");
	}
	*/
	while (dma->this_buffer || !(s3v_dma_is_ready(dev))) {
		if (!_check) {
			_check++;
			queue_task(&s3v_dma_task, &tq_timer);
		}
		/* printk(KERN_ERR "a) dma->this_buffer=%p _check=%i\n",
			dma->this_buffer, _check); */
		interruptible_sleep_on(&dma->waiting);
		/* printk(KERN_ERR "b) dma->this_buffer=%p _check=%i\n",
			dma->this_buffer, _check); */
	}

	if (down_interruptible(&s3v_buf_sem))
		return -ERESTARTSYS;
	else
		return 0;
}

int s3v_simple_unlock( struct inode *inode, struct file *filp,
        unsigned int cmd, unsigned long arg )
{
/*
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
*/
/*	printk(KERN_ERR "s3v_simple_unlock\n"); */

	up(&s3v_buf_sem);

	return 0;
}

void s3v_do_status(drm_device_t *dev)
{
	drm_s3v_private_t *dev_priv =
		(drm_s3v_private_t *)dev->dev_private;
	u32 wp, rp;

	udelay(S3V_UDELAY);

	wp = (S3V_READ(S3V_CMD_DMA_WRITEP_REG) & 0xFFFC) >> 2;
	rp = (S3V_READ(S3V_CMD_DMA_READP_REG) & 0xFFFC) >> 2;

	printk(KERN_ERR "[status] wp = 0x%x; rp = 0x%x\n", wp, rp);

}

int s3v_status( struct inode *inode, struct file *filp,
        unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	
	s3v_do_status(dev);

	return 0;
}

void s3v_dma_check(void* _dev)
{
	drm_device_t *dev = (drm_device_t*) _dev;
	drm_s3v_private_t *dev_priv =
                (drm_s3v_private_t *)dev->dev_private;
	drm_device_dma_t *dma = dev->dma;

	int wp, rp;
	static u16 times=0;

	if (_check>1)
		printk(KERN_ERR "_check = %i\n", _check);

	if (times>40) {
        	s3v_do_reset(dev);
	}

	udelay(S3V_UDELAY);

	wp = (S3V_READ(S3V_CMD_DMA_WRITEP_REG) & 0xFFFC) >> 2;
	rp = (S3V_READ(S3V_CMD_DMA_READP_REG) & 0xFFFC) >> 2;

/*	rmb(); */

	if(wp==rp) {
/*
		printk(KERN_ERR "*** buf completed after %i times\n",
			times);
*/
		_check = 0;
		times = 0; 
		dma->this_buffer->pending = 0;
		dma->this_buffer = NULL; 
/*
		printk(KERN_ERR "[waking up the neighbours]\n");
		mdelay(1);
*/
	/* s3v_lock_free(dev,&dev->lock.hw_lock->lock,DRM_KERNEL_CONTEXT); */
		up(&s3v_gfx_sem);
		wake_up_interruptible(&dma->waiting);
	} else {
		times++; 
		queue_task(&s3v_dma_task, &tq_timer);
	}
}

static inline void s3v_dma_dispatch(drm_device_t *dev, drm_buf_t *dma_buf)
{
	drm_s3v_private_t *dev_priv =
		(drm_s3v_private_t *)dev->dev_private;

	drm_buf_t    *buf = dev->dma->buflist[S3V_DMA_BUF_NR];
	unsigned int *pgt = buf->address;

	down_interruptible(&s3v_buf_sem); 
	
	S3V_DEBUG("dma_buf->idx = %i\n", dma_buf->idx);
	S3V_DEBUG("dma_buf->used = %i\n", dma_buf->used);
	S3V_DEBUG("pgt[dma_buf->idx]) = 0x%x\n", pgt[dma_buf->idx]);

	_sent++;

#if 0
do {
	int i, reg, first_reg, num_commands;

	first_reg = *(int*)(dma_buf->address) >> 14;
	num_commands = *(int*)(dma_buf->address) & 0xFFFF;

	for(i=1; i<dma_buf->used/4; i++)
	{
		reg = first_reg+i*4;
		S3V_DEBUG("LOOK: 0x%x = %x\n", reg,
                        *(int*)(dma_buf->address+(i)*4));
	}
} while(0);		
#endif

	/* FIXME: this should reset dma read & write regs, but... */
	S3V_FIFOSPACE(2);
		S3V_WRITE(0x850C, (0x1 << 1));
		S3V_WRITE(0x850C, (0x0 << 1));

#ifdef _VIRGEDX
	S3V_FIFOSPACE(4);
#else
	S3V_FIFOSPACE(3);
#endif
	S3V_WRITE(S3V_CMD_DMA_BASEADDR_REG, pgt[dma_buf->idx]); 

	S3V_WRITE(S3V_CMD_DMA_WRITEP_REG, (0x0000 << 2));
	S3V_WRITE(S3V_CMD_DMA_READP_REG, (0x0000 << 2));

	/* FIXME: next one seems to be a must on DX (to avoid lockups) */
	/* I still could not determine if this help on MX too */
	/* Anyone with GX? */
#ifdef _VIRGEDX
	S3V_WRITE(S3V_CMD_DMA_ENABLE_REG, 0x1);
#endif

	S3V_FIFOSPACE(1);
        S3V_WRITE( S3V_CMD_DMA_WRITEP_REG,
                   (dma_buf->used)
                   | S3V_CMD_DMA_WRITEP_UPDATE );

	up(&s3v_buf_sem); 
}

inline int s3v_dma_is_ready(drm_device_t *dev)
{
	drm_s3v_private_t *dev_priv =
		(drm_s3v_private_t *)dev->dev_private;

	int wp, rp;

	udelay(S3V_UDELAY);

	wp = (S3V_READ(S3V_CMD_DMA_WRITEP_REG) & 0xFFFC) >> 2;
	rp = (S3V_READ(S3V_CMD_DMA_READP_REG) & 0xFFFC) >> 2;

	/* rmb(); */

	return !(wp-rp);	/* !(0) == 1 == TRUE: is ready */
}

static int s3v_dma_send_buffers(drm_device_t *dev, drm_dma_t *d)
{
	drm_buf_t        *buf = NULL;
	int              idx;
	drm_device_dma_t *dma = dev->dma;

	S3V_DEBUG("got %d buf of %i size\n", d->send_count, d->send_sizes[0]);

#if 0
	for (i = 0; i < d->send_count; i++) {
                idx = d->send_indices[i];
	}
#else
	idx = d->send_indices[0];
#endif
	S3V_DEBUG("idx = %i\n", idx);

	buf = dma->buflist[ idx ];
	buf->used = d->send_sizes[0];

	S3V_DEBUG("buf @ %p\n", buf->address);

	while (!(s3v_dma_is_ready(dev))) {
	/* a buf is already being processed by gfx card. It could be:
	 * [a] a buffer of this context (dma->this_buffer)
	 * [b] a buffer of another context (!dma->this_buffer)
	 */
		dma->next_buffer = buf;
	
		if (!_check) {
		/* _check is global. If set our timeout task is
		 * already running. We do not need to run it twice
		 * mmm ... use tasklets?
		 */
			_check++;
			queue_task(&s3v_dma_task, &tq_timer);
		}

		interruptible_sleep_on(&dma->waiting);
	}

	if (dma->this_buffer) {
		dma->next_buffer=NULL;
	/* s3v_lock_free(dev,&dev->lock.hw_lock->lock,DRM_KERNEL_CONTEXT); */
		up(&s3v_gfx_sem);
	}

	/* needed? */
        if(!buf) {
		printk(KERN_ERR "send: !buf\n");
		return 0;
	}

	dma->this_buffer=buf;
	dma->this_buffer->pending = 1;

	down_interruptible(&s3v_gfx_sem);
	/* s3v_lock_take(&dev->lock.hw_lock->lock, DRM_KERNEL_CONTEXT); */
	s3v_dma_dispatch(dev, dma->this_buffer);

	return 0;
}

int s3v_dma(struct inode *inode, struct file *filp, unsigned int cmd,
	      unsigned long arg)
{
	drm_file_t	  	 *priv	 = filp->private_data;
	drm_device_t	 *dev	 = priv->dev;
	drm_device_dma_t *dma	 = dev->dma;
	int			 retcode = 0;
	drm_dma_t	  	 d;
	int			 idx;

	if (copy_from_user(&d, (drm_dma_t *)arg, sizeof(d)))
		return -EFAULT;

	if (d.send_count < 0 || d.send_count > dma->buf_count) {
		DRM_ERROR("Process %d trying to send %d buffers (of %d max)\n",
			  current->pid, d.send_count, dma->buf_count);
		return -EINVAL;
	}

	if (d.request_count < 0 || d.request_count > dma->buf_count) {
		DRM_ERROR("Process %d trying to get %d buffers (of %d max)\n",
			  current->pid, d.request_count, dma->buf_count);
		return -EINVAL;
	}

	if (d.send_count) {
		DRM_DEBUG("*** [dma] SENDING ***\n");
		DRM_DEBUG("d.send_count=%i of (dma->buf_count) %i\n",
                d.send_count, dma->buf_count);
		S3V_DEBUG("IDX -d.send_list[0]- =%i; SIZE -d.send_sizes[0]- =%i\n",
                (d.send_indices)[0], (d.send_sizes)[0]);

		if (d.flags & _DRM_DMA_PRIORITY)
			retcode = 0; /* s3v_dma_priority(dev, &d); */
		else
			retcode = s3v_dma_send_buffers(dev, &d);
	}

	d.granted_count = 0;

	if (!retcode && d.request_count) {
		retcode = s3v_dma_get_buffers(dev, &d);
		idx = (d.request_indices)[0];
		S3V_DEBUG("****************\n");
		S3V_DEBUG("* getting #%i  *\n", idx);
		S3V_DEBUG("****************\n");

		_got++;
	}

	DRM_DEBUG("%d returning, granted = %d\n",
		  current->pid, d.granted_count);
		  
	if (copy_to_user((drm_dma_t *)arg, &d, sizeof(d)))
		return -EFAULT;

	return retcode;
}

/* =============================================================
 * DMA initialization, cleanup
 */
int s3v_do_cleanup_dma( drm_device_t *dev )
{
	DRM_DEBUG( "%s\n", __FUNCTION__ );

	if ( dev->dev_private ) {
		drm_s3v_private_t *dev_priv = dev->dev_private;
#if 0
		if (!dev_priv->pcimode)
			DRM_IOREMAPFREE( dev_priv->buffer_map );
#endif
		DRM(free)( dev_priv, sizeof(drm_s3v_private_t),	DRM_MEM_DRIVER );
		dev_priv = NULL;
	}
	
	return 0;
}

static int s3v_do_init_dma( drm_device_t *dev, drm_s3v_init_t *init )
{
	drm_s3v_private_t *dev_priv;
	drm_device_dma_t  *dma = dev->dma;
	drm_buf_t         *buf;
	int i;
	struct list_head *list;
	unsigned int     *pgt;

	DRM_DEBUG( "%s\n", __FUNCTION__ );

	dev_priv = DRM(alloc)( sizeof(drm_s3v_private_t),
                            DRM_MEM_DRIVER );
	if ( !dev_priv )
		return -ENOMEM;

	dev->dev_private = (void *)dev_priv;

	/* FIXME: gather all init code */

	_check = 0;
	init_waitqueue_head(&dma->waiting);
	s3v_dma_task.routine = s3v_dma_check;
	s3v_dma_task.data = (void*) dev;
	sema_init(&s3v_buf_sem, 1);
	sema_init(&s3v_gfx_sem, 1);

	memset( dev_priv, 0, sizeof(drm_s3v_private_t) );

	list_for_each(list, &dev->maplist->head) {
		drm_map_list_t *r_list = (drm_map_list_t *)list;
		if( r_list->map &&
		r_list->map->type == _DRM_SHM &&
		r_list->map->flags & _DRM_CONTAINS_LOCK ) {
			dev_priv->sarea_map = r_list->map;
			break;
		}
	}

	if(!dev_priv->sarea_map) {
		dev->dev_private = (void *)dev_priv;
		s3v_do_cleanup_dma(dev);
		DRM_ERROR("can not find sarea!\n");
		return -EINVAL;
	}
	DRM_DEBUG("SAREA found\n");

	dev_priv->sarea_priv = (drm_s3v_sarea_t *)
		((u8 *)dev_priv->sarea_map->handle +
		init->sarea_priv_offset);

	DRM_FIND_MAP( dev_priv->mmio_map, init->mmio_offset );
	if(!dev_priv->mmio_map) {
		dev->dev_private = (void *)dev_priv;
		s3v_do_cleanup_dma(dev);
		DRM_ERROR("can not find mmio map!\n");
		return -EINVAL;
	}
	DRM_DEBUG("MMIO found\n");

	/* FIXME: ! */
	init->pcimode = 1;
    
	if (init->pcimode) {
		S3V_DEBUG("Card is PCI\n");
		for (i=0; i<S3V_DMA_BUF_NR; i++)
			DRM_DEBUG("buf #%i @%p\n", i, dma->buflist[i]->address);

	        buf = dma->buflist[S3V_DMA_BUF_NR];
	        pgt = buf->address;

	        for (i = 0; i < S3V_DMA_BUF_NR; i++) {
			DRM_DEBUG("virt_to_phys: round #%i of %i: ",
				i, S3V_DMA_BUF_NR);
			
			buf = dma->buflist[i];
			DRM_DEBUG("from (virt) %p ", buf->address);

#ifdef S3V_BUF_4K
			*pgt = ((virt_to_phys((void*)buf->address)) & 0xfffff000);
#else
			*pgt = ((virt_to_phys((void*)buf->address)) & 0xfffff000) |
				S3V_CMD_DMA_BUFFERSIZE_64K;
#endif
/* or simpler: virt_to_phys((void*)buf->address|S3V_CMD_DMA_BUFFERSIZE_64K) */

			DRM_DEBUG("to (phys) 0x%x\n", *pgt);
			pgt++;
        	}

		buf = dma->buflist[S3V_DMA_BUF_NR];
		pgt = buf->address;

		for (i = 0; i < S3V_DMA_BUF_NR; i++)
			S3V_DEBUG("*** NEW *** buf#%i @ 0x%x\n",
				i, *pgt++);

        	buf = dma->buflist[S3V_DMA_BUF_NR];
	} else {
		/* Where in the world is S3Virge AGP ? */
		S3V_DEBUG("Card is AGP\n");
	}

	DRM_DEBUG("init some values\n");

	dev_priv->front_offset = init->front_offset;
	dev_priv->front_width = init->front_width;
	dev_priv->front_height = init->front_height;
	dev_priv->front_pitch = init->front_pitch;  /* stride */

	dev_priv->back_offset = init->back_offset;
	dev_priv->back_width = init->back_width;
	dev_priv->back_height = init->back_height;
	dev_priv->back_pitch = init->back_pitch;

	dev_priv->depth_offset = init->depth_offset;
	dev_priv->depth_width = init->depth_width;
	dev_priv->depth_height = init->depth_height;
	dev_priv->depth_pitch = init->depth_pitch;

/*
	S3V_FIFOSPACE(1);
	S3V_WRITE(0x8504, (0x5 << 5));
*/
	return 0;
}

int s3v_dma_init( struct inode *inode, struct file *filp,
          unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_s3v_init_t init;

	_reset=0;

	if ( copy_from_user( &init, (drm_s3v_init_t *)arg, sizeof(init) ) )
	        return -EFAULT;

	switch ( init.func ) {
	case S3V_INIT_DMA:
	        printk(KERN_ERR "init dma\n");
        	return s3v_do_init_dma( dev, &init );
	case S3V_CLEANUP_DMA:
		printk(KERN_ERR "cleanup dma\n");
		printk(KERN_ERR "_reset = %i\n", _reset);
        	return s3v_do_cleanup_dma( dev );
	}

	return -EINVAL;
}
