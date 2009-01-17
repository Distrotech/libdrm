/**************************************************************************
 *
 * Copyright (c) 2009 VMware, Inc., Palo Alto, CA., USA,
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
 *
 * The purpose of having device private context structures is to
 * be able to have pre-allocated per-context reloc-, valicate_buffer-, and
 * pci- buffer request in order to parallelize most of the execbuf handling.
 * Also we have a possibility to check for invalid context use.
 *
 * We refcount the via_cpriv struct since it's entirely possible that
 * via_context_dtor is called while execbuf is using the context.
 */

#include "drmP.h"
#include "via_drv.h"

static void via_context_destroy(struct kref *kref)
{
	struct via_cpriv *cpriv = container_of(kref, struct via_cpriv, kref);

	if (cpriv->reloc_buf)
		drm_free(cpriv->reloc_buf, VIA_RELOC_BUF_SIZE, DRM_MEM_DRIVER);
	if (cpriv->val_bufs)
		vfree(cpriv->val_bufs);

	drm_free(cpriv, sizeof(*cpriv), DRM_MEM_DRIVER);
}

/*
 * Note! odd error reporting.
 */

int via_context_ctor(struct drm_device *dev, int context)
{
	struct drm_via_private *dev_priv = via_priv(dev);
	struct via_cpriv *cpriv;
	int ret;

	cpriv = drm_calloc(1, sizeof(*cpriv), DRM_MEM_DRIVER);
	if (unlikely(cpriv == NULL))
		return 0;

	cpriv->hash.key = context;
	atomic_set(&cpriv->in_execbuf, -1);
	kref_init(&cpriv->kref);
	cpriv->reloc_buf = drm_alloc(VIA_RELOC_BUF_SIZE, DRM_MEM_DRIVER);
	if (unlikely(cpriv->reloc_buf == NULL))
		goto out_err0;

	write_lock(&dev_priv->context_lock);
	ret = drm_ht_insert_item(&dev_priv->context_hash, &cpriv->hash);
	write_unlock(&dev_priv->context_lock);

	if (unlikely(ret != 0))
		goto out_err1;

	return 1;
      out_err1:
	drm_free(cpriv->reloc_buf, VIA_RELOC_BUF_SIZE, DRM_MEM_DRIVER);
      out_err0:
	drm_free(cpriv, sizeof(*cpriv), DRM_MEM_DRIVER);
	return 0;
}

int via_context_dtor(struct drm_device *dev, int context)
{
	struct drm_via_private *dev_priv = via_priv(dev);
	struct drm_hash_item *hash;
	struct via_cpriv *cpriv = NULL;
	int ret;

	via_release_futex(dev_priv, context);

	write_lock(&dev_priv->context_lock);
	ret = drm_ht_find_item(&dev_priv->context_hash, context, &hash);

	if (ret == 0) {
		(void)drm_ht_remove_item(&dev_priv->context_hash, hash);
		cpriv = drm_hash_entry(hash, struct via_cpriv, hash);
	}
	write_unlock(&dev_priv->context_lock);

	BUG_ON(ret != 0);

	if (likely(cpriv != NULL))
		kref_put(&cpriv->kref, via_context_destroy);

	return 0;
}

struct via_cpriv *via_context_lookup(struct drm_via_private *dev_priv,
				     int context)
{
	struct drm_hash_item *hash;
	struct via_cpriv *cpriv = NULL;
	int ret;

	read_lock(&dev_priv->context_lock);
	ret = drm_ht_find_item(&dev_priv->context_hash, context, &hash);
	if (likely(ret == 0)) {
		cpriv = drm_hash_entry(hash, struct via_cpriv, hash);
		kref_get(&cpriv->kref);
	}
	read_unlock(&dev_priv->context_lock);

	return cpriv;
}

void via_context_unref(struct via_cpriv **p_cpriv)
{
	struct via_cpriv *cpriv = *p_cpriv;
	*p_cpriv = NULL;
	kref_put(&cpriv->kref, via_context_destroy);
}
