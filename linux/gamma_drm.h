typedef struct {
	unsigned int	GDeltaMode;
	unsigned int	GDepthMode;
	unsigned int	GGeometryMode;
	unsigned int	GTransformMode;
} drm_gamma_context_regs_t;

typedef struct _drm_gamma_sarea {
   	drm_gamma_context_regs_t context_state;

	unsigned int dirty;

	int ctxOwner;
} drm_gamma_sarea_t;

typedef struct drm_gamma_init {
   	enum {
	   	GAMMA_INIT_DMA    = 0x01,
	       	GAMMA_CLEANUP_DMA = 0x02
	} func;

   	int sarea_priv_offset;

#if 0
	int chipset;
   	int sgram;

	unsigned int maccess;

   	unsigned int fb_cpp;
	unsigned int front_offset, front_pitch;
   	unsigned int back_offset, back_pitch;

   	unsigned int depth_cpp;
   	unsigned int depth_offset, depth_pitch;

   	unsigned int texture_offset[MGA_NR_TEX_HEAPS];
   	unsigned int texture_size[MGA_NR_TEX_HEAPS];

	unsigned int fb_offset;
	unsigned int mmio_offset;
	unsigned int status_offset;
	unsigned int warp_offset;
	unsigned int primary_offset;
#endif
	unsigned int buffers_offset;
} drm_gamma_init_t;

