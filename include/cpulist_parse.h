/*
 * Parse a string that describes a cpuset and create the cpuset mask.
 *
 * Copyright (c) 2024 David P. Reed. All rights reserved. 
 */

#ifndef _CPU_LIST_PARSE_H_
#define  _CPU_LIST_PARSE_H_

#include <stdlib.h>
#include <sched.h>

static inline int parse_cpu_list(const char *clist, cpu_set_t *set, size_t setsize)
{
	CPU_ZERO_S(setsize, set);
	for (char c = *clist; c != '\0'; c = *clist) {
		/* clist is changed, or -1 is returned */
		long num, num2;
		char *endp, *endp2;
		if (c == ' ') return -1;
		num = strtoul(clist, &endp, 10);
		if ((endp != clist) && c >= '0' && c <= '9') {
			switch (*endp) {
			case '-':
				c = *++endp;
				num2 = strtoul(endp, &endp2, 10);
				if ((endp2 != endp) && c >= '0' && c <= '9') {
					switch (*endp2) {
					case ',':
						endp2 += 1;
						/*  fallthru */
					case '\0':
						clist = endp2;
						if (num2 >= (long)setsize) return -1;
						for (int i = num; i < num2; i++)
							CPU_SET(num, set);
						continue;
					default:
						break;
					}
				}
				break;
			case ',':
				/* move to next list element */
				endp += 1;
				/* fallthru */
			case '\0':
				clist = endp;
				if (num >= (long)setsize) return -1;
				CPU_SET(num, set);
				/* end of input, will return */
				continue;
			default:
				break;
			}
		}
		return -1;
	}
			
	return 0;
}


#endif
