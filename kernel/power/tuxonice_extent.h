/*
 * kernel/power/tuxonice_extent.h
 *
 * Copyright (C) 2003-2008 Nigel Cunningham (nigel at tuxonice net)
 *
 * This file is released under the GPLv2.
 *
 * It contains declarations related to extents. Extents are
 * TuxOnIce's method of storing some of the metadata for the image.
 * See tuxonice_extent.c for more info.
 *
 */

#include "tuxonice_modules.h"

#ifndef EXTENT_H
#define EXTENT_H

#ifndef MAX_SWAPFILES
#define MAX_SWAPFILES 32
#endif

struct hibernate_extent {
	unsigned long start, end;
	struct hibernate_extent *next;
};

struct hibernate_extent_chain {
	unsigned long size; /* size of the chain ie sum (max-min+1) */
	int num_extents, prio, next;
	struct hibernate_extent *first, *last_touched;
	struct hibernate_extent *current_extent;
	unsigned long current_offset;
};

struct toi_extent_iterate_state {
	struct hibernate_extent_chain *chains;
	int num_chains;
	int current_chain;
};

struct hibernate_extent_iterate_saved_state {
	int chain_num;
	int extent_num[MAX_SWAPFILES];
	unsigned long offset[MAX_SWAPFILES];
};

#define toi_extent_state_eof(state) \
	((state)->num_chains == (state)->current_chain)

/* Simplify iterating through all the values in an extent chain */
#define toi_extent_for_each(extent_chain, extentpointer, value) \
if ((extent_chain)->first) \
	for ((extentpointer) = (extent_chain)->first, (value) = \
			(extentpointer)->start; \
	     ((extentpointer) && ((extentpointer)->next || (value) <= \
				 (extentpointer)->end)); \
	     (((value) == (extentpointer)->end) ? \
		((extentpointer) = (extentpointer)->next, (value) = \
		 ((extentpointer) ? (extentpointer)->start : 0)) : \
			(value)++))

void toi_put_extent_chain(struct hibernate_extent_chain *chain);
int toi_add_to_extent_chain(struct hibernate_extent_chain *chain,
		unsigned long start, unsigned long end);
int toi_serialise_extent_chain(struct toi_module_ops *owner,
		struct hibernate_extent_chain *chain);
int toi_load_extent_chain(struct toi_extent_iterate_state *state, int index);

void toi_extent_state_save(struct toi_extent_iterate_state *state,
		struct hibernate_extent_iterate_saved_state *saved_state);
void toi_extent_state_restore(struct toi_extent_iterate_state *state,
		struct hibernate_extent_iterate_saved_state *saved_state);
void toi_extent_state_goto_start(struct toi_extent_iterate_state *state);
unsigned long toi_extent_state_next(struct toi_extent_iterate_state *state,
		int blocks, int current_stream);
void toi_insert_chain_in_prio_list(struct toi_extent_iterate_state *state,
		int i);
extern struct hibernate_extent_iterate_saved_state toi_writer_posn_save[4];
extern struct toi_extent_iterate_state toi_writer_posn;
#endif
