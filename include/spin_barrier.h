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
 * Hacker's Dictionary by Henry S. Warren, Jr.
 */
static inline unsigned flp2(unsigned x)
{
	x |= (x >> 1);
	x |= (x >> 2);
	x |= (x >> 4);
	x |= (x >> 8);
	x |= (x >> 16);
	return x - (x >> 1);
}

typedef struct barrier {
	unsigned word;	/* init to count - flp2(count)  */
	unsigned n; 		/* least power of two >= count */
} barrier_t;

#if __GNUC__
#  define BARRIER_INC(x) __atomic_add_fetch(x, 1, __ATOMIC_SEQ_CST)
#  define BARRIER_GET(x) __atomic_load_n(x, __ATOMIC_SEQ_CST)
#elif _MSC_VER
#  define BARRIER_INC(x) _InterlockedIncrement(x)
#  define BARRIER_GET(x) _InterlockedOr(x, 0)
#endif

// Spin-lock barrier for n threads
// Initialize barrier->word to n - count
// Initialize n to smallest power of 2 >= count
static inline void barrier_init(barrier_t *barrier, unsigned count)
{
	barrier->n = flp2(count);
	barrier->word = barrier->n - count;
}

static inline void barrier_wait(barrier_t *barrier)
{
	/* relies on n being power of 2, as set by barrier_init */
    unsigned v = BARRIER_INC(&barrier->word);
    unsigned n = barrier->n;
    if (v & (n - 1)) {
        for (v &= n; (BARRIER_GET(&barrier->word) & n) == v;);
    }
}

#endif
