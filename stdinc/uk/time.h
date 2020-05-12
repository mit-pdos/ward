#pragma once

#define CLOCK_REALTIME              0
#define CLOCK_MONOTONIC             1
#define CLOCK_PROCESS_CPUTIME_ID    2
#define CLOCK_THREAD_CPUTIME_ID     3
#define CLOCK_MONOTONIC_RAW         4
#define CLOCK_REALTIME_COARSE       5
#define CLOCK_MONOTONIC_COARSE      6
#define CLOCK_BOOTTIME              7
#define CLOCK_REALTIME_ALARM        8
#define CLOCK_BOOTTIME_ALARM        9

typedef int time_t;
typedef int clockid_t;

struct tm {
  int tm_sec;         /* seconds */
  int tm_min;         /* minutes */
  int tm_hour;        /* hours */
  int tm_mday;        /* day of the month */
  int tm_mon;         /* month */
  int tm_year;        /* year */
  int tm_wday;        /* day of the week */
  int tm_yday;        /* day in the year */
  int tm_isdst;       /* daylight saving time */
};

struct timespec {
  time_t   tv_sec;        /* seconds */
  long     tv_nsec;       /* nanoseconds */
};

BEGIN_DECLS

// These math functions are shared by user space and the kernel
struct tm *gmtime_r(const time_t *timep, struct tm *result);
struct tm *localtime_r(const time_t *timep, struct tm *result);
time_t mktime(struct tm *tm);
long clock_gettime(int clk_id, struct timespec* tp);

END_DECLS
