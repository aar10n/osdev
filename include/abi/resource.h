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
  struct timeval ru_utime;
  struct timeval ru_stime;
  /* linux extentions, but useful */
  long	ru_maxrss;
  long	ru_ixrss;
  long	ru_idrss;
  long	ru_isrss;
  long	ru_minflt;
  long	ru_majflt;
  long	ru_nswap;
  long	ru_inblock;
  long	ru_oublock;
  long	ru_msgsnd;
  long	ru_msgrcv;
  long	ru_nsignals;
  long	ru_nvcsw;
  long	ru_nivcsw;
  /* room for more... */
  long    __reserved[16];
};

#endif
