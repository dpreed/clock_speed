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
	if (*clist != '\0') {
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
							if (num2 >= (long)setsize * 8) return -1;
							for (int i = num; i < num2; i++)
								CPU_SET_S(num, setsize, set);
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
					if (num >= (long)setsize * 8) return -1;
					CPU_SET_S(num, setsize, set);
					/* end of input, will return */
					continue;
				default:
					break;
				}
			}
			return -1;
		}
	} else {
		/* null string argument defaults to current affinity set */
		return sched_getaffinity(0, setsize, set);
	}
	return 0;
}


/* get a single cpu as a cpu set */
static inline int parse_cpu_single(const char *cpu_num, cpu_set_t *set, size_t setsize)
{
	unsigned long num;
	char *endp;
	CPU_ZERO_S(setsize, set);
	/* there must be one actual argument, no whitespace */
	if (*cpu_num < '0' || *cpu_num > '9') return -1;
	num = strtoul(cpu_num, &endp, 10);
	if (*endp != '\0') return -1; 
	CPU_SET_S(num, setsize, set);
	return 0;
}

static inline int format_cpu_set(const cpu_set_t *set, size_t setsize, char *buffer)
{
	char *out = buffer;
	char byte = 0;
	int leading = 1;
	for (int i = setsize * 8; --i >= 0;) {
		if (CPU_ISSET_S(i, setsize, set))
			byte |= (1 << (i & 0x7));
		if ((i & 0x7) == 0 && (!leading || byte)) {
			/* output byte as hex */
			if (snprintf(out, 3, "%02X", byte) != 2) return -1; 
			out += 2;
			byte = 0;
			leading = 0;
		}
	}
	*out = '\0';
	return out - buffer;
}

#endif
