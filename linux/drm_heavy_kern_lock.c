/* This has to be used VERY carefully, since you never want to sleep with the
 * lock.  It would be very bad if you slept w/ irq's disabled.
 */

/* Big fscking lock implementation */
#define __NO_VERSION__
#include "drmP.h"
#include <asm/softirq.h>

/* This can NEVER be called from an interrupt */
void drm_schedule(drm_device_t *dev)
{
#if 1
	drm_file_t *filp;
	if(in_interrupt()) {
		sti();
		BUG();
	}

	filp = drm_find_filp_by_current_pid(dev);
	if(filp == NULL) {
	   sti();
	   BUG();
	}
	drm_release_big_fscking_lock(dev, filp);
	schedule();
	drm_reacquire_big_fscking_lock(dev, filp);
#else
	schedule();
#endif
}

void drm_schedule_timeout(drm_device_t *dev, unsigned long timeout)
{
#if 1
	drm_file_t *filp;

	if(in_interrupt()) {
		BUG();
	}

	filp = drm_find_filp_by_current_pid(dev);
	if(filp == NULL) BUG();
	drm_release_big_fscking_lock(dev, filp);
	schedule_timeout(timeout);
	drm_reacquire_big_fscking_lock(dev, filp);
#else
	schedule_timeout(timeout);
#endif
}

void drm_big_fscking_lock(drm_device_t *dev)
{
	drm_file_t *filp;
	unsigned long flags;

	if(in_irq()) {
		barrier();
		if(!++dev->irq_lock_depth) {
#if 1
			spin_lock_irqsave(&dev->big_fscking_lock,
					  flags);
			dev->irq_flags = flags;
#endif
		}
	} else if (in_softirq()) {
		barrier();
		if(!++dev->bh_lock_depth) {
#if 1
			spin_lock_irqsave(&dev->big_fscking_lock,
					  flags);
			dev->bh_flags = flags;
#endif
		}
	} else {
		filp = drm_find_filp_by_current_pid(dev);
		if(filp == NULL) BUG();
		barrier();
		if(!++filp->lock_depth) {
#if 1
			spin_lock_irqsave(&dev->big_fscking_lock, 
					  flags);
			dev->irq_flags = flags;
#endif
		}
	}
}

void drm_big_fscking_unlock(drm_device_t *dev)
{
	drm_file_t *filp;

	if(in_irq()) {
		barrier();
		if(--dev->irq_lock_depth < 0)
#if 1
			spin_unlock_irqrestore(&dev->big_fscking_lock,
					       dev->irq_flags);
#else
			;
#endif
	} else if (in_softirq()) {
		barrier();
		if(--dev->bh_lock_depth < 0)
#if 1
			spin_unlock_irqrestore(&dev->big_fscking_lock,
					       dev->bh_flags);
#else
			;
#endif
	} else {
		filp = drm_find_filp_by_current_pid(dev);
		if(filp == NULL) BUG();
		barrier();
		if(--filp->lock_depth < 0)
#if 1
			spin_unlock_irqrestore(&dev->big_fscking_lock, 
					       filp->irq_flags);
#else
			;
#endif
	}
}

void __inline drm_big_fscking_lock_filp(drm_device_t *dev, drm_file_t *filp)
{
	unsigned long flags;

	barrier();
	if(!++filp->lock_depth) {
#if 1
		spin_lock_irqsave(&dev->big_fscking_lock, 
				  flags);
		dev->irq_flags = flags;
#endif
	}
}

void __inline drm_big_fscking_unlock_filp(drm_device_t *dev, drm_file_t *filp)
{
		barrier();
		if(--filp->lock_depth < 0)
#if 1
			spin_unlock_irqrestore(&dev->big_fscking_lock, 
					       filp->irq_flags);
#else
			;
#endif
}
