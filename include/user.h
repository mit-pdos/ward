#include <stdarg.h>
#include <assert.h>
#include <sys/types.h>

BEGIN_DECLS
struct stat;
struct sockaddr;

#include "sysstubs.h"

// ulib.c
char* gets(char*, int max);

// uthread.S
int forkt(void *sp, void *pc, void *arg, int forkflags);
void forkt_setup(u64 pid, void* stacktop);
void munmap_and_exit(void* base, u64 len);

END_DECLS
