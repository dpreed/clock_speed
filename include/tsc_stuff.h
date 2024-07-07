#ifndef _TSC_STUFF_H_
#define _TSC_STUFF_H_

static inline unsigned long tsc_cycles(void)
{
	
	unsigned long cycles_low, cycles_high, cpu;
	asm volatile("rdtscp;" : "=a"(cycles_low), "=d"(cycles_high), "=c"(cpu));
	return (cycles_high << 32) | cycles_low;
}

/* ARM processsor cycle count reading */
#if 0
static inline unsigned long cycles(void)
{
	unsigned long result;
	asm volatile("mrs %0, PMCCNTR_EL0;" : "=r"(result));
	return result;
}
#endif

#endif
