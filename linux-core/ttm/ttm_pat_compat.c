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

#include "ttm/ttm_pat_compat.h"
#include <linux/version.h>

#include <linux/spinlock.h>
#include <asm/pgtable.h>

#if (defined(CONFIG_X86) && !defined(CONFIG_X86_PAT))
#include <asm/tlbflush.h>
#include <asm/msr.h>
#include <asm/system.h>
#include <linux/notifier.h>
#include <linux/cpu.h>

#ifndef MSR_IA32_CR_PAT
#define MSR_IA32_CR_PAT 0x0277
#endif

#ifndef _PAGE_PAT
#define _PAGE_PAT 0x080
#endif

static int ttm_has_pat = 0;

/*
 * Used at resume-time when CPU-s are fired up.
 */

static void ttm_pat_ipi_handler(void *notused)
{
	u32 v1, v2;

	rdmsr(MSR_IA32_CR_PAT, v1, v2);
	v2 &= 0xFFFFFFF8;
	v2 |= 0x00000001;
	wbinvd();
	wrmsr(MSR_IA32_CR_PAT, v1, v2);
	wbinvd();
	__flush_tlb_all();
}

static void ttm_pat_enable(void)
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,27))
	if (on_each_cpu(ttm_pat_ipi_handler, NULL, 1, 1) != 0) {
#else
	if (on_each_cpu(ttm_pat_ipi_handler, NULL, 1) != 0) {
#endif
		printk(KERN_ERR "Timed out setting up CPU PAT.\n");
	}
}

void ttm_pat_resume(void)
{
	if (unlikely(!ttm_has_pat))
		return;

	ttm_pat_enable();
}

static int psb_cpu_callback(struct notifier_block *nfb,
			    unsigned long action, void *hcpu)
{
	if (action == CPU_ONLINE) {
		ttm_pat_resume();
	}

	return 0;
}

static struct notifier_block psb_nb = {
	.notifier_call = psb_cpu_callback,
	.priority = 1
};

/*
 * Set i386 PAT entry PAT4 to Write-combining memory type on all processors.
 */

void ttm_pat_init(void)
{
	if (likely(ttm_has_pat))
		return;

	if (!boot_cpu_has(X86_FEATURE_PAT)) {
		return;
	}

	ttm_pat_enable();

	if (num_present_cpus() > 1)
		register_cpu_notifier(&psb_nb);

	ttm_has_pat = 1;
}

void ttm_pat_takedown(void)
{
	if (unlikely(!ttm_has_pat))
		return;

	if (num_present_cpus() > 1)
		unregister_cpu_notifier(&psb_nb);

	ttm_has_pat = 0;
}

pgprot_t pgprot_ttm_x86_wc(pgprot_t prot)
{
	if (likely(ttm_has_pat)) {
		pgprot_val(prot) |= _PAGE_PAT;
		return prot;
	} else {
		return pgprot_noncached(prot);
	}
}

#else

void ttm_pat_init(void)
{
}

void ttm_pat_takedown(void)
{
}

void ttm_pat_resume(void)
{
}

#ifdef CONFIG_X86
#include <asm/pat.h>

pgprot_t pgprot_ttm_x86_wc(pgprot_t prot)
{
	uint32_t cache_bits = ((1) ? _PAGE_CACHE_WC : _PAGE_CACHE_UC_MINUS);

	return __pgprot((pgprot_val(prot) & ~_PAGE_CACHE_MASK) | cache_bits);
}
#else
pgprot_t pgprot_ttm_x86_wc(pgprot_t prot)
{
	BUG();
}
#endif
#endif
