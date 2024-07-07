/*
 * Wexford's algorithm for computing running average and variance
 * Copyright (c) David P. Reed. All rights reserved.
 */

#ifndef _RUNNNING_AVERAGE_H_
#define _RUNNNING_AVERAGE_H_

struct running_stats {
	unsigned long samples;
	double mean;
	double m2;
};

static inline void running_stats_init(struct running_stats *stats)
{
	stats->samples = 0;
	stats->mean = 0.0;
	stats->m2 = 0;
}

static inline void running_stats_sample(struct running_stats *stats, double new)
{
	double delta = new - stats->mean;
	stats->samples += 1;
	stats->mean += delta / stats->samples;
	stats->m2 += (new - stats->mean) * delta;	
}

static inline unsigned long running_stats_samples(struct running_stats *stats)
{
	return stats->samples;
}

static inline double running_stats_mean(struct running_stats *stats)
{
	return stats->mean;
}

static inline double running_stats_variance(struct running_stats *stats)
{
	/* NOTE: 0.0/0.0 should not trap in C. Produces a quiet-NaN. */
	return (stats->samples > 1) ? stats->m2 / stats->samples : 0.0 / 0.0 ;
}

static inline double running_stats_sample_variance(struct running_stats *stats)
{
	/* NOTE: 0.0/0.0 should not trap in C. Produces a quiet-NaN. */
	return (stats->samples > 2) ? stats->m2 / (stats->samples - 1) : 0.0 / 0.0 ;
}
#endif
