/*
 * Prototype for performance timestamping logging.
 *
 * Copyright (c) 2024 David P. Reed. All rights reserved.
 */
#ifndef _PSTAMP_H_
#define _PSTAMP_H_
/*
 * Very low-overhead mechanism for performance timestamping and logging of events in both user and kernel.
 * Uses x86 TSC to generate precise, monotonic timestamps with identifiers.
 *
 * a pstamp_t is a precise capture of an "instant event", consisting of what logical processor
 * (core/hyperthread), at what point in the code of a system, at what cycle.
 *
 * a logged pstamp consists of two parts: a "cause" which is a prior pstamp, which is passed to all
 * subsequent logged pstamps in a log. This pair allows all the events due to a particular cause to
 * be identified and put in order. So an entire causal chain can be linked together.
 *
 * A simple pstamp log is just a ring buffer of logged pstamps. To make it as fast as possible, the caller must
 * ensure that the recording of the pstamp can't be preempted by any activity that would concurrently
 * record a log. So, in the simplest case, each logical processor must have a private, per core log.
 * Logs can be merged in a sorted order by picking the oldest logged entry first.
 *
 * It might be tempting to have logs be shared across logical processors, but the overhead of even "lock free"
 * logging would be significant. So, instead it is highly recommended that to increase the size of a log, merging
 * be used! This eliminates "false sharing" to a very large extent!
 * The simple log counts "lost" log events, and throws away the oldest, just by wrapping around. If lost events
 * are a problem, the simple log can be expanded by adding new rings for newer entries whenever the log becomes full.
 * once such a log is full, it can be passed to some activity that may eventually merge it.
 * If a new log ring is not available (the system is out of memory), the log ring will be discarded.
 *
 * Thus, the simple log has ultra-minimal logging overhead, with rarely, slightly longer "moves" to a new segment, but
 * it never waits for memory. The assumption is made that a consumer of the log is always running semi-concurrently
 * and allocating a new ring before the current ring is empty, if no entries are to be lost. This is
 * double-buffering of rings.
 *
 * A simple merging operation for logs is provided here that enumerates multiple logs in pstamp order.
 */

#include <stdbool.h>

/* timestamp taken at an enumerated point on logical processor at a particular time instant */
typedef struct pstamp {
	int point;
	int logical_processor;
	unsigned long time;
} pstamp_t;

typedef struct pstamp_log {
	pstamp_t pstamp;
	pstamp_t cause;
} pstamp_log_t;

typedef struct pstamp_ring {
	struct pstamp_ring *next_ring;
	unsigned int size;
	unsigned int next;
	unsigned int end;
	bool inactive;	/* set when recording has moved to next ring */
	unsigned long overflows;
	pstamp_log_t ring[];
} pstamp_ring_t;

static inline void pstamp(int point, pstamp_t *pstamp)
{
	unsigned long d, a, c;
	asm volatile("rdtscp;" : "=a"(a), "=d"(d), "=c"(c));
	pstamp->time = (d << 32) | a; 
	pstamp->logical_processor = c;
	pstamp->point = point;
}

static inline void log_pstamp(int point, const pstamp_t *cause, pstamp_log_t *pstamp_log)
{
	pstamp(point, &pstamp_log->pstamp);
	pstamp_log->cause = *cause;
}

/* size of memory for a particular pstamp_ring size, if we want to allocate it dynamically */
#define pstamp_ring_size(size) (sizeof(pstamp_ring_t) + sizeof(pstamp_log_t) * size)

/* initialize pstamp ring buffer in memory */
static inline void pstamp_ring_init(pstamp_ring_t *pstamp_ring, int size)
{
	pstamp_ring->next_ring = NULL;
	pstamp_ring->next = pstamp_ring->overflows = 0;
	pstamp_ring->size = pstamp_ring->end = size;
	pstamp_ring->inactive = false;
}

/* avoid ring modulo division cost by comparing with n with size and wrapping to zero */
static inline unsigned int _wrap(unsigned int n, unsigned int size)
{
	return n < size? n : 0;
}

/* log, and perhaps change the pointer to the ring */
static inline pstamp_ring_t *pstamp_log(pstamp_ring_t *pstamp_ring, int point, const pstamp_t *cause)
{
	/* if  full ring */
	if (pstamp_ring->next == pstamp_ring->end) {
		/* if no next ring, overwrite, else move to next ring */
		if (pstamp_ring->next_ring == NULL)
			pstamp_ring->end = pstamp_ring->next = _wrap(pstamp_ring->next + 1, pstamp_ring->size);
		else {
			pstamp_ring-> inactive = true;
			pstamp_ring = pstamp_ring->next_ring;
		}
	}
	pstamp_ring->next = _wrap(pstamp_ring->next + 1, pstamp_ring->size);
	log_pstamp(point, cause, pstamp_ring->ring + pstamp_ring->next);
	/* return current (may be next) ring */
	return pstamp_ring;
}

/*
 * add an extra ring to a pstamp ring before it overflows, returns true if extended,
 * false if already extended.
 * The next_ring must be initialized before this call
 * Overflows may happen during the call, but otherwise this is safe. 
 */
static inline bool pstamp_log_extend(pstamp_ring_t *pstamp_ring, pstamp_ring_t *next_ring)
{
	bool ok = false;
	if (!pstamp_ring->inactive && pstamp_ring->next_ring == NULL) {
	    pstamp_ring->next_ring = next_ring;
	    ok = true;
	}
	return ok;
}

/*
 * calls that can observe the pstamp_log while it is in use or inactive.
 */

/* capture the number of overflows that have happened in this part of the log so far */
static inline unsigned long pstamp_log_overflows(pstamp_ring_t *pstamp_ring)
{
	return pstamp_ring->overflows;
}

static inline bool pstamp_log_extended(pstamp_ring_t *pstamp_ring)
{
	return pstamp_ring->next_ring != NULL;
}

/*
 * Enumerate current log entries in order, calling a callback per entry
 * If log is concurrently updated, overflows may overwrite log entries, but
 * at most size entries will be enumerated.
 * to avoid interference due to overwriting, only enumerate log if it has been extended or
 * is inactive. 
 */
static inline void pstamp_log_enumerate(pstamp_ring_t *pstamp_ring, void (*callback)(pstamp_log_t *pstamp_log))
{
	/* snapshot the ring pointers */
	int size = pstamp_ring->size;
	int last = pstamp_ring->next;
	int start = _wrap(pstamp_ring->end + 1, size);
	
	for (int i = start;
	     i != last;
	     i = _wrap(i + 1, size)) {
		callback(pstamp_ring->ring + i);
	}
}


#endif
