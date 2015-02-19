/*
 * kernel/power/tuxonice_incremental.c
 *
 * Copyright (C) 2014 Nigel Cunningham (nigel at tuxonice net)
 *
 * This file is released under the GPLv2.
 *
 * This file contains routines related to storing incremental images - that
 * is, retaining an image after an initial cycle and then storing incremental
 * changes on subsequent hibernations.
 *
 * Based in part on on...
 *
 * Debug helper to dump the current kernel pagetables of the system
 * so that we can see what the various memory ranges are set to.
 *
 * (C) Copyright 2008 Intel Corporation
 *
 * Author: Arjan van de Ven <arjan@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

#include <linux/mm.h>
#include <linux/tuxonice.h>
#include <linux/sched.h>
#include <asm/pgtable.h>
#include <asm/cacheflush.h>
#include <asm/page.h>
#include "tuxonice_pageflags.h"
#include "tuxonice_builtin.h"

int toi_disable_memory_ro;
unsigned int toi_search;

extern void kdb_init(int level);
extern noinline void kgdb_breakpoint(void);

/* Multipliers for offsets within the PTEs */
#define PTE_LEVEL_MULT (PAGE_SIZE)
#define PMD_LEVEL_MULT (PTRS_PER_PTE * PTE_LEVEL_MULT)
#define PUD_LEVEL_MULT (PTRS_PER_PMD * PMD_LEVEL_MULT)
#define PGD_LEVEL_MULT (PTRS_PER_PUD * PUD_LEVEL_MULT)

/*
 * This function gets called on a break in a continuous series
 * of PTE entries; the next one is different so we need to
 * print what we collected so far.
 */
static void note_page(void *addr)
{
    static struct page *lastpage;
    struct page *page;

    page = virt_to_page(addr);

    if (page != lastpage) {
        unsigned int level;
        pte_t *pte = lookup_address((unsigned long) addr, &level);
        struct page *pt_page2 = pte_page(*pte);
        printk("Note page %p (=> %p => %p|%ld).\n", addr, pte, pt_page2, page_to_pfn(pt_page2));
        SetPageTOI_Untracked(pt_page2);
        lastpage = page;
    }
}

static void walk_pte_level(pmd_t addr)
{
	int i;
	pte_t *start;

	start = (pte_t *) pmd_page_vaddr(addr);
	for (i = 0; i < PTRS_PER_PTE; i++) {
		note_page(start);
		start++;
	}
}

#if PTRS_PER_PMD > 1

static void walk_pmd_level(pud_t addr)
{
	int i;
	pmd_t *start;

	start = (pmd_t *) pud_page_vaddr(addr);
	for (i = 0; i < PTRS_PER_PMD; i++) {
		if (!pmd_none(*start)) {
			if (pmd_large(*start) || !pmd_present(*start))
				note_page(start);
			else
				walk_pte_level(*start);
		} else
			note_page(start);
		start++;
	}
}

#else
#define walk_pmd_level(a) walk_pte_level(__pmd(pud_val(a)))
#define pud_large(a) pmd_large(__pmd(pud_val(a)))
#define pud_none(a)  pmd_none(__pmd(pud_val(a)))
#endif

#if PTRS_PER_PUD > 1

static void walk_pud_level(pgd_t addr)
{
	int i;
	pud_t *start;

	start = (pud_t *) pgd_page_vaddr(addr);

	for (i = 0; i < PTRS_PER_PUD; i++) {
		if (!pud_none(*start)) {
			if (pud_large(*start) || !pud_present(*start))
				note_page(start);
			else
				walk_pmd_level(*start);
		} else
			note_page(start);

		start++;
	}
}

#else
#define walk_pud_level(a) walk_pmd_level(__pud(pgd_val(a)))
#define pgd_large(a) pud_large(__pud(pgd_val(a)))
#define pgd_none(a)  pud_none(__pud(pgd_val(a)))
#endif

/*
 * Not static in the original at the time of writing, so needs renaming here.
 */
static void toi_ptdump_walk_pgd_level(pgd_t *pgd)
{
#ifdef CONFIG_X86_64
	pgd_t *start = (pgd_t *) &init_level4_pgt;
#else
	pgd_t *start = swapper_pg_dir;
#endif
	int i;
	if (pgd) {
		start = pgd;
	}

	for (i = 0; i < PTRS_PER_PGD; i++) {
		if (!pgd_none(*start)) {
			if (pgd_large(*start) || !pgd_present(*start))
				note_page(start);
			else
				walk_pud_level(*start);
		} else
			note_page(start);

		start++;
	}

	/* Flush out the last page */
	note_page(start);
}

#ifdef CONFIG_PARAVIRT
extern struct pv_info pv_info;

