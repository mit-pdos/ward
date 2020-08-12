#include "types.h"
#include "kernel.hh"
#include "amd64.h"
#include "fs.h"
#include "file.hh"
#include "major.h"
#include "kstream.hh"

#include <algorithm>
#include <uk/time.h>
#include <sys/time.h>
#include <errno.h>

#define IO_RTC  0x70

// The UNIX epoch time, in nanoseconds, when nsectime() was 0.
static uint64_t rtc_nsec0 __attribute__ ((section(".qdata")));

static uint8_t
rtcread1(uint8_t reg, bool bcd = false, bool twelvehour = false)
{
  outb(IO_RTC, reg);
  uint8_t val = inb(IO_RTC+1);
  bool pm = twelvehour && (val & 0x80);
  if (twelvehour)
    val &= ~0x80;
  if (bcd)
    val = ((val >> 4) * 10) + (val & 0xF);
  if (twelvehour && val == 12)
    val = 0;
  if (pm)
    val += 12;
  return val;
}

static time_t
rtcread()
{
  struct tm x{}, y{};
  bool stable = false;
  uint8_t statusB = rtcread1(0xB);
  bool bcd = !(statusB & (1 << 2));
  bool twelvehour = !(statusB & (1 << 1));

  // The time may change while we're reading it, so retry until we get
  // the same value twice.
  for (int i = 0; i < 10 && !stable; ++i) {
    // Wait for "update-in-progress" flag to be clear.
    int j;
    for (j = 0; rtcread1(0x0A) & (1<<7); ++j) {
      if (j == 10) {
        return ((time_t)-1);
      }
      microdelay(1000);
    }
    x.tm_sec = rtcread1(0, bcd);
    x.tm_min = rtcread1(2, bcd);
    x.tm_hour = rtcread1(4, bcd, twelvehour);
    x.tm_mday = rtcread1(7, bcd);
    x.tm_mon = rtcread1(8, bcd) - 1;
    x.tm_year = 2000 + rtcread1(9, bcd);
    stable = i > 0 && memcmp(&x, &y, sizeof x) == 0;
    y = x;
  }
  if (!stable) {
    return ((time_t)-1);
  }

  // mktime expects local time
  x.tm_sec += RTC_TZ_SECS;      // To UTC
  x.tm_sec -= TZ_SECS;          // To local time

  long epoch_year = x.tm_year - 1970;
  long month_offset[2][12] = {{0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334},
                              {0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335}};

  return x.tm_sec + x.tm_min * 60 + x.tm_hour * 3600
    + (x.tm_mday + month_offset[x.tm_year%4 ? 0 : 1][x.tm_mon]) * 3600 * 24
    + (epoch_year*365 + (epoch_year-3)/4) * 3600 * 24;
}

void
initrtc(void)
{
  // Read the current RTC value and the current nsec time to get the
  // correspondence between wall-clock time and the nsec epoch.
  time_t rtc_now = rtcread();
  if (rtc_now == (time_t)-1)
    panic("Failed to read initial RTC value");
  uint64_t nsectime_now = nsectime();

  rtc_nsec0 = rtc_now * 1000000000ull - nsectime_now;
}

//SYSCALL
uint64_t
sys_time_nsec(void)
{
  // Return the number of nanoseconds since the UNIX epoch
  return rtc_nsec0 + nsectime();
}

//SYSCALL
time_t
sys_time(userptr<time_t> tloc)
{
  time_t t = (rtc_nsec0 + nsectime()) / 1000000000ull;

  if(tloc && !tloc.store(&t))
    return -EFAULT;
  return t;
}

//SYSCALL
long
sys_gettimeofday(userptr<struct timeval> tv, userptr<struct timezone> tz)
{
  if(tz)
    return -1;

  timeval time;
  u64 nsec = rtc_nsec0 + nsectime();
  time.tv_sec = nsec / 1000000000;
  time.tv_usec = (nsec % 1000000000) / 1000;

  if(!tv.store(&time))
    return -1;
  return 0;
}

//SYSCALL
long
sys_clock_gettime(int clk_id, userptr<struct timespec> tp) {
  STRACE_PARAMS("%d, %p", clk_id, tp.unsafe_get());

  timespec time;

  switch (clk_id) {
  case CLOCK_REALTIME:
  case CLOCK_MONOTONIC:
  case CLOCK_MONOTONIC_RAW:
  case CLOCK_REALTIME_COARSE:
  case CLOCK_MONOTONIC_COARSE:
  {
    u64 nsec = rtc_nsec0 + nsectime();
    time.tv_sec = nsec / 1000000000;
    time.tv_nsec = nsec % 1000000000;
    break;
  }
  default:
    return -EINVAL;
  };

  if(!tp.store(&time))
    return -EFAULT;
  return 0;
}
