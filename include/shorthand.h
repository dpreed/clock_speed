/*
 * A few simple C shorthand macros.
 * Copyright (c) 2024 David P. Reed. All rights reserved.
 */

#ifndef _SHORTHAND_
#define _SHORTHAND_

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#define _unused_ __attribute__((unused))

/* magical macros for max and min using GCC/CLANG extension __auto_type */
#define max(x,y) (						\
		{ __auto_type __x = (x); __auto_type __y = (y); \
			__x > __y ? __x : __y; })
#define min(x,y) (						\
		{ __auto_type __x = (x); __auto_type __y = (y); \
			__x > __y ? __y : __x; })

static inline void err_exit_negative(int err, const char *msg, int perr)
{
	if (err < 0) {
		fprintf(stderr, "Error: %s. %s\n", msg, perr? strerror(errno) : ""); 
		exit(1);
	}
}

static inline void err_exit_nonzero(int err, const char *msg, int perr)
{
	if (err != 0) {
		fprintf(stderr, "Error: %s. %s\n", msg, perr? strerror(err) : "");
		exit(1);
	}
}

static inline void null_exit(const void *p, const char *msg, int perr)
{
	if (p == NULL) {
		fprintf(stderr, "Error: %s. %s\n", msg, perr? strerror(errno) : "");
		exit(1);
	}
}

#endif

