/**************************************************************************
 *
 * Copyright (c) 2007-2008 Tungsten Graphics, Inc., Cedar Park, TX., USA
 * All Rights Reserved.
 * Copyright (c) 2009 VMware, Inc., Palo Alto, CA., USA
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
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
 **************************************************************************/
/*
 * Authors: Thomas Hellström <thomas-at-tungstengraphics-dot-com>
 */

#include "ttm/ttm_lock.h"
#include <asm/atomic.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/sched.h>

void ttm_lock_init(struct ttm_lock *lock)
{
	init_waitqueue_head(&lock->queue);
	atomic_set(&lock->write_lock_pending, 0);
	atomic_set(&lock->readers, 0);
	lock->kill_takers = false;
	lock->signal = SIGKILL;
}

void ttm_read_unlock(struct ttm_lock *lock)
{
	if (atomic_dec_and_test(&lock->readers))
		wake_up_all(&lock->queue);
}

int ttm_read_lock(struct ttm_lock *lock, bool interruptible)
{
	while (unlikely(atomic_read(&lock->write_lock_pending) != 0)) {
		int ret;

		if (!interruptible) {
			wait_event(lock->queue,
				   atomic_read(&lock->write_lock_pending) == 0);
			continue;
		}
		ret = wait_event_interruptible
		    (lock->queue, atomic_read(&lock->write_lock_pending) == 0);
		if (ret)
			return -ERESTART;
	}

	while (unlikely(!atomic_add_unless(&lock->readers, 1, -1))) {
		int ret;
		if (!interruptible) {
			wait_event(lock->queue,
				   atomic_read(&lock->readers) != -1);
			continue;
		}
		ret = wait_event_interruptible
		    (lock->queue, atomic_read(&lock->readers) != -1);
		if (ret)
			return -ERESTART;
	}

	if (unlikely(lock->kill_takers)) {
		send_sig(lock->signal, current, 0);
		ttm_read_unlock(lock);
		return -ERESTART;
	}

	return 0;
}

static int __ttm_write_unlock(struct ttm_lock *lock)
{
	if (unlikely(atomic_cmpxchg(&lock->readers, -1, 0) != -1))
		return -EINVAL;
	wake_up_all(&lock->queue);
	return 0;
}

static void ttm_write_lock_remove(struct ttm_base_object **p_base)
{
	struct ttm_base_object *base = *p_base;
	struct ttm_lock *lock = container_of(base, struct ttm_lock, base);
	int ret;

	*p_base = NULL;
	ret = __ttm_write_unlock(lock);
	BUG_ON(ret != 0);
}

int ttm_write_lock(struct ttm_lock *lock,
		   bool interruptible,
		   struct ttm_object_file *tfile)
{
	int ret = 0;

	atomic_inc(&lock->write_lock_pending);

	while (unlikely(atomic_cmpxchg(&lock->readers, 0, -1) != 0)) {
		if (!interruptible) {
			wait_event(lock->queue,
				   atomic_read(&lock->readers) == 0);
			continue;
		}
		ret = wait_event_interruptible
		    (lock->queue, atomic_read(&lock->readers) == 0);

		if (ret) {
			if (atomic_dec_and_test(&lock->write_lock_pending))
				wake_up_all(&lock->queue);
			return -ERESTART;
		}
	}

	if (atomic_dec_and_test(&lock->write_lock_pending))
		wake_up_all(&lock->queue);

	if (unlikely(lock->kill_takers)) {
		send_sig(lock->signal, current, 0);
		__ttm_write_unlock(lock);
		return -ERESTART;
	}

	/*
	 * Add a base-object, the destructor of which will
	 * make sure the lock is released if the client dies
	 * while holding it.
	 */

	ret = ttm_base_object_init(tfile, &lock->base, false,
				   ttm_lock_type, &ttm_write_lock_remove, NULL);
	if (ret)
		(void)__ttm_write_unlock(lock);

	return ret;
}

int ttm_write_unlock(struct ttm_lock *lock, struct ttm_object_file *tfile)
{
	return ttm_ref_object_base_unref(tfile,
					 lock->base.hash.key, TTM_REF_USAGE);
}
