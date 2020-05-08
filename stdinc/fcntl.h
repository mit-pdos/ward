#pragma once

#include "compiler.h"
#include <uk/fcntl.h>
#include <sys/types.h>

// Values for the second argument to `fcntl'.
#define F_DUPFD  0  // Duplicate file descriptor.
#define F_GETFD  1  // Get file descriptor flags.
#define F_SETFD  2  // Set file descriptor flags.
#define F_GETFL  3  // Get file status flags.
#define F_SETFL  4  // Set file status flags.
#define F_GETLK  5  // Get record locking info
#define F_SETLK  6  // Set record locking info (non-blocking)
#define F_SETLKW 7  // Set record locking info (blocking)

BEGIN_DECLS

int open(const char*, int, ...);
long openat(int, const char *, int, ...);

END_DECLS
