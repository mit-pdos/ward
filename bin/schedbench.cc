#include <errno.h>
#include <atomic>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <pthread.h>
#include "assert.h"
#include <linux/futex.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <sched.h>
#include <unistd.h>

void nsleep(uint64_t t) {
  timespec ts;
  ts.tv_nsec = t % 1000000000;
  ts.tv_sec = t / 1000000000;
  nanosleep(&ts, nullptr);
}
int futex(uint32_t *uaddr, int futex_op, int val, uint64_t timeout) {
  timespec ts;
  ts.tv_nsec = timeout % 1000000000;
  ts.tv_sec = timeout / 1000000000;
  if(timeout == 0)
    ts.tv_sec = 0xffffffff;

  int r = syscall(SYS_futex, uaddr, futex_op, val, &ts);
  return r < 0 ? errno : r;
}

static volatile std::atomic<uint64_t> waiting;
static volatile std::atomic<uint64_t> waking __attribute__((unused));
static int iters;
static int nworkers;
static volatile int go;

pthread_t* worker_threads;

static struct {
  uint32_t mem;
  __padout__;
} ftx[256] __mpalign__;

#define die(...) do { \
  printf( __VA_ARGS__ ); \
  exit(-1); \
} while(0)

static
void* worker0(void* x)
{
  // Ping pong a futex between a pair of workers
  uint64_t id = (uint64_t)x;
  uint32_t* f = &(ftx[id>>1].mem);
  long r;

  // setaffinity(id);

  while (go == 0)
    sched_yield();

  if (id & 0x1) {
    for (uint64_t i = 0; i < iters; i++) {
      r = futex(f, FUTEX_WAIT_PRIVATE, (uint64_t)(i<<1), 0);
      if (r < 0 && r != -EWOULDBLOCK)
        die("futex: %ld\n", r);
      *f = (i<<1)+2;
      r = futex(f, FUTEX_WAKE_PRIVATE, 1, 0);
    }
  } else {
    for (uint64_t i = 0; i < iters; i++) {
      *f = (i<<1)+1;
      r = futex(f, FUTEX_WAKE_PRIVATE, 1, 0);
      r = futex(f, FUTEX_WAIT_PRIVATE, (uint64_t)(i<<1)+1, 0);
      if (r < 0 && r != -EWOULDBLOCK)
        die("futex: %ld\n", r);
    }
  }

  return nullptr;
}

static
void master0(void)
{
  go = 1;
  for (int i = 0; i < nworkers; i++)
    pthread_join(worker_threads[i], nullptr);
}

int
main(int ac, char** av)
{
  long r;

  if (ac == 1) {
    iters = 500000;
    nworkers = 2;
  } else if (ac < 3){
    die("usage: %s iters nworkers", av[0]);
  } else {
    iters = atoi(av[1]);
    nworkers = atoi(av[2]);
  }

  worker_threads = (pthread_t*)malloc(nworkers * sizeof(pthread_t));

  waiting.store(0);

  for (int i = 0; i < nworkers; i++) {
    r = pthread_create(&worker_threads[i],
                       nullptr, worker0, (void*)(uint64_t)i);
    if (r < 0)
      die("pthread_create");
  }
  nsleep(1000*1000);

  struct timespec start, end;
  clock_gettime(CLOCK_REALTIME, &start);
  master0();
  clock_gettime(CLOCK_REALTIME, &end);

  unsigned long delta = (end.tv_sec - start.tv_sec) * 1000000000UL +
    (unsigned long)end.tv_nsec - (unsigned long)start.tv_nsec;
  printf("%lu ns/iter\n", delta/iters);
  fflush(stdout);
  return 0;
}