static void toi_set_paravirt_ops_untracked(void) {
    int i;

    unsigned long pvpfn = page_to_pfn(virt_to_page(__parainstructions)),
                  pvpfn_end = page_to_pfn(virt_to_page(__parainstructions_end));
    printk(KERN_EMERG ".parainstructions goes from pfn %ld to %ld.\n", pvpfn, pvpfn_end);
    for (i = pvpfn; i <= pvpfn_end; i++) {
        SetPageTOI_Untracked(pfn_to_page(i));
    }
}
#else
#define toi_set_paravirt_ops_untracked() { do { } while(0) }
#endif

void toi_generate_untracked_map(void)
{
    /* Pagetable pages */
    toi_ptdump_walk_pgd_level(NULL);

    /* Printk buffer */
    toi_set_logbuf_untracked();

    /* Paravirt ops */
    toi_set_paravirt_ops_untracked();

    /* Task structs and stacks */
    {
        struct task_struct *p, *t;

        for_each_process_thread(p, t) {
            printk("Setting task %s page %p (%p) untracked. Stack %p (%p)\n", p->comm, virt_to_page(p), p, p->stack, virt_to_page(p->stack));
            SetPageTOI_Untracked(virt_to_page(p));
            SetPageTOI_Untracked(virt_to_page(p->stack));
        }
    }

    /* IST stacks */

    /* Per CPU data */
}

/**
 * TuxOnIce's incremental image support works by marking all memory apart from
 * the page tables read-only, then in the page-faults that result enabling
 * writing if appropriate and flagging the page as dirty. Free pages are also
 * marked as dirty and not protected so that if allocated, they will be included
 * in the image without further processing.
 *
 * toi_reset_dirtiness is called when and image exists and incremental images are
 * enabled, and each time we resume thereafter. It is not invoked on a fresh boot.
 *
 * This routine should be called from a single-cpu-running context to avoid races in setting
 * page dirty/read only flags.
 *
 * TODO: Make "it is not invoked on a fresh boot" true  when I've finished developing it!
 *
 * TODO: Consider Xen paravirt guest boot issues. See arch/x86/mm/pageattr.c.
 **/
static __init int toi_reset_dirtiness(void)
{
	struct zone *zone;
	unsigned long loop;
        int allocated_bitmaps = 0;

        if (!free_map) {
            printk(KERN_EMERG "Allocating free and pagetable bitmaps.\n");
            BUG_ON(toi_alloc_bitmap(&free_map));
            allocated_bitmaps = 1;
        }

        printk(KERN_EMERG "Generate free page map.\n");
        toi_generate_free_page_map();

	kdb_init(1);

        if (1) {
        printk(KERN_EMERG "Reset dirtiness.\n");
        for_each_populated_zone(zone) {
            // 64 bit only. No need to worry about highmem.
            //int highmem = is_highmem(zone);

            for (loop = 0; loop < zone->spanned_pages; loop++) {
                unsigned long pfn = zone->zone_start_pfn + loop;
                struct page *page;
                int chunk_size;

                if (!pfn_valid(pfn)) {
                    continue;
                }

                chunk_size = toi_size_of_free_region(zone, pfn);
                if (chunk_size) {
                    /*
                     * If the page gets allocated, it will be need
                     * saving in an image.
                     * Don't bother with explicitly removing any
                     * RO protection applied below.
                     * We'll SetPageTOI_Dirty(page) if/when it
                     * gets allocated.
                     */
                    printk("Skipping %d free pages.\n", chunk_size);
                    loop += chunk_size - 1;
                    continue;
                }

                page = pfn_to_page(pfn);

                if (PageNosave(page) || PageTOI_Untracked(page)) {
                    continue;
                }

                /**
                 * Do we need to (re)protect the page?
                 * If it is already protected (PageTOI_RO), there is
                 * nothing to do - skip the following.
                 * If it is marked as dirty (PageTOI_Dirty), it was
                 * either free and has been allocated or has been
                 * written to and marked dirty. Reset the dirty flag
                 * and (re)apply the protection.
                 */
                if (!PageTOI_RO(page)) {
                    int enforce = !toi_disable_memory_ro && (!toi_search || pfn < toi_search);
                    /**
                     * Don't worry about whether the Dirty flag is
                     * already set. If this is our first call, it
                     * won't be.
                     */

                    preempt_disable();

                    ClearPageTOI_Dirty(page);
                    SetPageTOI_RO(page);
                    printk(KERN_EMERG "%saking page %ld (%p|%p) read only.\n", enforce ? "M" : "Not m", pfn, page, page_address(page));

                    if (pfn == 7694) {
                        //extern int kgdb_break_asap;
                        //kgdb_break_asap = 1;

                        //kdb_init(1);
                        //kgdb_break_asap = 0;
                        //kgdb_breakpoint();
                    }

                    if (enforce)
                        set_memory_ro((unsigned long) page_address(page), 1);

                    preempt_enable();
                }
            }
        }
        }

        if (allocated_bitmaps) {
            toi_free_bitmap(&free_map);
        }

        printk(KERN_EMERG "Done resetting dirtiness.\n");
        return 0;
}

