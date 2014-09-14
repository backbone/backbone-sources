/*
 * kernel/power/tuxonice_pageflags.h
 *
 * Copyright (C) 2004-2014 Nigel Cunningham (nigel at tuxonice net)
 *
 * This file is released under the GPLv2.
 */

#ifndef KERNEL_POWER_TUXONICE_PAGEFLAGS_H
#define KERNEL_POWER_TUXONICE_PAGEFLAGS_H

struct toi_memory_bitmap;

void toi_memory_bm_set_bit(struct toi_memory_bitmap *bm, unsigned long pfn);
unsigned long toi_memory_bm_next_pfn(struct toi_memory_bitmap *bm);
unsigned long toi_memory_bm_next_pfn_index(struct toi_memory_bitmap *bm, int index);
void toi_memory_bm_position_reset(struct toi_memory_bitmap *bm);
void toi_memory_bm_free(struct toi_memory_bitmap *bm, int clear_nosave_free);
int toi_alloc_bitmap(struct toi_memory_bitmap **bm);
void toi_free_bitmap(struct toi_memory_bitmap **bm);
void toi_memory_bm_clear(struct toi_memory_bitmap *bm);
void toi_memory_bm_clear_bit(struct toi_memory_bitmap *bm, unsigned long pfn);
void toi_memory_bm_set_bit(struct toi_memory_bitmap *bm, unsigned long pfn);
int toi_memory_bm_test_bit(struct toi_memory_bitmap *bm, unsigned long pfn);
int toi_memory_bm_test_bit_index(struct toi_memory_bitmap *bm, unsigned long pfn, int index);
void toi_memory_bm_clear_bit_index(struct toi_memory_bitmap *bm, unsigned long pfn, int index);

struct toi_module_ops;
int toi_memory_bm_write(struct toi_memory_bitmap *bm, int (*rw_chunk)
	(int rw, struct toi_module_ops *owner, char *buffer, int buffer_size));
int toi_memory_bm_read(struct toi_memory_bitmap *bm, int (*rw_chunk)
	(int rw, struct toi_module_ops *owner, char *buffer, int buffer_size));
int toi_memory_bm_space_needed(struct toi_memory_bitmap *tbm);

extern struct toi_memory_bitmap *pageset1_map;
extern struct toi_memory_bitmap *pageset1_copy_map;
extern struct toi_memory_bitmap *pageset2_map;
extern struct toi_memory_bitmap *page_resave_map;
extern struct toi_memory_bitmap *io_map;
extern struct toi_memory_bitmap *nosave_map;
extern struct toi_memory_bitmap *free_map;
extern struct toi_memory_bitmap *compare_map;

#define PagePageset1(page) \
	(toi_memory_bm_test_bit(pageset1_map, page_to_pfn(page)))
#define SetPagePageset1(page) \
	(toi_memory_bm_set_bit(pageset1_map, page_to_pfn(page)))
#define ClearPagePageset1(page) \
	(toi_memory_bm_clear_bit(pageset1_map, page_to_pfn(page)))

#define PagePageset1Copy(page) \
	(toi_memory_bm_test_bit(pageset1_copy_map, page_to_pfn(page)))
#define SetPagePageset1Copy(page) \
	(toi_memory_bm_set_bit(pageset1_copy_map, page_to_pfn(page)))
#define ClearPagePageset1Copy(page) \
	(toi_memory_bm_clear_bit(pageset1_copy_map, page_to_pfn(page)))

#define PagePageset2(page) \
	(toi_memory_bm_test_bit(pageset2_map, page_to_pfn(page)))
#define SetPagePageset2(page) \
	(toi_memory_bm_set_bit(pageset2_map, page_to_pfn(page)))
#define ClearPagePageset2(page) \
	(toi_memory_bm_clear_bit(pageset2_map, page_to_pfn(page)))

#define PageWasRW(page) \
	(toi_memory_bm_test_bit(pageset2_map, page_to_pfn(page)))
#define SetPageWasRW(page) \
	(toi_memory_bm_set_bit(pageset2_map, page_to_pfn(page)))
#define ClearPageWasRW(page) \
	(toi_memory_bm_clear_bit(pageset2_map, page_to_pfn(page)))

#define PageResave(page) (page_resave_map ? \
	toi_memory_bm_test_bit(page_resave_map, page_to_pfn(page)) : 0)
#define SetPageResave(page) \
	(toi_memory_bm_set_bit(page_resave_map, page_to_pfn(page)))
#define ClearPageResave(page) \
	(toi_memory_bm_clear_bit(page_resave_map, page_to_pfn(page)))

#define PageNosave(page) (nosave_map ? \
		toi_memory_bm_test_bit(nosave_map, page_to_pfn(page)) : 0)
#define SetPageNosave(page) \
	(toi_memory_bm_set_bit(nosave_map, page_to_pfn(page)))
#define ClearPageNosave(page) \
	(toi_memory_bm_clear_bit(nosave_map, page_to_pfn(page)))

#define PageNosaveFree(page) (free_map ? \
		toi_memory_bm_test_bit(free_map, page_to_pfn(page)) : 0)
#define SetPageNosaveFree(page) \
	(toi_memory_bm_set_bit(free_map, page_to_pfn(page)))
#define ClearPageNosaveFree(page) \
	(toi_memory_bm_clear_bit(free_map, page_to_pfn(page)))

#define PageCompareChanged(page) (compare_map ? \
		toi_memory_bm_test_bit(compare_map, page_to_pfn(page)) : 0)
#define SetPageCompareChanged(page) \
	(toi_memory_bm_set_bit(compare_map, page_to_pfn(page)))
#define ClearPageCompareChanged(page) \
	(toi_memory_bm_clear_bit(compare_map, page_to_pfn(page)))

extern void save_pageflags(struct toi_memory_bitmap *pagemap);
extern int load_pageflags(struct toi_memory_bitmap *pagemap);
extern int toi_pageflags_space_needed(void);
#endif
