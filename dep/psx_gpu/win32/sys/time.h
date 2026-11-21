#pragma once

#include <time.h>

struct timeval
{
  long tv_sec;
  long tv_usec;
};

static inline int gettimeofday(struct timeval* tv, void* tz)
{
  (void)tz;
  struct timespec ts;
  timespec_get(&ts, TIME_UTC);
  tv->tv_sec = (long)ts.tv_sec;
  tv->tv_usec = (long)(ts.tv_nsec / 1000);
  return 0;
}
