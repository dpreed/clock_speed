/*
 * Tests of low level timing routines.
 * Copyright (c) 2024 David P. Reed. All rights reserved.
 */

#define _GNU_SOURCE
#include <unistd.h>
#include "shorthand.h"
#include <stdbool.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <sys/sysinfo.h>
#include <pthread.h>
#include "time_math.h"
#include "tsc_stuff.h"
#include "tsc_freq.h"
#include "running_average.h"
#include "cpulist_parse.h"
#include "spin_barrier.h"
#include "pstamp.h"

/*
 * macro that takes an asm instruction and clobbered regs and repeats it 10 times counting
 * cycles, and provides (if needed) registers pointing to data in RSI and RDI that can be
 * included in the string "inst" as %0 and %1 substitutions.
 * TODO: if b1 or b1 parameter omitted, omit the "S" and/or "D" options in INSTRUCTION,
 * since this generates two unnecessary timed instructions
 */
#define INSTRUCTION(inst, b1, b2, ... ) asm volatile(inst "# %0 %1" : : "S"(&b1), "D"(&b2) : __VA_ARGS__);
#define TWENTYTIMES(x) x x x x x x x x x x x x x x x x x x x x  
#define TIME_INSTRUCTION(inst, b1, b2, ...)			\
	{       unsigned long begin, fini, elapsed_cycles, nsec;	\
		double cycles_per, nsec_per;				\
		char output[64];					\
		begin = tsc_cycles();					\
		TWENTYTIMES(INSTRUCTION(inst ";", b1, b2, __VA_ARGS__)); \
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
#define TIME_CODE_20(line) \
	{								\
		unsigned long begin, fini, elapsed, nsec;		\
		double cycles_per, nsec_per;				\
		begin = tsc_cycles();					\
		TWENTYTIMES(line);					\
		fini = tsc_cycles();					\
		elapsed = (fini - begin);				\
		elapsed -= min(elapsed, overhead);			\
		nsec = tsc_cycles_to_ns(elapsed, &ns_adjust);		\
		nsec_per = nsec / 20.0;					\
		cycles_per = elapsed / 20.0;				\
		printf("20* %s took (cycles %lu) %lu nsec. averaging (%.3g cycles) %.3g nsec.\n", \
		       #line, elapsed, nsec, cycles_per, nsec_per);	\
	}
#define TIME_CODE(line)							\
	{								\
	        unsigned long begin, fini, elapsed;			\
		begin = tsc_cycles();					\
		line;							\
		fini = tsc_cycles();					\
		elapsed = fini - begin - overhead;			\
		printf("%s took (cycles %lu) %lu nsec.\n", #line, elapsed, \
		       tsc_cycles_to_ns(elapsed, &ns_adjust));		\
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

/* very simple call that gives a different answer each time but isn't inlineable */
static __attribute__((__noinline__)) int simple_call(void)
{
	static int simple_call_count = 0;
	return ++simple_call_count;
}

static struct tsc_ns_adjust ns_adjust;

static void *alt_thread_main(void *arg);

struct thread_shared_data {
	pthread_barrier_t barrier1, barrier2;
	barrier_t spin_barrier;
	unsigned long timestamp1, timestamp2;
	unsigned long arrival1, arrival2;
};

int main(_unused_ int argc, _unused_ char *argv[])
{
	struct timespec start, end;
	long elapsed_nsec;
	int err, opt;
	unsigned long begin, fini, elapsed_cycles;
	unsigned long overhead;
	struct running_stats stats;
	double std;
	unsigned long nsec_variance;
	unsigned long buffer[32] = {0};
	unsigned long out_buffer[32] = {0};
	char *cpu_num;
	char *cpu_list;
	char *cpu_alt;
	cpu_set_t cpuset, cpu_as_set, alt_as_set;
	size_t cpusetsize = 0;
	unsigned int test_cpu;
	char curcpu[8];
	pthread_t alt_thread;
	pthread_attr_t alt_thread_attr;
	struct thread_shared_data *shared;
	int bar_ret, result;
	char *teststr;
	pstamp_t cause_pstamp;
	pstamp_ring_t *pstamp_ring;

	/* setup defaults */
	cpusetsize = (get_nprocs_conf() + 7) >> 3;
	err = getcpu(&test_cpu, NULL);
	err_exit_negative(err, "Can't get current CPU number\n", 0);
	/* default arguments based on current cpu */
	snprintf(curcpu, sizeof(curcpu), "%d", test_cpu);
	cpu_list = cpu_num = cpu_alt = curcpu;

	/*  parse arguments */
	while ((opt = getopt(argc, argv, "c:s:a:")) != -1) {
		switch (opt) {
		case 's':
			cpu_list = optarg;
			break;
		case 'c':
			cpu_num = optarg;
			break;
		case 'a':
			cpu_alt = optarg;
			break;
		default:
			fprintf(stderr, "Usage: %s [-c <cpu>] [-a <altcpu>] [-s <cpu-list>]\n", argv[0]);
			return 0;
		}
	}

	/* initially set the usable cpuset and cpus to test for testing */
	err = parse_cpu_list(cpu_list, &cpuset, cpusetsize);
	err_exit_negative(err, "Error parsing cpu list", 0);
	err = parse_cpu_single(cpu_num, &cpu_as_set, cpusetsize);
	err_exit_negative(err, "Error parsing cpu", 0);
	err = parse_cpu_single(cpu_alt, &alt_as_set, cpusetsize);
	err_exit_negative(err, "Error parsing alternate cpu", 0);
	CPU_OR_S(cpusetsize, &cpuset, &cpuset, &cpu_as_set);
	CPU_OR_S(cpusetsize, &cpuset, &cpuset, &alt_as_set);

	/* set affinity to unified cpuset, first */
	err = sched_setaffinity(0, cpusetsize, &cpuset);
	err_exit_negative(err, "Error setting sched affinity", 1);

	/* create alternate thread and common memory for tests involving thread communication */
	shared = malloc(sizeof(struct thread_shared_data));
	null_exit(shared, "Allocation failed", 1);
	memset(shared, '\0', sizeof(struct thread_shared_data));
	err = pthread_barrier_init(&shared->barrier1, NULL, 2);
	err_exit_nonzero(err, "Error initializing barrier1", 1);
	err = pthread_barrier_init(&shared->barrier2, NULL, 2);
	err_exit_nonzero(err, "Error initializing barrier2", 1);

	barrier_init(&shared->spin_barrier, 2);

	err = pthread_attr_init(&alt_thread_attr);
	err_exit_nonzero(err, "Error creating alternate thread attr", 1);
	err = pthread_attr_setaffinity_np(&alt_thread_attr, cpusetsize, &alt_as_set);
	err_exit_nonzero(err, "Error creating alternate thread's affinity", 1);
	err = pthread_attr_setdetachstate(&alt_thread_attr, PTHREAD_CREATE_DETACHED);
	err_exit_nonzero(err, "Error setting alternate thread's detached state", 1);
	err = pthread_create(&alt_thread, &alt_thread_attr, alt_thread_main, (void *)shared);
	err_exit_nonzero(err, "Error creating alternate thread", 1);
	err = pthread_attr_destroy(&alt_thread_attr);
	err_exit_nonzero(err, "Error destroying alternate thread attr", 1);
	
	/* further restrict this primary thread to running on a specific cpu in the cpuset */
	err = sched_setaffinity(0, cpusetsize, &cpu_as_set);
	err_exit_negative(err, "Error setting primary affinity", 1);

	/* Get TSC cycle frequency conversion constants */
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
	       "Timing sequences of individual instructions repeated 20 times\n"
	       "\n");

	/*
	 * measure single instruction. Clobbered registers must be described to tell C compiler to save them.
	 * %0 contains a pointer to some words on the stack.
	 */

	TIME_INSTRUCTION("rdtsc", buffer, out_buffer, "%rax", "%rdx");

	TIME_INSTRUCTION("lfence; rdtsc", buffer, out_buffer,  "%rax", "%rdx");

	TIME_INSTRUCTION("rdtscp", buffer, out_buffer, "%rax", "%rdx", "%rcx");

	TIME_INSTRUCTION("lfence", buffer, out_buffer);

	TIME_INSTRUCTION("pause", buffer, out_buffer);

	TIME_INSTRUCTION("nop", buffer, out_buffer);

	TIME_INSTRUCTION("inc %%rax", buffer, out_buffer, "%rax", "cc");

	TIME_INSTRUCTION("mov (%0),%%rdx", buffer, out_buffer, "%rdx");

	TIME_INSTRUCTION("mov %%rdx,(%1)", buffer, out_buffer);

	TIME_INSTRUCTION("sub %%rax, %%rax", buffer, out_buffer, "%rax", "cc");

	TIME_INSTRUCTION("mov $0,%%rax", buffer, out_buffer, "%rax");

	TIME_INSTRUCTION("cmpxchg %%rdx,(%1);", buffer, out_buffer, "memory", "cc");

	TIME_INSTRUCTION("lock cmpxchg %%rdx,(%1);", buffer, out_buffer, "memory", "cc");

	/* library function time tests */
	printf("\nTesting call, library, and syscall overhead\n\n");
	TIME_CODE_20(simple_call(););
	TIME_CODE(teststr = malloc(128););
	TIME_CODE(memset(teststr, 'x', 128); teststr[127] = '\0';);
	TIME_CODE(result = strlen(teststr););
	if (result != 127) exit(1); /* just to use the result of strlen */
	TIME_CODE(getpid(); /* syscall */);
	TIME_CODE(sched_yield(); /* syscall */);

	/* context switch tests */
	printf("\nContext switch among cores tests\n\n");
	printf("Switch affinity to current core\n");
	TIME_CODE(err = sched_setaffinity(0, cpusetsize, &cpu_as_set););
	printf("Switch affinity to current core again\n");
	TIME_CODE(err = sched_setaffinity(0, cpusetsize, &cpu_as_set););
	printf("\nSwitch affinity to alternate core\n");
	TIME_CODE(err = sched_setaffinity(0, cpusetsize, &alt_as_set););
	printf("Switch affinity back again\n");
	TIME_CODE(err = sched_setaffinity(0, cpusetsize, &cpu_as_set););
	printf("Switch affinity to alternate core\n");
	TIME_CODE(err = sched_setaffinity(0, cpusetsize, &alt_as_set););
	printf("Switch affinity back again\n");
	TIME_CODE(err = sched_setaffinity(0, cpusetsize, &cpu_as_set););

	/* Multi-thread shared data tests */
	printf("\nBegin multithread testing\n\n");
	bar_ret = pthread_barrier_wait(&shared->barrier1);
	if (bar_ret != PTHREAD_BARRIER_SERIAL_THREAD)
		err_exit_nonzero(bar_ret, "Error barrier1 wait failed.", 1);
	err = pthread_barrier_destroy(&shared->barrier1);
	err_exit_nonzero(err, "Error destroying barrier1", 1);

	/* Measure synchronization of main and alternate threads after barrier */
	barrier_wait(&shared->spin_barrier);

	shared->arrival1 = tsc_cycles();

	/* wait until both threads have recorded time of arrival */
	barrier_wait(&shared->spin_barrier);

	elapsed_cycles = (shared->arrival1 > shared->arrival2) ? shared->arrival1 - shared->arrival2 :
		shared->arrival2 - shared->arrival1;
	printf("Barrier sync arrival difference is main-alt (%lu cycles) %lu nsec\n\n", elapsed_cycles,
	       tsc_cycles_to_ns(elapsed_cycles, &ns_adjust));

	barrier_wait(&shared->spin_barrier);

	while((begin = shared->timestamp1) == 0) asm volatile("lfence;");
	fini = tsc_cycles();
	shared->timestamp1 = 0;
	elapsed_cycles = fini - begin;
	printf("Shared memory ping takes (%lu cycles) %ld nsec\n", elapsed_cycles,
	       tsc_cycles_to_ns(elapsed_cycles, &ns_adjust));

	barrier_wait(&shared->spin_barrier);
	/* write timestamp to shared variable while the other is waiting*/
	asm volatile("pause\n"); /* suspend hyperthread briefly */
	shared->timestamp2 = tsc_cycles();
	asm volatile("pause\n"); /* suspend hyperthread briefly*/

	barrier_wait(&shared->spin_barrier);

	/* time simple pstamp logging */
	pstamp_ring = (pstamp_ring_t *)malloc(pstamp_ring_size(1024));
	null_exit(pstamp_ring, "Error allocating pstamp ring", 1);
	pstamp_ring_init(pstamp_ring, 1024);

	TIME_CODE_20(pstamp(0, &cause_pstamp););
	TIME_CODE_20(pstamp_log(pstamp_ring, 1, &cause_pstamp););
	
	free(pstamp_ring);

	bar_ret = pthread_barrier_wait(&shared->barrier2);
	if (bar_ret != PTHREAD_BARRIER_SERIAL_THREAD)
		err_exit_nonzero(bar_ret, "Error barrier2 wait failed.", 1);
	err = pthread_barrier_destroy(&shared->barrier2);
	err_exit_nonzero(err, "Error destroying barrier2", 1);

	printf("Main thread finished.\n");
	sleep(1);

	return 0;
}

static void *alt_thread_main(void *arg)
{
	int bar_ret;
	unsigned long begin, fini, elapsed_cycles;
	struct thread_shared_data *shared = (struct thread_shared_data *)arg;

	bar_ret = pthread_barrier_wait(&shared->barrier1);
	if (bar_ret != PTHREAD_BARRIER_SERIAL_THREAD)
		err_exit_nonzero(bar_ret, "Error barrier1 wait failed.", 1);


	/* spin barrier */
	barrier_wait(&shared->spin_barrier);
	shared->arrival2 = tsc_cycles();

	/* main thread reinitializes barrier1 */
	barrier_wait(&shared->spin_barrier);
	/* main thread prints difference in arrival times */

	barrier_wait(&shared->spin_barrier);

	/* write timestamp to shared variable while the other is waiting*/
	asm volatile("pause\n"); /* suspend hyperthread briefly*/
	shared->timestamp1 = tsc_cycles(); /* PING main */
	asm volatile("pause\n"); /* suspend hyperthread briefly*/
	barrier_wait(&shared->spin_barrier);

	while((begin = shared->timestamp2) == 0) asm volatile("lfence;");
	fini = tsc_cycles();
	shared->timestamp2 = 0;
	elapsed_cycles = fini - begin;
	printf("Shared memory pong takes (%lu cycles) %ld nsec\n", elapsed_cycles,
	       tsc_cycles_to_ns(elapsed_cycles, &ns_adjust));
	
	barrier_wait(&shared->spin_barrier);

	/* here the main program is timing pstamp logging while we wait for it. */
	
	printf("\nAlternate thread finished.\n");
	bar_ret = pthread_barrier_wait(&shared->barrier2);
	if (bar_ret != PTHREAD_BARRIER_SERIAL_THREAD)
		err_exit_nonzero(bar_ret, "Error barrier2 wait failed.", 1);

	pthread_exit(NULL);
}
