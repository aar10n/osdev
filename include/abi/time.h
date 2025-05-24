//
// Created by Aaron Gill-Braun on 2023-07-07.
//

#ifndef INCLUDE_ABI_TIME_H
#define INCLUDE_ABI_TIME_H

#define ITIMER_REAL    0
#define ITIMER_VIRTUAL 1
#define ITIMER_PROF    2

struct itimerval {
	struct timeval it_interval;
	struct timeval it_value;
};

struct timezone {
	int tz_minuteswest;
	int tz_dsttime;
};

struct tm {
  int tm_sec; /* seconds */
  int tm_min; /* minutes */
  int tm_hour; /* hours */
  int tm_mday; /* day of the month */
  int tm_mon; /* month */
  int tm_year; /* year */
  int tm_wday; /* weekday */
  int tm_yday; /* day in the year */
  int tm_isdst; /* daylight saving time */
  long tm_gmtoff; /* offset from UTC in seconds */
  const char *tm_zone; /* timezone abbreviation */
};

#endif
