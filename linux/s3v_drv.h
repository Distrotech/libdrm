/*
 * Author: Max Lingua <sunmax@libero.it>
 */

#ifndef _S3V_DRV_H
#define _S3V_DRV_H

typedef struct _drm_s3v_buf_priv {
   	u32 *in_use;
   	int use_idx;
	int currently_mapped;
	void *virtual;
	void *kernel_virtual;
	int map_count;
   	struct vm_area_struct *vma;
} drm_s3v_buf_priv_t;

typedef struct _drm_s3v_private {
	drm_map_t *sarea_map;
	drm_map_t *buffer_map;
	drm_map_t *mmio_map;

	drm_s3v_sarea_t *sarea_priv;

	unsigned int  pcimode;
   	unsigned long hw_status_page;
   	unsigned long counter;

   	atomic_t flush_done;
   	wait_queue_head_t flush_queue;	/* Processes waiting until flush */
	drm_buf_t *mmap_buffer;

	unsigned int mmio_offset;
	unsigned int buffers_offset;
	unsigned int sarea_priv_offset;

	unsigned int front_offset;
	unsigned int front_width;
	unsigned int front_height;
	unsigned int front_pitch;

	unsigned int back_offset;
	unsigned int back_width;
	unsigned int back_height;
	unsigned int back_pitch;

	unsigned int depth_offset;
	unsigned int depth_width;
	unsigned int depth_height;
	unsigned int depth_pitch;

	u32 front_di1, back_di1, zi1;

	unsigned int usec_timeout;
} drm_s3v_private_t;

			/* s3v_dma.c */
extern int  s3v_dma_init(struct inode *inode, struct file *filp,
            unsigned int cmd, unsigned long arg);
extern int  s3v_dma(struct inode *inode, struct file *filp,
            unsigned int cmd, unsigned long arg);
/* extern int  s3v_dma_schedule(drm_device_t *dev, int locked); */
/* extern void s3v_dma_ready(drm_device_t *dev); */
extern inline int s3v_dma_is_ready(drm_device_t *dev);
extern int  s3v_test(struct inode *inode, struct file *filp,
            unsigned int cmd, unsigned long arg);
extern int  s3v_reset(struct inode *inode, struct file *filp,
            unsigned int cmd, unsigned long arg);
extern int s3v_simple_lock(struct inode *inode, struct file *filp,
            unsigned int cmd, unsigned long arg);
extern int s3v_simple_flush_lock(struct inode *inode, struct file *filp,
            unsigned int cmd, unsigned long arg);
extern int s3v_simple_unlock(struct inode *inode, struct file *filp,
            unsigned int cmd, unsigned long arg);
extern int  s3v_status(struct inode *inode, struct file *filp,
            unsigned int cmd, unsigned long arg);
/*
extern void s3v_dma_quiescent(drm_device_t *dev);
*/

#define LOCK_TEST_WITH_RETURN( dev )                    \
do {                                    \
    if ( !_DRM_LOCK_IS_HELD( dev->lock.hw_lock->lock ) ||       \
         dev->lock.pid != current->pid ) {              \
        DRM_ERROR( "%s called without lock held\n",     \
               __FUNCTION__ );              \
        return -EINVAL;                     \
    }                               \
} while (0)

#define S3V_VERBOSE 		0
#define S3V_TIMEOUT_USEC        1000000

#define S3V_BASE(reg)		((unsigned long) dev_priv->mmio_map->handle)
#define S3V_ADDR(reg)		(S3V_BASE(reg) + reg)
#define S3V_READ(reg)		readl(S3V_ADDR(reg))
#define S3V_WRITE(reg,val) 	do \
				  { writel(val, S3V_ADDR(reg)); wmb(); } \
				while (0)
#define S3V_READ16(reg)		readw(S3V_ADDR(reg))	
#define S3V_WRITE16(reg,val)	do \
				  { writew(val, S3V_ADDR(reg)); wmb(); } \
				while (0)


/* Subsystem control register */
#define S3V_SUB_CTRL_REG            	0x8504	/* reg base */
#define S3V_SUB_CTRL_S3DON          	0x0040	/* offset */

/* Subsystem status register */
#define S3V_SUB_STAT_REG            	0x8504	/* reg base */
#define S3V_SUB_STAT_VSYNC_INT      	0x0001	/* offsets */
#define S3V_SUB_STAT_3D_DONE_INT        0x0002
#define S3V_SUB_STAT_FIFO_OVR_INT       0x0004
#define S3V_SUB_STAT_FIFO_EMPTY_INT     0x0008
#define S3V_SUB_STAT_HDMA_DONE_INT      0x0010
#define S3V_SUB_STAT_CDMA_DONE_INT      0x0020
#define S3V_SUB_STAT_S3D_FIFO_EMPTY_INT 0x0040
#define S3V_SUB_STAT_LPB_INT        	0x0080
#define S3V_SUB_STAT_3DBUSY         	0x0200

#define S3V_FIFOSPACE(n) \
do { \
	udelay(1); \
} while(((S3V_READ(S3V_SUB_STAT_REG) >> 8) & 0x1f) < n)

/* Command DMA buffer stuff */
#define S3V_CMD_DMA_BASEADDR_REG        0x8590	/* reg base */
#define S3V_CMD_DMA_BUFFERSIZE_4K       0x0000	/* offsets */
#define S3V_CMD_DMA_BUFFERSIZE_64K      0x0002
#define S3V_CMD_DMA_WRITEP_REG      	0x8594	/* reg base */
#define S3V_CMD_DMA_READP_REG       	0x8598	/* reg base */
#define S3V_CMD_DMA_RWP_MASK        	0x00FC	/* offsets */
#define S3V_CMD_DMA_WRITEP_UPDATE       0x010000
#define S3V_CMD_DMA_ENABLE_REG      	0x859C	/* reg base */
#define S3V_CMD_DMA_ENABLE          	0x0001	/* offset */

#endif	/* _S3V_DRV_H */
