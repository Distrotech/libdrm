/*
 * Author: Max Lingua <sunmax@libero.it>
 */

#ifndef _S3V_DRM_H
#define _S3V_DRM_H

/* WARNING: These defines must be the same as what the Xserver uses.
 * if you change them, you must change the defines in the Xserver.
 */

#ifndef _S3V_DEFINES
#define _S3V_DEFINES

/* #define S3V_BUF_4K 1 */

#ifdef S3V_BUF_4K
	#define S3V_DMA_BUF_ORDER 12
	#define S3V_DMA_BUF_NR    256
#else
	#define S3V_DMA_BUF_ORDER 16 /* -much- better */
	#define S3V_DMA_BUF_NR    16
#endif
/* on s3virge you can only choose between *
 * 4k (2^12) and 64k (2^16) dma bufs      */
#define S3V_DMA_BUF_SZ        (1<<S3V_DMA_BUF_ORDER)

#define S3V_NR_SAREA_CLIPRECTS 8

/* Each region is a minimum of 16k (64*64@4bpp)
 * and there are at most 40 of them.
 */
#define S3V_NR_TEX_REGIONS 64 /* was 40 */
#define S3V_LOG_TEX_GRANULARITY 16 /* was 4 */
/* 40 * (2 ^ 4) = 640k, that's all we have for tex on 4mb gfx card */
/* FIXME: will it work with card with less than 4mb? */
/* FIXME: we should set this at run time */

#endif	/* _S3V_DEFINES */

/* FIXME: all of the following have to be checked: do we need them? */

#define S3V_UPLOAD_TEX0IMAGE  0x1 /* handled clientside */
#define S3V_UPLOAD_TEX1IMAGE  0x2 /* handled clientside */
#define S3V_UPLOAD_CTX        0x4
#define S3V_UPLOAD_BUFFERS    0x8
#define S3V_UPLOAD_TEX0       0x10
#define S3V_UPLOAD_TEX1       0x20
#define S3V_UPLOAD_CLIPRECTS  0x40

#define S3V_FRONT   0x1
#define S3V_BACK    0x2
#define S3V_DEPTH   0x4

/* s3v specific ioctls */
#define DRM_IOCTL_S3V_INIT      	DRM_IOW( 0x40, drm_s3v_init_t)
#define DRM_IOCTL_S3V_SIMPLE_LOCK   	DRM_IO(  0x4a)
#define DRM_IOCTL_S3V_SIMPLE_FLUSH_LOCK DRM_IO(  0x4b)
#define DRM_IOCTL_S3V_SIMPLE_UNLOCK 	DRM_IO(  0x4c)
#define DRM_IOCTL_S3V_RESET     	DRM_IO(  0x41)
#define DRM_IOCTL_S3V_STATUS        	DRM_IO(  0x42)
/*
#define DRM_IOCTL_S3V_COPY     		DRM_IOW( 0x4d, drm_s3v_copy_t)
*/

typedef struct _drm_s3v_init {
	enum {
		S3V_INIT_DMA = 0x01,
		S3V_CLEANUP_DMA = 0x02
	} func;

	unsigned int pcimode;	/* bool: 1=pci 0=agp */

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
	
	unsigned int texture_offset;
} drm_s3v_init_t;

/* Warning: If you change the SAREA structure you must change the Xserver
 * structure as well */

typedef struct _drm_s3v_tex_region {
	unsigned char next, prev; 	/* indices to form a circular LRU  */
	unsigned char in_use;		/* owned by a client, or free? */
	int age;			/* tracked by clients to update local LRU's */
} drm_s3v_tex_region_t;

typedef struct _drm_s3v_sarea {

   	unsigned int dirty;

	unsigned int nbox;
	drm_clip_rect_t boxes[S3V_NR_SAREA_CLIPRECTS];

	/* Maintain an LRU of contiguous regions of texture space.  If
	 * you think you own a region of texture memory, and it has an
	 * age different to the one you set, then you are mistaken and
	 * it has been stolen by another client.  If global texAge
	 * hasn't changed, there is no need to walk the list.
	 *
	 * These regions can be used as a proxy for the fine-grained
	 * texture information of other clients - by maintaining them
	 * in the same lru which is used to age their own textures,
	 * clients have an approximate lru for the whole of global
	 * texture space, and can make informed decisions as to which
	 * areas to kick out.  There is no need to choose whether to
	 * kick out your own texture or someone else's - simply eject
	 * them all in LRU order.  
	 */
   
	drm_s3v_tex_region_t texList[S3V_NR_TEX_REGIONS+1]; /* Last elt is sentinal */

	int texAge;		/* last time texture was uploaded */
	int last_enqueue;	/* last time a buffer was enqueued */
	int last_dispatch;	/* age of the most recently dispatched buffer */
	int last_quiescent;	/*  */
	int ctxOwner;		/* last context to upload state */

	int vertex_prim;

} drm_s3v_sarea_t;

typedef struct _drm_s3v_clear {
	int clear_color;
	int clear_depth;
	int flags;
} drm_s3v_clear_t;

/* These may be placeholders if we have more cliprects than
 * S3V_NR_SAREA_CLIPRECTS.  In that case, the client sets discard to
 * false, indicating that the buffer will be dispatched again with a
 * new set of cliprects.
 */
typedef struct _drm_s3v_vertex {
   	int idx;	/* buffer index */
	int used;	/* nr bytes in use */
	int discard;	/* client is finished with the buffer? */
} drm_s3v_vertex_t;

typedef struct _drm_s3v_copy {
   	int idx;	/* buffer index */
	int used;	/* nr bytes in use */
	void *address;	/* Address to copy from */
} drm_s3v_copy_t;

typedef struct _drm_s3v_dma {
	void *virtual;
	int request_idx;
	int request_size;
	int granted;
} drm_s3v_dma_t;

#endif /* _S3V_DRM_H */
