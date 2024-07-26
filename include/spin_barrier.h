/*
 * simple multi-thread spin-lock based barrier. Originally coded
 * by Chris Wellons. (https://nullprogram.com/blog/2022/03/13/)
 * Turned into a library and changed to use its own typedef to hide
 * representation. Also changed logic to allow non-power-of-two number of
 * threads.
 * Copyright (c) 2024 David P. Reed. 
 */
#ifndef _SPIN_BARRIER_H_
#define _SPIN_BARRIER_H_

/*
 * smallest power of 2 >= x, adapted algorithm from
 * Hacker's Delight (Second Edition) by Henry S. Warren, Jr.
 */
static inline unsigned clp2(unsigned x)
{
	x -= 1;
	/* trick: extend high order bit rightwards */
	x |= (x >> 1);
	x |= (x >> 2);
	x |= (x >> 4);
	x |= (x >> 8);
	x |= (x >> 16);
	return x + 1;
}

typedef struct barrier {
	unsigned word;	/* init to count - flp2(count)  */
	unsigned n; 		/* least power of two >= count */
	unsigned reset;		/* value to add back to reset counter */
} barrier_t;

#if __GNUC__
#define BARRIER_INC(x) __atomic_add_fetch(x, 1, __ATOMIC_SEQ_CST)
#define BARRIER_ADD(x, n) __atomic_add_fetch(x, n, __ATOMIC_SEQ_CST) 
#define BARRIER_GET(x) __atomic_load_n(x, __ATOMIC_SEQ_CST)
#else
#error Compiler missing __atomic_add_fetch and __atomic_load_n directives
#endif

/*
 * Spin-lock barrier object for count threads
 * Initialize n to smallest power of 2 >= count
 * Initialize barrier->word to n - count
 * Initialize reset to n - count
 * (This may only be called before any wait is done on the barrier)
 */
static inline void barrier_init(barrier_t *barrier, unsigned count)
{
	barrier->n = clp2(count);
	barrier->word = barrier->reset = barrier->n - count;
}

/*
 * Self-resetting barrier wait.
 * Low bits count up, and overflow to change "phase" in high order bits.
 * The last process through the barrier is the one that carries into
 * the "phase". It reinitializes the barrier-count to the initial value by an atomic
 * add.
 */

static inline void barrier_wait(barrier_t *barrier)
{
	/* relies on n being power of 2, as set by barrier_init */
    unsigned v = BARRIER_INC(&barrier->word);
    unsigned n = barrier->n;
    if (v & (n - 1)) {
	    for (v &= n; (BARRIER_GET(&barrier->word) & n) == v;)
		    asm volatile("lfence;"); /* pause to allow other hyperthread to run */
    } else if (barrier->reset) 	/* non-power-of-two case requires pre-adding initial value */
	    BARRIER_ADD(&barrier->word, barrier->reset); 
}

#endif
