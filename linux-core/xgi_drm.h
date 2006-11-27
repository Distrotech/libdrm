
#ifndef _xgi_drm_public_h_
#define _xgi_drm_public_h_

/* XGI specific ioctls */
#define NOT_USED_0_3
#define DRM_XGI_FB_ALLOC	0x04
#define DRM_XGI_FB_FREE	        0x05
#define NOT_USED_6_12
#define DRM_XGI_AGP_INIT	0x13
#define DRM_XGI_AGP_ALLOC	0x14
#define DRM_XGI_AGP_FREE	0x15
#define DRM_XGI_FB_INIT	        0x16

#define XGI_IOCTL_FB_ALLOC		DRM_IOWR(DRM_COMMAND_BASE + DRM_XGI_FB_ALLOC, drm_xgi_mem_t)
#define XGI_IOCTL_FB_FREE		DRM_IOW( DRM_COMMAND_BASE + DRM_XGI_FB_FREE, drm_xgi_mem_t)
#define XGI_IOCTL_AGP_INIT		DRM_IOWR(DRM_COMMAND_BASE + DRM_XGI_AGP_INIT, drm_xgi_agp_t)
#define XGI_IOCTL_AGP_ALLOC		DRM_IOWR(DRM_COMMAND_BASE + DRM_XGI_AGP_ALLOC, drm_xgi_mem_t)
#define XGI_IOCTL_AGP_FREE		DRM_IOW( DRM_COMMAND_BASE + DRM_XGI_AGP_FREE, drm_xgi_mem_t)
#define XGI_IOCTL_FB_INIT		DRM_IOW( DRM_COMMAND_BASE + DRM_XGI_FB_INIT , drm_xgi_fb_t)


typedef struct {
  int context;
  unsigned int offset;
  unsigned int size;
  unsigned long free;
} drm_xgi_mem_t;

typedef struct {
  unsigned int offset, size;
} drm_xgi_agp_t;

typedef struct {
	unsigned int offset, size;
} drm_xgi_fb_t;

#if 0
typedef struct {
  unsigned int left, right;
} drm_xgi_flip_t;
#endif

#endif
