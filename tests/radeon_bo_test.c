/*
 * Copyright © 2008 Red Hat
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
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Dave Airlie
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include "drm.h"
#include "radeon_drm.h"
#include "radeon_bo_gem.h"

#define OBJECT_SIZE 16384

int main(int argc, char **argv)
{
	int fd;
	struct drm_radeon_gem_create create;
	struct drm_radeon_gem_mmap mmap;
	struct drm_gem_close unref;
	uint8_t expected[OBJECT_SIZE];
	uint8_t buf[OBJECT_SIZE];
	uint8_t *addr;
	int ret;
	int handle;
	struct radeon_bo_manager *bom;
	struct radeon_bo *bo;
	int i;

	fd = drm_open_any();

	bom = radeon_bo_manager_gem_ctor(fd);
	if (!bom) {
		fprintf(stderr,"failed to setup bom\n");
		exit(-2);
	}

	bo = radeon_bo_open(bom, 0, OBJECT_SIZE, 0, 0, 0);
	if (!bo) {
		fprintf(stderr,"failed to open bo\n");
		exit(-2);
	}

	ret = radeon_bo_map(bo, 1);
	if (ret) {
		fprintf(stderr,"failed to map bo\n");
		exit(-2);
	}
	
	addr = bo->ptr;

#if 0
	printf("Testing contents of newly created object.\n");
	memset(expected, 0, sizeof(expected));
	assert(memcmp(addr, expected, sizeof(expected)) == 0);
#endif

	/* fill the buffer with something, then migrate in out */
	memset(expected, 0xaa, sizeof(expected));
	memset(addr, 0xaa, OBJECT_SIZE);

	if (memcmp(addr, expected, sizeof(expected)))
	  fprintf(stderr,"0xaa failed to match up\n");

	for (i = 0; i < 15000; i++) {
	ret = radeon_gem_set_domain(bo, 0, RADEON_GEM_DOMAIN_VRAM);
	if (ret) {
	  fprintf(stderr,"set domain failed\n");
	  break;
	}
	
	if (memcmp(addr, expected, sizeof(expected))) {
	  fprintf(stderr,"0xaa failed to match up -post VRAM move\n");
	  break;
	}

	ret = radeon_gem_set_domain(bo, 0, RADEON_GEM_DOMAIN_GTT);
	if (ret) {
	  fprintf(stderr,"set domain failed\n");
	  break;
	}
	if (memcmp(addr, expected, sizeof(expected))) {
	  fprintf(stderr,"0xaa failed to match up -post GTT move\n");
	  break;
	}
        }
	radeon_bo_unref(bo);

	radeon_bo_manager_gem_dtor(bom);
	close(fd);

	return 0;
}
