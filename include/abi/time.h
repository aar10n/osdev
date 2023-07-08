//
// Created by Aaron Gill-Braun on 2023-07-07.
//

#ifndef INCLUDE_ABI_TIME_H
#define INCLUDE_ABI_TIME_H

struct itimerval {
	struct timeval it_interval;
	struct timeval it_value;
};

struct timezone {
	int tz_minuteswest;
	int tz_dsttime;
};

#endif
