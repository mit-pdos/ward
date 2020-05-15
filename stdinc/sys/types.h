#pragma once
// Partial implementation of IEEE Std 1003.1-2008

#include <stddef.h>
#include <stdint.h>

typedef ptrdiff_t ssize_t;
typedef ssize_t off_t;

typedef uint64_t dev_t;
typedef uint64_t ino_t;
typedef uint64_t nlink_t;
typedef uint32_t mode_t;
typedef int pid_t;

typedef long time_t;
typedef long suseconds_t;
