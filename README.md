# Experimental precision timing measurements

This is a personal project to capture timing on x86_64 systems at TSC resolution.

It uses a number of techniques, focused on using RDTSCP to read the precise clock time.

In building it, I discovered that clock_gettime(CLOCK_REALTIME), despite being implemented in vDSO so a kernel call is not needed, is quite slow compared to reading the TSC and converting it to nanoseconds directly(!), perhaps because the VDSO code is written to be portable across architectures. In particular, it handles issues that never arise on modern Intel and AMD cpus, as far as I know, and seems to share most of its code with ARM and other architectures.

So all measurements done are use raw TSC readings, which are both very fast and whose timebase is accurate across all threads in the system. [on multi-socket systems, TSC's may drift slightly between the cores on different sockets, but they are synchronized at boot time and run off the same clock source] The conversion between TSC frequency and real time is done using a multiplier and shift taken from the `perf` kernel driver information, which is accurate to at least 32 bits in a second-long interval.

Where intervals are short, as in measuring single instructions, the instruction sequence includes 20 copies of the same instruction, and the results are divided by 20. Also the overhead of timing is measured and its mean and standard deviation are calculated. The mean overhead is subtracted in all measurements.

The `getpid()` syscall stands in as a benchmark for the minimal system call overhead.

It is recommended, to eliminate noise, that the test be run on isolated cores, having booted the system with the `isolccpus` kernel parameter. Otherwise, kernel threads and other application threads may preempt the test. On my machine I typically run the test with parameters `-c 6 -a 7' which runs the program's main thread on core 6, and its other thread on core 7. The alternate thread is used in multiprocessing timing tests, like shared memory polling delay.

To measure context switch time, the time taken for the main thread to go through sched_yield() is tested, and also the time to move the thread from one core to another is tested by changing the thread affiliation.

# Building

Typing `make` in the root directory currently builds the program in the `build` subdirectory. It currently names the file built `a.out` because I haven't given it a better name. See the makefile for how to change the TARGET.

# Roadmap

One key goal is to also measure timing of microbenchmarks in the kernel, where they are different, especially shared-memory between user and kernel. This will be done by creating a safe kernel module that includes the tests and uses the TSC based timing method to report results.