#if 0
/* toi_generate_untracked_map
 *
 * Populate a map of where pagetable pages are, so we don't mark the
 * leaves read-only and create recursive page faults.
 *
 * We're only interested in the bottom level.
 */
void toi_generate_untracked_map(void)
{
    unsigned long flags, i;
    struct zone *zone;
    unsigned long pfn, pa;
    //struct page *page, *pt_page, *page2;
    struct page *page;

    for_each_populated_zone(zone) {
        struct page *pt_page;
        unsigned long ptepfn, lastptepfn = 0;
        pte_t *pte, *ptep;
        unsigned int level;
        void *pt_page2;

        if (!zone->spanned_pages)
            continue;

        spin_lock_irqsave(&zone->lock, flags);

        pfn = zone->zone_start_pfn;
        page = pfn_to_page(pfn);
        pt_page = virt_to_page((void *) page);
        pte = lookup_address((unsigned long) page_address(pt_page), &level);
        ptepfn = pte_pfn(*pte);
        pt_page2 = pte_page(*pte);

        if (1) {
            printk("Pages %p-%p are page table pages.\n", pt_page2, pt_page2 + DIV_ROUND_UP(zone->spanned_pages, 64));
        }


        if (1) {
            printk("Zone spanning %ld pages.\n", zone->spanned_pages);

            pfn = zone->zone_start_pfn;
            page = pfn_to_page(pfn);
            pt_page = virt_to_page((void *) page);
            pte = lookup_address((unsigned long) page_address(pt_page), &level);
            ptepfn = pte_pfn(*pte);
            pt_page2 = pte_page(*pte);

            printk("Page %ld => Struct page %p => page table page %p => ptepfn %ld => pte page %p.\n", pfn, page, pt_page, ptepfn, pt_page2);

            for (i = 0; i < zone->spanned_pages; i++) {
                pfn = zone->zone_start_pfn + i;

                if (!pfn_valid(pfn))
                    continue;

                if (0 && !memcmp(page_address(page), (void *) pfn_to_page(zone->zone_start_pfn), PAGE_SIZE)) {
                    printk(KERN_EMERG "zone start page for pfn %ld matches pfn %ld.\n", zone->zone_start_pfn, pfn);
                }

                page = pfn_to_page(pfn);
                pt_page = virt_to_page((void *) page);
                pte = lookup_address((unsigned long) page_address(pt_page), &level);
                ptepfn = pte_pfn(*pte);
                pt_page2 = pte_page(*pte);

                if (lastptepfn != ptepfn) {
                    printk("Page %ld => Struct page %p => page table page %p => ptepfn %ld => pte page %p.\n", pfn, page, pt_page, ptepfn, pt_page2);
                    lastptepfn = ptepfn;
                }
            }

            pfn = zone->zone_start_pfn + zone->spanned_pages - 1;
            while (!pfn_valid(pfn)) {
                pfn--;
            }
            if (pfn < (zone->zone_start_pfn + zone->spanned_pages - 1)) {
                printk("Last valid pfn in this zone is at %ld.\n", pfn);
            }
            page = pfn_to_page(pfn);
            pt_page = virt_to_page((void *) page);
            pa = (unsigned long) page_address(pt_page);
            pte = lookup_address(pa, &level);
            ptepfn = pte_pfn(*pte);
            pt_page2 = pte_page(*pte);
            ptep = (pte_t *) pt_page2;

            //printk("Page %ld => Struct page %p => page table page %p => ptepfn %ld => pte page %p.\n", pfn, page, pt_page, ptepfn, (void *) ptep[pfn]);
        }

        spin_unlock_irqrestore(&zone->lock, flags);
    }
}
#else
extern void toi_generate_untracked_map(void);
#endif

// Leave early_initcall for pages to register untracked sections.
core_initcall(toi_reset_dirtiness);

static int __init toi_search_setup(char *str)
{
	int value;

	if (sscanf(str, "=%d", &value) && value)
		toi_search = value;

	return 1;
}

__setup("toi_max", toi_search_setup);

static int __init toi_disable_memory_ro_setup(char *str)
{
	int value;

	if (sscanf(str, "=%d", &value) && value)
		toi_disable_memory_ro = value;

	return 1;
}

__setup("toi_no_ro", toi_disable_memory_ro_setup);
