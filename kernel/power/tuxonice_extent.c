/*
 * kernel/power/tuxonice_extent.c
 *
 * Copyright (C) 2003-2008 Nigel Cunningham (nigel at tuxonice net)
 *
 * Distributed under GPLv2.
 *
 * These functions encapsulate the manipulation of storage metadata.
 */

#include <linux/suspend.h>
#include "tuxonice_modules.h"
#include "tuxonice_extent.h"
#include "tuxonice_alloc.h"
#include "tuxonice_ui.h"
#include "tuxonice.h"

/**
 * toi_get_extent - return a free extent
 *
 * May fail, returning NULL instead.
 **/
static struct hibernate_extent *toi_get_extent(void)
{
	return (struct hibernate_extent *) toi_kzalloc(2,
			sizeof(struct hibernate_extent), TOI_ATOMIC_GFP);
}

/**
 * toi_put_extent_chain - free a whole chain of extents
 * @chain:	Chain to free.
 **/
void toi_put_extent_chain(struct hibernate_extent_chain *chain)
{
	struct hibernate_extent *this;

	this = chain->first;

	while (this) {
		struct hibernate_extent *next = this->next;
		toi_kfree(2, this, sizeof(*this));
		chain->num_extents--;
		this = next;
	}

	chain->first = NULL;
	chain->last_touched = NULL;
	chain->current_extent = NULL;
	chain->size = 0;
}
EXPORT_SYMBOL_GPL(toi_put_extent_chain);

/**
 * toi_add_to_extent_chain - add an extent to an existing chain
 * @chain:	Chain to which the extend should be added
 * @start:	Start of the extent (first physical block)
 * @end:	End of the extent (last physical block)
 *
 * The chain information is updated if the insertion is successful.
 **/
