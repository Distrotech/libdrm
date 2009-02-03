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

#ifndef __NV50_KMS_WRAPPER_H__
#define __NV50_KMS_WRAPPER_H__

#include "drmP.h"
#include "drm.h"

#include "nv50_display.h"
#include "nv50_crtc.h"
#include "nv50_cursor.h"
#include "nv50_lut.h"
#include "nv50_fb.h"
#include "nv50_output.h"
#include "nv50_connector.h"

#include "drm_crtc.h"
#include "drm_edid.h"

#define to_nv50_crtc(x) container_of((x), struct nv50_crtc, base)
#define to_nv50_output(x) container_of((x), struct nv50_output, base)
#define to_nv50_connector(x) container_of((x), struct nv50_connector, base)

void nv50_kms_connector_detect_all(struct drm_device *dev);
bool nv50_kms_connector_get_digital(struct drm_connector *drm_connector);

int nv50_kms_init(struct drm_device *dev);
int nv50_kms_destroy(struct drm_device *dev);

struct drm_framebuffer *
nv50_framebuffer_create(struct drm_device *, struct drm_gem_object *,
			struct drm_mode_fb_cmd *);

#endif /* __NV50_KMS_WRAPPER_H__ */
