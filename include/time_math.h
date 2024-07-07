/*
 * Standard <time.h> doesn't include the ability to do math on POSIX timespec.
 * Useful basic routines included here. Static inlined to make more useful.
 *
 * Copyright (c) 2024 David P. Reed. All rights reserved.
 */

#ifndef _TIME_MATH_H_
#define _TIME_MATH_H_
#include <time.h> 		/* include here so struct timespec defined before the function */

static inline signed long diff_timespec(const struct timespec *t1, const struct timespec *t0)
{
	struct timespec diff = {.tv_sec = t1->tv_sec - t0->tv_sec,
				.tv_nsec = t1->tv_nsec - t0->tv_nsec};
	if (diff.tv_nsec < 0) {
		diff.tv_nsec += 1000000000L;
		diff.tv_sec -= 1;
	}
	return diff.tv_sec * 1000000000L + diff.tv_nsec;
}

#endif
