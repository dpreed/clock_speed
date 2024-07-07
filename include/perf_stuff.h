/*
 * Linux provides access to the perf event system through a file descriptor.
 * but it is not exported by a library, so the file descriptor has to be
 * opened by a syscall. This inlines the code from the man page perf_event_open
 * that documents that interface.
 *
 * Copyright 2024 David P. Reed. All rights reserved.
 */

#ifndef _PERF_EVENT_H_
#define _PERF_EVENT_H_
#include <linux/perf_event.h>
#include <sys/syscall.h>
#include <unistd.h>

static inline int perf_event_open(struct perf_event_attr *hw_event, pid_t pid,
				  int cpu, int group_fd, unsigned long flags)
{
	return syscall(SYS_perf_event_open, hw_event, pid, cpu,
		       group_fd, flags);
}


#endif 
