/*
 * Tests of low level timing routines.
 * Copyright (c) 2024 David P. Reed. All rights reserved.
 */

#include "shorthand.h"

#include <stdio.h>
#include <time.h>
#include "time_math.h"
#include "tsc_stuff.h"
#include "tsc_freq.h"
#include "running_average.h"
#include <math.h>

/*
 * macro that takes an asm instruction and clobbered regs and repeats it 10 times counting
 * cycles, and provides (if needed) registers pointing to data in RSI and RDI that can be
 * included in the string "inst" as %0 and %1 substitutions.
 */
#define INSTRUCTION(inst, ... ) asm volatile(inst "# %0 %1" : : "S"(&buffer), "D"(&out_buffer) : __VA_ARGS__);
#define TWENTYTIMES(x) x x x x x x x x x x x x x x x x x x x x  
#define TIME_INSTRUCTION(inst, ...) \
	{       unsigned long begin, fini, elapsed_cycles, nsec;	\
		double cycles_per, nsec_per;				\
		char output[64];					\
		begin = tsc_cycles();					\
		TWENTYTIMES(INSTRUCTION(inst ";", __VA_ARGS__));		\
		fini = tsc_cycles();					\
		elapsed_cycles = fini - begin;				\
		elapsed_cycles -= min(elapsed_cycles, overhead);	\
		nsec = tsc_cycles_to_ns(elapsed_cycles, &ns_adjust);	\
		nsec_per = nsec / 20.0;					\
		cycles_per = elapsed_cycles / 20.0;			\
                fix_asm(inst, output);					\
		printf("20* %s took (%ld cycles) %ld nsec. averaging (%.3g cycles) %.3g nsec.\n", output,	\
		       elapsed_cycles, nsec, cycles_per, nsec_per);		\
	}

/* copy asm instruction string removing %% before register names and replacing %0 with "p" */
static char * fix_asm(const char *inst, char *fixed)
{
	for (char c = *inst; c != '\0'; c =*++inst) {
		if (c == '%') {
			switch (*(inst+1)) {
			case '1':
				strcpy(fixed, "rdi");
				fixed += strlen("rdi");
				inst += 1;
				continue;
			case '0':
				strcpy(fixed, "rsi");
				fixed += strlen("rdi");
				/* fallthru */
			case '%':
				inst += 1;
				continue;
			default:
				break;
			}
		}
		*fixed++ = c;
	}
	*fixed = '\0';
	return fixed;
}


int main(_unused_ int argc, _unused_ char *argv[])
{
	struct timespec start, end;
	long elapsed_nsec;
	int err;
	unsigned long begin, fini, elapsed_cycles;
	unsigned long overhead;
	struct tsc_ns_adjust ns_adjust;
	struct running_stats stats;
	double std;
	unsigned long nsec_variance;
	unsigned long buffer[32] = {0};
	unsigned long out_buffer[32] = {0};

	err = get_tsc_ns_adjust(&ns_adjust);
	err_exit_negative(err, "Error getting tsc ns adjust\n", 0);

	printf("Calibrating speed of interval timers:\n"
	       "  POSIX clock_gettime(CLOCK_REALTIME) and\n"
	       "  inline tsc_cycles() which uses RDTSCP instruction\n"
	       "\n");

	err = clock_gettime(CLOCK_REALTIME, &start);
	err = clock_gettime(CLOCK_REALTIME, &end);
	elapsed_nsec = diff_timespec(&end, &start);

	printf("clock_gettime(CLOCK_REALTIME) takes %ld nsec\n", elapsed_nsec);
	
	begin = tsc_cycles();
	fini = tsc_cycles();
	elapsed_cycles = fini - begin;

	printf("tsc_cycles() takes (%lu cycles) %ld nsec\n", elapsed_cycles,
	       tsc_cycles_to_ns(elapsed_cycles, &ns_adjust));

	running_stats_init(&stats);
	for (int i = 0; i < 100; i++) {
		begin = tsc_cycles();
		fini = tsc_cycles();
		running_stats_sample(&stats, fini - begin);
	}
	
	overhead = running_stats_mean(&stats);
	printf("Mean overhead using tsc_cycles() to measure interval is (%lu cycles) %lu nsec\n",
	       overhead, tsc_cycles_to_ns((unsigned long)overhead, &ns_adjust));
	std = sqrt(running_stats_sample_variance(&stats));
	nsec_variance = tsc_cycles_to_ns((unsigned long)std, &ns_adjust);
	printf("  [Standard deviation of estimated overhead is (%.2g cycles) %lu nsec]\n",
	       std, nsec_variance);

	printf("\n"
	       "Timing sequences of individual instructions repeated 10 times\n"
	       "\n");

	/*
	 * measure single instruction. Clobbered registers must be described to tell C compiler to save them.
	 * %0 contains a pointer to some words on the stack.
	 */

	TIME_INSTRUCTION("rdtsc", "%rax", "%rdx");

	TIME_INSTRUCTION("lfence; rdtsc", "%rax", "%rdx");

	TIME_INSTRUCTION("rdtscp", "%rax", "%rdx", "%rcx");

	TIME_INSTRUCTION("lfence");

	TIME_INSTRUCTION("nop");

	TIME_INSTRUCTION("inc %%rax", "%rax", "cc");

	TIME_INSTRUCTION("mov (%0),%%rdx", "%rdx");

	TIME_INSTRUCTION("mov %%rdx,(%1)");

	TIME_INSTRUCTION("sub %%rax, %%rax", "%rax", "cc");

	TIME_INSTRUCTION("mov $0,%%rax", "%rax");

	TIME_INSTRUCTION("cmpxchg %%rdx,(%1);", "memory", "cc")

	TIME_INSTRUCTION("lock cmpxchg %%rdx,(%1);", "memory", "cc")

	return 0;
}
