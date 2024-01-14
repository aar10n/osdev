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

struct tms {
  clock_t tms_utime;  /* user time */
  clock_t tms_stime;  /* system time */
  clock_t tms_cutime; /* user time of children */
  clock_t tms_cstime; /* system time of children */
  /* times are in ticks */
};

#define CLOCKS_PER_SEC 1000000L /* US_PER_SEC */

#endif
