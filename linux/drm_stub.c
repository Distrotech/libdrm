/**
 * \file drm_stub.c
 * Stub support.
 *
 * The functions in this file provide the support for the attribution of 
 * minor device numbers in the presence of multiple (distinct) DRM modules.
 * It also registers the proc filesystem for each minor.
 * 
 * \author Rickard E. (Rik) Faith <faith@valinux.com>
 */

/*
 * Copyright 2001 VA Linux Systems, Inc., Sunnyvale, California.
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
 */

#define __NO_VERSION__
#include "drmP.h"

#define DRM_STUB_MAXCARDS 16	/* Enough for one machine */

/** 
 * Stub list. 
 *
 * One for each minor. 
 */
static struct drm_stub_list {
	const char             *name;		/**< driver name */
	struct file_operations *fops;		/**< file operations */
	struct proc_dir_entry  *dev_root;	/**< proc directory entry */
} *drm_stub_list;

static struct proc_dir_entry *drm_stub_root;

/** 
 * Stub information. 
 *
 * This structure holds the callbacks for stub (un)registration is made
 * available for other DRM modules as a registered symbol named "drm".
 */
static struct drm_stub_info {
	int (*info_register)(const char *name, struct file_operations *fops,
			     drm_device_t *dev);
	int (*info_unregister)(int minor);
} drm_stub_info;

/**
 * File \c open operation.
 *
 * \param inode device inode.
 * \param filp file pointer.
 *
 * Puts the drm_stub_list::fops corresponding to the device minor number into
 * \p filp, call the \c open method, and restore the file operations.
 */
static int drm_stub_open(struct inode *inode, struct file *filp)
{
	int                    minor = minor(inode->i_rdev);
	int                    err   = -ENODEV;
	struct file_operations *old_fops;

	if (!drm_stub_list || !drm_stub_list[minor].fops) return -ENODEV;
	old_fops   = filp->f_op;
	filp->f_op = fops_get(drm_stub_list[minor].fops);
	if (filp->f_op->open && (err = filp->f_op->open(inode, filp))) {
		fops_put(filp->f_op);
		filp->f_op = fops_get(old_fops);
	}
	fops_put(old_fops);

	return err;
}

/** File operations structure */
static struct file_operations drm_stub_fops = {
	.owner = THIS_MODULE,
	.open  = drm_stub_open
};

/**
 * Get a device minor number.
 *
 * \param name driver name.
 * \param fops file operations.
 * \param dev DRM device.
 * \return minor number on success, or a negative number on failure.
 *
 * Allocate and initialize ::stub_list if one doesn't exist already.  Search an
 * empty entry and initialize it to the given parameters, and create the proc
 * init entry via proc_init().
 */
static int drm_stub_getminor(const char *name, struct file_operations *fops,
			      drm_device_t *dev)
{
	int i;

	if (!drm_stub_list) {
		drm_stub_list = drm_alloc(sizeof(*drm_stub_list)
					    * DRM_STUB_MAXCARDS);
		if(!drm_stub_list) return -1;
		for (i = 0; i < DRM_STUB_MAXCARDS; i++) {
			drm_stub_list[i].name = NULL;
			drm_stub_list[i].fops = NULL;
		}
	}
	for (i = 0; i < DRM_STUB_MAXCARDS; i++) {
		if (!drm_stub_list[i].fops) {
			drm_stub_list[i].name = name;
			drm_stub_list[i].fops = fops;
			drm_stub_root = drm_proc_init(dev, i, drm_stub_root,
							&drm_stub_list[i]
							.dev_root);
			return i;
		}
	}
	return -1;
}

/**
 * Put a device minor number.
 *
 * \param minor minor number.
 * \return always zero.
 *
 * Cleans up the proc resources. If a minor is zero then release the foreign
 * "drm" data, otherwise unregisters the "drm" data, frees the stub list and
 * unregisters the character device. 
 */
static int drm_stub_putminor(int minor)
{
	if (minor < 0 || minor >= DRM_STUB_MAXCARDS) return -1;
	drm_stub_list[minor].name = NULL;
	drm_stub_list[minor].fops = NULL;
	drm_proc_cleanup(minor, drm_stub_root,
			  drm_stub_list[minor].dev_root);
	if (minor) {
		inter_module_put("drm");
	} else {
		inter_module_unregister("drm");
		drm_free(drm_stub_list);
		unregister_chrdev(DRM_MAJOR, "drm");
	}
	return 0;
}

/**
 * Register.
 *
 * \param name driver name.
 * \param fops file operations
 * \param dev DRM device.
 * \return zero on success or a negative number on failure.
 *
 * Attempt to register the char device and get the foreign "drm" data. If
 * successful then another module already registered so gets the stub info,
 * otherwise use this module stub info and make it available for other modules.
 *
 * Finally calls stub_info::info_register.
 */
int drm_stub_register(const char *name, struct file_operations *fops,
		       drm_device_t *dev)
{
	struct drm_stub_info *i = NULL;

	DRM_DEBUG("\n");
	if (register_chrdev(DRM_MAJOR, "drm", &drm_stub_fops))
		i = (struct drm_stub_info *)inter_module_get("drm");

	if (i) {
				/* Already registered */
		drm_stub_info.info_register   = i->info_register;
		drm_stub_info.info_unregister = i->info_unregister;
		DRM_DEBUG("already registered\n");
	} else if (drm_stub_info.info_register != drm_stub_getminor) {
		drm_stub_info.info_register   = drm_stub_getminor;
		drm_stub_info.info_unregister = drm_stub_putminor;
		DRM_DEBUG("calling inter_module_register\n");
		inter_module_register("drm", THIS_MODULE, &drm_stub_info);
	}
	if (drm_stub_info.info_register)
		return drm_stub_info.info_register(name, fops, dev);
	return -1;
}

/**
 * Unregister.
 *
 * \param minor
 *
 * Calls drm_stub_info::unregister.
 */
int drm_stub_unregister(int minor)
{
	DRM_DEBUG("%d\n", minor);
	if (drm_stub_info.info_unregister)
		return drm_stub_info.info_unregister(minor);
	return -1;
}
