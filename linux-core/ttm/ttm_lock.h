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
 * Authors: Thomas Hellstrom <thomas-at-tungstengraphics-dot-com>
 */

/** @file ttm_lock.h
 * This file implements a simple replacement for the buffer manager use
 * of the DRM heavyweight hardware lock.
 * The lock is a read-write lock. Taking it in read mode is fast, and
 * intended for in-kernel use only.
 * Taking it in write mode is slow.
 *
 * The write mode is used only when there is a need to block all
 * user-space processes from validating buffers.
 * It's allowed to leave kernel space with the write lock held.
 * If a user-space process dies while having the write-lock,
 * it will be released during the file descriptor release.
 *
 * The read lock is typically placed at the start of an IOCTL- or
 * user-space callable function that may end up allocating a memory area.
 * This includes setstatus, super-ioctls and faults; the latter may move
 * unmappable regions to mappable. It's a bug to leave kernel space with the
 * read lock held.
 *
 * Both read- and write lock taking is interruptible for low signal-delivery
 * latency. The locking functions will return -ERESTART if interrupted by a
 * signal.
 *
 * Locking order: The lock should be taken BEFORE any TTM mutexes
 * or spinlocks.
 *
 * Typical usages:
 * a) VT-switching, when we want to clean VRAM and perhaps AGP. The lock
 * stops it from being repopulated.
 * b) out-of-VRAM or out-of-aperture space, in which case the process
 * receiving the out-of-space notification may take the lock in write mode
 * and evict all buffers prior to start validating its own buffers.
 */

#ifndef _TTM_LOCK_H_
#define _TTM_LOCK_H_

#include "ttm_object.h"
#include <linux/wait.h>
#include <asm/atomic.h>

/**
 * struct ttm_lock
 *
 * @base: ttm base object used solely to release the lock if the client
 * holding the lock dies.
 * @queue: Queue for processes waiting for lock change-of-status.
 * @write_lock_pending: Flag indicating that a write-lock is pending. Avoids
 * write lock starvation.
 * @readers: The lock status: A negative number indicates that a write lock is
 * held. Positive values indicate number of concurrent readers.
 */

struct ttm_lock {
	struct ttm_base_object base;
	wait_queue_head_t queue;
	atomic_t write_lock_pending;
	atomic_t readers;
	bool kill_takers;
	int signal;
};

/**
 * ttm_lock_init
 *
 * @lock: Pointer to a struct ttm_lock
 * Initializes the lock.
 */
extern void ttm_lock_init(struct ttm_lock *lock);

/**
 * ttm_read_unlock
 *
 * @lock: Pointer to a struct ttm_lock
 *
 * Releases a read lock.
 */

extern void ttm_read_unlock(struct ttm_lock *lock);

/**
 * ttm_read_unlock
 *
 * @lock: Pointer to a struct ttm_lock
 * @interruptible: Interruptible sleeping while waiting for a lock.
 *
 * Takes the lock in read mode.
 * Returns:
 * -ERESTART If interrupted by a signal and interruptible is true.
 */

extern int ttm_read_lock(struct ttm_lock *lock, bool interruptible);

/**
 * ttm_write_lock
 *
 * @lock: Pointer to a struct ttm_lock
 * @interruptible: Interruptible sleeping while waiting for a lock.
 * @tfile: Pointer to a struct ttm_object_file used to identify the user-space
 * application taking the lock.
 *
 * Takes the lock in write mode.
 * Returns:
 * -ERESTART If interrupted by a signal and interruptible is true.
 * -ENOMEM: Out of memory when locking.
 */
extern int ttm_write_lock(struct ttm_lock *lock, bool interruptible,
			  struct ttm_object_file *tfile);

/**
 * ttm_write_unlock
 *
 * @lock: Pointer to a struct ttm_lock
 * @tfile: Pointer to a struct ttm_object_file used to identify the user-space
 * application taking the lock.
 *
 * Releases a write lock.
 * Returns:
 * -EINVAL If the lock was not held.
 */
extern int ttm_write_unlock(struct ttm_lock *lock,
			    struct ttm_object_file *tfile);

/**
 * ttm_lock_set_kill
 *
 * @lock: Pointer to a struct ttm_lock
 * @val: Boolean whether to kill processes taking the lock.
 * @signal: Signal to send to the process taking the lock.
 *
 * The kill-when-taking-lock functionality is used to kill processes that keep
 * on using the TTM functionality when its resources has been taken down, for
 * example when the X server exits. A typical sequence would look like this:
 * - X server takes lock in write mode.
 * - ttm_lock_set_kill() is called with @val set to true.
 * - As part of X server exit, TTM resources are taken down.
 * - X server releases the lock on file release.
 * - Another dri client wants to render, takes the lock and is killed.
 *
 */

static inline void ttm_lock_set_kill(struct ttm_lock *lock, bool val, int signal)
{
	lock->kill_takers = val;
	if (val)
		lock->signal = signal;
}

#endif
