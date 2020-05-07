#pragma once

#include "compiler.h"
#include <sys/types.h>
#include <uk/time.h>

struct timeval
{
  time_t tv_sec;
  suseconds_t tv_usec;
};

struct timezone
{
  // Not implemented
};
