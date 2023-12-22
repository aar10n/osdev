//
// Created by Aaron Gill-Braun on 2023-07-07.
//

#ifndef INCLUDE_ABI_RESOURCE_H
#define INCLUDE_ABI_RESOURCE_H

#include <bits/resource.h>

typedef unsigned long long rlim_t;

struct rlimit {
  rlim_t rlim_cur;
  rlim_t rlim_max;
};

struct rusage {
  struct timeval ru_utime; /* user time used */
  struct timeval ru_stime; /* system time used */
  /* linux extentions, but useful */
  long	ru_maxrss; /* max resident set size */
  long	ru_ixrss; /* integral shared memory size */
  long	ru_idrss; /* integral unshared data size */
  long	ru_isrss; /* integral unshared stack size */
  long	ru_minflt; /* page reclaims (soft page faults) */
  long	ru_majflt; /* page faults (hard page faults) */
  long	ru_nswap; /* swaps */
  long	ru_inblock; /* block input operations */
  long	ru_oublock; /* block output operations */
  long	ru_msgsnd; /* IPC messages sent */
  long	ru_msgrcv; /* IPC messages received */
  long	ru_nsignals; /* signals received */
  long	ru_nvcsw; /* voluntary context switches */
  long	ru_nivcsw; /* involuntary context switches */
  /* room for more... */
  long    __reserved[16];
};

#endif