int toi_add_to_extent_chain(struct hibernate_extent_chain *chain,
		unsigned long start, unsigned long end)
{
	struct hibernate_extent *new_ext = NULL, *cur_ext = NULL;

	/* Find the right place in the chain */
	if (chain->last_touched && chain->last_touched->start < start)
		cur_ext = chain->last_touched;
	else if (chain->first && chain->first->start < start)
		cur_ext = chain->first;

	if (cur_ext) {
		while (cur_ext->next && cur_ext->next->start < start)
			cur_ext = cur_ext->next;

		if (cur_ext->end == (start - 1)) {
			struct hibernate_extent *next_ext = cur_ext->next;
			cur_ext->end = end;

			/* Merge with the following one? */
			if (next_ext && cur_ext->end + 1 == next_ext->start) {
				cur_ext->end = next_ext->end;
				cur_ext->next = next_ext->next;
				toi_kfree(2, next_ext, sizeof(*next_ext));
				chain->num_extents--;
			}

			chain->last_touched = cur_ext;
			chain->size += (end - start + 1);

			return 0;
		}
	}

	new_ext = toi_get_extent();
	if (!new_ext) {
		printk(KERN_INFO "Error unable to append a new extent to the "
				"chain.\n");
		return -ENOMEM;
	}

	chain->num_extents++;
	chain->size += (end - start + 1);
	new_ext->start = start;
	new_ext->end = end;

	chain->last_touched = new_ext;

	if (cur_ext) {
		new_ext->next = cur_ext->next;
		cur_ext->next = new_ext;
	} else {
		if (chain->first)
			new_ext->next = chain->first;
		chain->first = new_ext;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(toi_add_to_extent_chain);

/**
 * toi_serialise_extent_chain - write a chain in the image
 * @owner:	Module writing the chain.
 * @chain:	Chain to write.
 **/
int toi_serialise_extent_chain(struct toi_module_ops *owner,
		struct hibernate_extent_chain *chain)
{
	struct hibernate_extent *this;
	int ret, i = 0;

	ret = toiActiveAllocator->rw_header_chunk(WRITE, owner, (char *) chain,
			sizeof(chain->size) + sizeof(chain->num_extents));
	if (ret)
		return ret;

	this = chain->first;
	while (this) {
		ret = toiActiveAllocator->rw_header_chunk(WRITE, owner,
				(char *) this, 2 * sizeof(this->start));
		if (ret)
			return ret;
		this = this->next;
		i++;
	}

	if (i != chain->num_extents) {
		printk(KERN_EMERG "Saved %d extents but chain metadata says "
			"there should be %d.\n", i, chain->num_extents);
		return 1;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(toi_serialise_extent_chain);

/**
 * toi_load_extent_chain - read back a chain saved in the image
 * @chain:	Chain to load
 *
 * The linked list of extents is reconstructed from the disk. chain will point
 * to the first entry.
 **/
int toi_load_extent_chain(struct hibernate_extent_chain *chain)
{
	struct hibernate_extent *this, *last = NULL;
	int i, ret;

	/* Get the next page */
	ret = toiActiveAllocator->rw_header_chunk_noreadahead(READ, NULL,
			(char *) chain, sizeof(chain->size) +
			sizeof(chain->num_extents));
	if (ret) {
		printk(KERN_ERR "Failed to read the size of extent chain.\n");
		return 1;
	}

	for (i = 0; i < chain->num_extents; i++) {
		this = toi_kzalloc(3, sizeof(struct hibernate_extent),
				TOI_ATOMIC_GFP);
		if (!this) {
			printk(KERN_INFO "Failed to allocate a new extent.\n");
			return -ENOMEM;
		}
		this->next = NULL;
		/* Get the next page */
		ret = toiActiveAllocator->rw_header_chunk_noreadahead(READ,
				NULL, (char *) this, 2 * sizeof(this->start));
		if (ret) {
			printk(KERN_INFO "Failed to read an extent.\n");
			return 1;
		}

		if (last)
			last->next = this;
		else {
			chain->first = this;

			/* 
			 * Couldn't do this earlier, but can't do
			 * goto_start now - we may have already used blocks
			 * in the first chain.
			 */
			chain->current_extent = this;
			chain->current_offset = this->start;
		}
		last = this;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(toi_load_extent_chain);

/**
 *
 **/
void toi_extent_chain_next(struct toi_extent_iterate_state *state)
{
	struct hibernate_extent_chain *this = state->chains +
		state->current_chain;

	if (!this->current_extent)
		return;

	if (this->current_offset == this->current_extent->end) {
		if (this->current_extent->next) {
			this->current_extent = this->current_extent->next;
			this->current_offset = this->current_extent->start;
		} else {
			this->current_extent = NULL;
			this->current_offset = 0;
		}
	} else
		this->current_offset++;
}

/**
 *
 */

static void find_next_chain_unstripped(struct toi_extent_iterate_state *state)
{
	struct hibernate_extent_chain *this = state->chains +
		state->current_chain;

	while (!this->current_extent) {
		int chain_num = ++(state->current_chain);

		if (chain_num == state->num_chains)
			return;

		this = state->chains + state->current_chain;

		if (this->first) {
			this->current_extent = this->first;
			this->current_offset = this->current_extent->start;
		}
	}
}

static void find_next_chain_stripped(struct toi_extent_iterate_state *state)
{
	int start_chain = state->current_chain;
	struct hibernate_extent_chain *this;

	do {
		int chain_num = ++(state->current_chain);

		if (chain_num == state->num_chains)
			state->current_chain = 0;

		/* Back on original chain? Use it again. */
		if (start_chain == state->current_chain)
			return;

		this = state->chains + state->current_chain;
	} while (!this->current_extent);
}

static void find_next_chain(struct toi_extent_iterate_state *state,
		int stripped)
{
	if (stripped)
		find_next_chain_stripped(state);
	else
		find_next_chain_unstripped(state);
}

/**
 * toi_extent_state_next - go to the next extent
 * @blocks: The number of values to progress.
 * @stripe_mode: Whether to spread usage across all chains.
 *
 * Given a state, progress to the next valid entry. We may begin in an
 * invalid state, as we do when invoked after extent_state_goto_start below.
 *
 * When using compression and expected_compression > 0, we let the image size
 * be larger than storage, so we can validly run out of data to return.
 **/
unsigned long toi_extent_state_next(struct toi_extent_iterate_state *state,
		int blocks, int stripe_mode)
{
	int i;

	if (state->current_chain == state->num_chains)
		return 0;

	/* Assume chains always have lengths that are multiples of @blocks */
	for (i = 0; i < blocks; i++)
		toi_extent_chain_next(state);

	if (stripe_mode ||
	    !((state->chains + state->current_chain)->current_extent))
		find_next_chain(state, stripe_mode);

	if (state->current_chain == state->num_chains)
		return 0;
	else
		return (state->chains + state->current_chain)->current_offset;
}
EXPORT_SYMBOL_GPL(toi_extent_state_next);

/**
 * toi_extent_state_goto_start - reinitialize an extent chain iterator
 * @state:	Iterator to reinitialize
 **/
void toi_extent_state_goto_start(struct toi_extent_iterate_state *state)
{
	int i;

	for (i = 0; i < state->num_chains; i++) {
		struct hibernate_extent_chain *this = state->chains + i;
		this->current_extent = this->first;
		if (this->current_extent)
			this->current_offset = this->current_extent->start;
	}

	state->current_chain = 0;
}
EXPORT_SYMBOL_GPL(toi_extent_state_goto_start);

/**
 * toi_extent_state_save - save state of the iterator
 * @state:		Current state of the chain
 * @saved_state:	Iterator to populate
 *
 * Given a state and a struct hibernate_extent_state_store, save the current
 * position in a format that can be used with relocated chains (at
 * resume time).
 **/
void toi_extent_state_save(struct toi_extent_iterate_state *state,
		struct hibernate_extent_iterate_saved_state *saved_state)
{
	struct hibernate_extent *extent;
	struct hibernate_extent_chain *cur_chain;
	int i;

	saved_state->chain_num = state->current_chain;

	if (saved_state->chain_num == -1)
		return;

	for (i = 0; i < state->num_chains; i++) {
		cur_chain = state->chains + i;

		saved_state->extent_num[i] = 0;
		saved_state->offset[i] = cur_chain->current_offset;

		extent = cur_chain->first;

		while (extent != cur_chain->current_extent) {
			saved_state->extent_num[i]++;
			extent = extent->next;
		}
	}
}
EXPORT_SYMBOL_GPL(toi_extent_state_save);

/**
 * toi_extent_state_restore - restore the position saved by extent_state_save
 * @state:		State to populate
 * @saved_state:	Iterator saved to restore
 **/
void toi_extent_state_restore(struct toi_extent_iterate_state *state,
		struct hibernate_extent_iterate_saved_state *saved_state)
{
	int i;
	struct hibernate_extent_chain *cur_chain;

	if (saved_state->chain_num == -1) {
		toi_extent_state_goto_start(state);
		return;
	}

	state->current_chain = saved_state->chain_num;

	for (i = 0; i < state->num_chains; i++) {
		int posn = saved_state->extent_num[i];
		cur_chain = state->chains + i;

		cur_chain->current_extent = cur_chain->first;
		cur_chain->current_offset = saved_state->offset[i];

		while (posn--)
			cur_chain->current_extent = cur_chain->current_extent->next;
	}
}
EXPORT_SYMBOL_GPL(toi_extent_state_restore);
