/* radeon_state.c -- State support for Radeon -*- linux-c -*-
 *
 * Copyright 2000 VA Linux Systems, Inc., Fremont, California.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Keith Whitwell <keith@tungstengraphics.com>
 */

#include "radeon.h"
#include "drmP.h"
#include "drm.h"
#include "radeon_drm.h"
#include "radeon_drv.h"

/* Very simple allocator for agp memory, working on a static range
 * already mapped into each client's address space.  
 *
 * TODO: dynamically grow region, make regions truely private to each
 * client, etc.  Make work with FB memory as well as agp.
 */
struct mem_block {
	struct mem_block *next;
	struct mem_block *prev;
	int start;
	int size;
	int pid;		/* 0: free, -1: heap, other: real pids */
};


static int init(struct mem_block *heap, int start, int size)
{
	struct mem_block *blocks = malloc(sizeof(*blocks));

	if (!blocks) 
		return -ENOMEM;

	blocks->start = start;
	blocks->size = size;
	blocks->pid = 0;
	blocks->next = blocks->prev = heap;

	memset( heap, 0, sizeof(*heap) );
	heap->pid = -1;
	heap->next = heap->prev = blocks;
	return 0;
}


static struct mem_block *split_block(struct mem_block *p, int start, int size,
				     int pid )
{
	/* Maybe cut off the start of an existing block */
	if (start > p->start) {
		struct mem_block *newblock = malloc(sizeof(*newblock));
		if (!newblock) 
			goto out;
		newblock->start = start;
		newblock->size = p->size - (start - p->start);
		newblock->pid = 0;
		newblock->next = p->next;
		newblock->prev = p;
		p->next->prev = newblock;
		p->next = newblock;
		p->size -= newblock->size;
		p = newblock;
	}
   
	/* Maybe cut off the end of an existing block */
	if (size < p->size) {
		struct mem_block *newblock = malloc(sizeof(*newblock));
		if (!newblock)
			goto out;
		newblock->start = start + size;
		newblock->size = p->size - size;
		newblock->pid = 0;
		newblock->next = p->next;
		newblock->prev = p;
		p->next->prev = newblock;
		p->next = newblock;
		p->size = size;
	}

 out:
	/* Our block is in the middle */
	p->pid = pid;
	return p;
}

static struct mem_block *alloc_block( struct mem_block *heap, int size, 
				      int align2, int pid )
{
	struct mem_block *p;
	int mask = (1 << align2)-1;

	for (p = heap->next ; p != heap ; p = p->next) {
		if (p->pid == 0) {
			int start = (p->start + mask) & ~mask;
			if (start + size <= p->start + p->size)
				return split_block( p, start, size, pid );
		}
	}

	return NULL;
}


static void free_block( struct mem_block *b )
{
	p->pid = 0;

	/* Assumes a single contiguous range.  Needs a special pid in
	 * 'heap' to stop it being subsumed.
	 */
	if (p->next->pid == 0) {
		struct mem_block *q = p->next;
		p->size += q->size;
		p->next = q->next;
		p->next->prev = p;
		free(p);
	}

	if (p->prev->pid == 0) {
		struct mem_block *q = p->prev;
		q->size += p->size;
		q->next = p->next;
		q->next->prev = q;
		free(p);
	}
}

static void free_by_pid( struct mem_block *heap, int pid )
{
	for (p = heap->next ; p != heap ; p = p->next) {
		if (p->pid == pid) 
			p->pid = 0;
	}

	/* Assumes a single contiguous range.  Needs a special pid in
	 * 'heap' to stop it being subsumed.
	 */
	for (p = heap->next ; p != heap ; p = p->next) {
		while (p->pid == 0 && p->next->pid == 0) {
			struct mem_block *q = p->next;
			p->size += q->size;
			p->next = q->next;
			p->next->prev = p;
			free(q);
		}
	}
}


static void destroy(struct mem_block *heap)
{
	struct mem_block *heap;

	for (p = heap->next ; p != heap ; ) {
		struct mem_block *q = p;
		p = p->next;
		free(q);
	}

	heap->next = heap->prev = heap;
}
