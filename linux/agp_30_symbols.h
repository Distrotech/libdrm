#if defined(__linux__)
#include <linux/config.h>
#include <linux/types.h>
#include <linux/agp_backend.h>
#include <linux/list.h>

#ifndef _AGP_EXTENDED_DEFINES
/* okay cheat, so we can compile. */
typedef struct {
	int			(*get_num_contexts)(void);
	struct list_head	*(*get_info_list)(void);

	void       		(*free_memory)(int, agp_memory *);
	agp_memory 		*(*allocate_memory)(int, size_t, u32);
	int        		(*bind_memory)(int, agp_memory *, off_t);
	int        		(*unbind_memory)(int, agp_memory *);
	void       		(*enable)(int, u32);
	int        		(*acquire)(int);
	void       		(*release)(int);
	int        		(*copy_info)(int, agp_kern_info *);
	int			(*get_map)(int, int, agp_memory **);
	void			*(*kmap)(int, agp_memory *, unsigned long,
					 unsigned long);
	int			(*vma_map_memory)(int, struct vm_area_struct *,
						  agp_memory *, unsigned long);
	unsigned long		(*usermap)(int, struct file *, agp_memory *,
					   unsigned long, unsigned long,
					   unsigned long, unsigned long);
} drm_agp_3_0_t;

/* No longer report the chipset_type enum, pass a char *driver_name back
 * instead since it would be more useful.
 */
typedef struct _agp_info_master {
	struct list_head masters;
	u8 agp_major_version;
	u8 agp_minor_version;
	u8 cap_ptr;
	struct pci_dev *device;
	int num_requests_enqueue;
	int calibration_cycle_ms;	/* Agp 3.0 field */

	/* Current isochronous information : Ignore if 
	 * AGP_SUPPORTS_ISOCHRONOUS is not set in the flags field
	 */
	int max_bandwidth_bpp;		/* Max Bandwidth in bytes per period */
	int num_trans_per_period;
	int max_requests;
	int payload_size;

	u32 flags;
} agp_info_master;

typedef struct _agp_info_target {
	u8 agp_major_version;
	u8 agp_minor_version;
	u8 cap_ptr;
	struct pci_dev *device;
	int num_requests_enqueue;
	int optimum_request_size;	/* Agp 3.0 field */
	int calibration_cycle_ms;	/* Agp 3.0 field */

	/* Current isochronous information : Ignore if 
	 * AGP_SUPPORTS_ISOCHRONOUS is not set in the flags field
	 */
	int max_bandwidth_bpp;		/* Max Bandwidth in bytes per period */
	int iso_latency_in_periods;
	int num_trans_per_period;
	int payload_size;

	u32 flags;
} agp_info_target;

typedef struct _agp_extended_info {
	struct list_head info_list;

	/* My context id */
	int agp_ctxt_idx;

	/* Information about the target for this aperture. */
	agp_info_target *bridge;

	/* Information about all the masters using this target. */
	struct list_head master_list;
	int num_masters;

	/* Agp driver information. */
	char *driver_name;
	u32 driver_flags;

	/* Where this targets agp aperture is located and how big it is. */
	off_t aper_base;
	size_t aper_size;

	/* Size information and mask - agp page. */
	int agp_page_shift;
	unsigned long agp_page_mask;

	/* Size information and mask - alloc page. */
	int alloc_page_shift;
	unsigned long alloc_page_mask;

	/* Memory limit information */
	int max_system_pages;	/* The maximum number of alloc pages */
	int current_memory;	/* The current number of alloc pages */
} agp_extended_info;

#endif
#endif
