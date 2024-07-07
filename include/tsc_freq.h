/*
 * routines to manage tsc frequency conversion to nsec.
 * get_tsc_ns_adjust is "inlined" so if it isn't used in the source file, it won't be generated.
 * it really won't be called more than once in any use case I can imagine.
 *
 * Copyright (c) 2024 David P. Reed. All rights reserved.
 */

#ifndef _TSC_FREQ_H_
#define _TSC_FREQ_H_

#include <stdint.h>
#include <unistd.h>
#include <sys/mman.h>
#include "perf_stuff.h"

struct tsc_ns_adjust {
	uint32_t time_mult;
	uint32_t time_shift;
};

static inline int get_tsc_ns_adjust(struct tsc_ns_adjust *ns_adjustp)
{
	int ret = 0;
	struct perf_event_attr pe = {
		.type = PERF_TYPE_HARDWARE,
		.size = sizeof(struct perf_event_attr),
		.config = PERF_COUNT_HW_INSTRUCTIONS,
		.disabled = 1,
		.exclude_kernel = 1,
		.exclude_hv = 1
	};
	int fd = perf_event_open(&pe, 0, -1, -1, 0);
	if (fd == -1) return fd;

	struct perf_event_mmap_page *pc = mmap(NULL, getpagesize(), PROT_READ, MAP_SHARED, fd, 0);
	if (pc == NULL) { ret = -1; goto pc_close; }
	if (!pc->cap_user_time) { ret = -1; goto pc_close; }
	ns_adjustp -> time_mult = pc->time_mult;
	ns_adjustp -> time_shift = pc->time_shift;
 pc_close:
	close(fd);
	return ret;
}

static inline unsigned long tsc_cycles_to_ns(unsigned long cycles, const struct tsc_ns_adjust *ns_adjustp)
{
    __uint128_t result = cycles; /* multiplication exceeds 64 bits */
    result *= ns_adjustp -> time_mult;
    result >>= ns_adjustp -> time_shift;
    return (unsigned long)result;
}

#endif
