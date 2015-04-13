/*
 * kernel/power/tuxonice_copy_before_write.c
 *
 * Copyright (C) 2015 Nigel Cunningham (nigel at nigelcunningham com au)
 *
 * This file is released under the GPLv2.
 *
 * Routines (apart from the fault handling code) to deal with allocating memory
 * for copying pages before they are modified, restoring the contents and getting
 * the contents written to disk.
 */

#include <linux/percpu-defs.h>
#include <linux/sched.h>
#include <linux/tuxonice.h>
#include "tuxonice_alloc.h"
#include "tuxonice.h"

DEFINE_PER_CPU(struct toi_cbw_state, toi_cbw_states);
#define CBWS_PER_PAGE (PAGE_SIZE / sizeof(struct toi_cbw))
#define toi_cbw_pool_size 100

static void _toi_free_cbw_data(struct toi_cbw_state *state)
{
    if (!state->first)
        return;

    while(state->first) {
        toi_free_page(41, (unsigned long) state->first->virt);
        if (state->first == state->last) {
            state->first = state->next = state->last = NULL;
        } else {
            state->first++;
        }
    }
}

void toi_free_cbw_data(void)
{
    int i;

    for (i = 0; i < NR_CPUS; i++) {
        struct toi_cbw_state *state = &per_cpu(toi_cbw_states, i);

        if (!cpu_online(i))
            continue;

        state->enabled = 0;

        while (state->active) {
            schedule();
        }

        _toi_free_cbw_data(state);
    }
}

static int _toi_allocate_cbw_data(struct toi_cbw_state *state)
{
    while(state->size < toi_cbw_pool_size) {
        int i;
        struct toi_cbw *ptr;

        ptr = (struct toi_cbw *) toi_get_zeroed_page(41, GFP_KERNEL);

        if (!ptr) {
            return -ENOMEM;
        }

        if (!state->first) {
            state->first = state->next = state->last = ptr;
        }

        for (i = 0; i < CBWS_PER_PAGE; i++) {
            struct toi_cbw *cbw = &ptr[i];

            if (cbw == state->first)
                continue;

            cbw->virt = toi_alloc_page(41, GFP_KERNEL);
            if (!cbw->virt) {
                state->size += i;
                return -ENOMEM;
            }

            state->last->next = cbw;
            state->last = cbw;
        }

        state->size += CBWS_PER_PAGE;
    }

    return 0;
}


int toi_allocate_cbw_data(void)
{
    int i, result;

    if (!toi_keeping_image)
        return 0;

    for (i = 0; i < NR_CPUS; i++) {
        struct toi_cbw_state *state = &per_cpu(toi_cbw_states, i);

        if (!cpu_online(i))
            continue;

        result = _toi_allocate_cbw_data(state);

        if (result)
            return result;
    }

    return 0;
}

void toi_cbw_restore(void)
{
    if (!toi_keeping_image)
        return;

}

void toi_cbw_write(void)
{
    if (!toi_keeping_image)
        return;

}
