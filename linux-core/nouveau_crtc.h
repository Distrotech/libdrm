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

#ifndef __NOUVEAU_CRTC_H__
#define __NOUVEAU_CRTC_H__

struct nouveau_crtc {
	struct drm_crtc base;

	int index;

	struct drm_mode_set mode_set;

	struct drm_display_mode *mode;
	bool use_dithering;

	struct {
		uint32_t offset;
	} fb;

	struct {
		struct drm_gem_object *gem;
		bool visible;
		uint32_t offset;
		void (*set_offset)(struct nouveau_crtc *, uint32_t offset);
		void (*set_pos)(struct nouveau_crtc *, int x, int y);
		void (*hide)(struct nouveau_crtc *, bool update);
		void (*show)(struct nouveau_crtc *, bool update);
	} cursor;

	struct {
		struct mem_block *mem;
		uint16_t r[256];
		uint16_t g[256];
		uint16_t b[256];
		int depth;
	} lut;

	int (*set_dither) (struct nouveau_crtc *crtc, bool update);
	int (*set_scale) (struct nouveau_crtc *crtc, int mode, bool update);
	int (*set_clock) (struct nouveau_crtc *crtc, struct drm_display_mode *);
	int (*set_clock_mode) (struct nouveau_crtc *crtc);
	int (*destroy) (struct nouveau_crtc *crtc);
};
#define to_nouveau_crtc(x) container_of((x), struct nouveau_crtc, base)

int nv50_crtc_create(struct drm_device *dev, int index);
int nv50_cursor_init(struct nouveau_crtc *);
void nv50_cursor_fini(struct nouveau_crtc *);

#endif /* __NOUVEAU_CRTC_H__ */
