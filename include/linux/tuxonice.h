/*
 * include/linux/tuxonice.h
 *
 * Copyright (C) 2015 Nigel Cunningham (nigel at tuxonice net)
 *
 * This file is released under the GPLv2.
 */

#ifndef INCLUDE_LINUX_TUXONICE_H
#define INCLUDE_LINUX_TUXONICE_H
#ifdef CONFIG_TOI_INCREMENTAL
extern void toi_set_logbuf_untracked(void);
extern int toi_make_writable(unsigned long address);
#else
#define toi_set_logbuf_untracked() do { } while(0)
#define toi_make_writable(addr) (0)
#endif
#endif
