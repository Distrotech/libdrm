/*
 * Copyright (C) 2008 Maarten Maathuis.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef __NV50_FB_H__
#define __NV50_FB_H__

#include "nv50_display.h"

struct nv50_crtc;

struct nv50_fb {
	struct drm_gem_object *gem;
	int width, height;
	int bpp, depth;
	int pitch;

	int x,y;

	/* function points */
	int (*bind) (struct nv50_crtc *crtc, struct drm_framebuffer *drm_fb,
		     int x, int y);
};

struct nv50_framebuffer {
	struct drm_framebuffer base;
	struct drm_gem_object *gem;
	struct drm_bo_kmap_obj kmap;
};

#define to_nv50_framebuffer(x) container_of((x), struct nv50_framebuffer, base)

int nv50_fb_create(struct nv50_crtc *crtc);
int nv50_fb_destroy(struct nv50_crtc *crtc);

#endif /* __NV50_FB_H__ */
