#define LWIP_TIMEVAL_PRIVATE 0

#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define PAGE_SIZE 4096

#ifdef HW_linux
  #include <sys/syscall.h>
  #include <stdint.h>
  typedef uint32_t u32;
  typedef uint64_t u64;
  const int MITIGATION_STYLES = 1;
#else /* HW_linux */
  #include "sysstubs.h"

  const int MITIGATION_STYLES = 3;
#endif /* HW_linux */

void assert(bool b) {
  if(!b) {
    printf("Assertion failed!\n");
    exit(-1);
  }
}

static inline u64 start_timer() {
  u32 cycles_low, cycles_high;
  asm volatile ("CPUID\n\t"
                "RDTSC\n\t"
                "mov %%edx, %0\n\t"
                "mov %%eax, %1\n\t"
                : "=r" (cycles_high), "=r" (cycles_low)
                :: "%rax", "%rbx", "%rcx", "%rdx");
  return ((u64)cycles_high << 32) | (u64)cycles_low;
}

static inline u64 end_timer() {
  u32 cycles_low, cycles_high;
  asm volatile("RDTSCP\n\t"
               "mov %%edx, %0\n\t"
               "mov %%eax, %1\n\t"
               "CPUID\n\t"
               : "=r" (cycles_high), "=r" (cycles_low)
               :: "%rax", "%rbx", "%rcx", "%rdx");
  return ((u64)cycles_high << 32) | (u64)cycles_low;
}

void set_mitigations(int style) {
#ifndef HW_linux
  switch(style) {
  case 0:
    ward_cmdline_change_param("lazy_barrier", "yes");
    ward_cmdline_change_param("spectre_v2", "no");
    ward_cmdline_change_param("kpti", "no");
    ward_cmdline_change_param("mds", "no");
    break;
  case 1:
    ward_cmdline_change_param("lazy_barrier", "no");
    ward_cmdline_change_param("spectre_v2", "yes");
    ward_cmdline_change_param("kpti", "yes");
    ward_cmdline_change_param("mds", "yes");
    break;
  case 2:
    ward_cmdline_change_param("lazy_barrier", "yes");
    ward_cmdline_change_param("spectre_v2", "yes");
    ward_cmdline_change_param("kpti", "yes");
    ward_cmdline_change_param("mds", "yes");
    break;
  };
#endif
}

void one_line_test(u64 (*f)(), int iter, const char* name){
  if (iter < 1) iter = 1;

  printf("%s,", name);
  for(int i = 0; i < 18-strlen(name); i++)
    printf(" ");
  fflush(stdout);

  u64 sum[MITIGATION_STYLES];
  u64 best[MITIGATION_STYLES];

  for(int j=0; j < MITIGATION_STYLES; j++) {
    set_mitigations(j);

    sum[j] = 0;
    best[j] = 999999999999999999ull;

    for(int i=0; i < 3 + iter/10; i++)
      (*f)();

    for (int i=0; i < iter; i++) {
      u64 time = (*f)();
      sum[j] += time;
      if(time < best[j])
        best[j] = time;
    }
  }

  for(int j=0; j < MITIGATION_STYLES; j++)
    printf("%12ld,", best[j]);
  // for(int j=0; j < MITIGATION_STYLES; j++)
  //   printf("%12ld,", sum[j] / iter);

  printf("\n");
  fflush(stdout);

  return;
}

void two_line_test(void (*f)(u64*,u64*), int iter, const char* name) {
  if (iter < 1) iter = 1;

  printf("%s,", name);
  for(int i = 0; i < 18-strlen(name); i++)
    printf(" ");
  fflush(stdout);

  u64 sumParent[MITIGATION_STYLES];
  u64 sumChild[MITIGATION_STYLES];
  u64 bestParent[MITIGATION_STYLES];
  u64 bestChild[MITIGATION_STYLES];

  for(int j=0; j < MITIGATION_STYLES; j++) {
    set_mitigations(j);

    u64 timeParent=0, timeChild=0;
    sumParent[j] = 0;
    sumChild[j] = 0;
    bestParent[j] = 999999999999999999ull;
    bestChild[j] = 999999999999999999ull;

    for(int i=0; i < 3 + iter/10; i++)
      (*f)(&timeChild,&timeParent);

    for (int i=0; i < iter; i++)
    {
      (*f)(&timeChild,&timeParent);
      sumParent[j] += timeParent;
      sumChild[j] += timeChild;
      if(timeParent < bestParent[j])
        bestParent[j] = timeParent;
      if(timeChild < bestChild[j])
        bestChild[j] = timeChild;
    }
  }

  for(int j=0; j < MITIGATION_STYLES; j++)
    printf("%12ld,", bestParent[j]);
  // for(int j=0; j < MITIGATION_STYLES; j++)
  //   printf("%12ld,", sumParent[j] / iter);
  printf("\n");

  printf("%s-child,", name);
  for(int i = 0; i < 12-strlen(name); i++)
    printf(" ");

  for(int j=0; j < MITIGATION_STYLES; j++)
    printf("%12ld,", bestChild[j]);
  // for(int j=0; j < MITIGATION_STYLES; j++)
  //   printf("%12ld,", sumChild[j] / iter);
  printf("\n");
  fflush(stdout);

  return;
}

u64 *timeB;
void forkTest(u64 *childTime, u64 *parentTime)
{
  timeB = (u64*)mmap((void*)0x900000000, 8, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  *timeB = 0;

  int status;
  u64 timeA = start_timer();

  int forkId = fork();
  if (forkId == 0){
    *timeB = end_timer();
    kill(getpid(),SIGINT);
	printf("[error] unable to kill child process\n");
  } else if (forkId > 0) {
    u64 timeC = end_timer();
    waitpid(forkId, &status, 0);
	*childTime = *timeB - timeA;
	*parentTime = timeC - timeA;
    munmap(timeB, 8);
  } else {
    printf("[error] fork failed.\n");
  }
}

void *thrdfnc(void *args) {
  *timeB = end_timer();
  pthread_exit(NULL);
}
void threadTest(u64 *childTime, u64 *parentTime) {
  u64 timeB_data;
  timeB = &timeB_data;
  pthread_t newThrd;

  u64 timeD = start_timer();
  pthread_create (&newThrd, NULL, thrdfnc, NULL);
  u64 timeC = end_timer();
  pthread_join(newThrd,NULL);

  *parentTime = timeC - timeD;
  *childTime = *timeB - timeD;

  timeB = NULL;
}

u64 getpid_test() {
  u64 startTime = start_timer();
  syscall(SYS_getpid);
  u64 endTime = end_timer();
  return endTime - startTime;
}

int file_size = -1;
u64 read_test() {
  char *buf_in = (char *) malloc (sizeof(char) * file_size);

  int fd = open("test_file.txt", O_RDONLY);
  if (fd < 0) printf("invalid fd in read: %d\n", fd);

  u64 startTime = start_timer();
  int bytes_left = file_size;
  while (bytes_left > 0) {
    bytes_left -= syscall(SYS_read, fd, buf_in + file_size - bytes_left, bytes_left);
  }
  u64 endTime = end_timer();
  close(fd);
  free(buf_in);

  return endTime - startTime;
}

void read_warmup() {
  char *buf_out = (char *) malloc (sizeof(char) * file_size);
  for (int i = 0; i < file_size; i++) {
    buf_out[i] = 'a';
  }

  int fd = open("test_file.txt", O_CREAT | O_WRONLY, 0777);
  if (fd < 0) printf("invalid fd in write: %d\n", fd);

  int bytes_left = file_size;
  while (bytes_left > 0) {
    bytes_left -= syscall(SYS_write, fd, buf_out, bytes_left);
  }
  close(fd);

  char *buf_in = (char *) malloc (sizeof(char) * file_size);

  fd =open("test_file.txt", O_RDONLY);
  if (fd < 0) printf("invalid fd in read: %d\n", fd);

  for (int i = 0; i < 1000; i ++) {
    syscall(SYS_read, fd, buf_in, file_size);
  }
  close(fd);

  free(buf_out);
  free(buf_in);
  return;

}
u64 write_test() {
  char *buf = (char *) mmap (0, file_size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, 0, 0);
  for (int i = 0; i < file_size; i++) {
    buf[i] = 'a';
  }
  int fd = open("test_file.txt", O_CREAT | O_WRONLY, 0777);
  if (fd < 0) printf("invalid fd in write: %d\n", fd);

  u64 startTime = start_timer();
  int bytes_left = file_size;
  while (bytes_left > 0) {
    bytes_left -= syscall(SYS_write, fd, buf + file_size - bytes_left, bytes_left);
  }
  u64 endTime = end_timer();

  close(fd);
  munmap(buf, file_size);
  return endTime - startTime;
}


u64 mmap_test() {
  int fd = open("test_file.txt", O_RDONLY);
  if (fd < 0) printf("invalid fd%d\n", fd);

  u64 startTime = start_timer();
  void *addr = (void *)syscall(SYS_mmap, NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
  u64 endTime = end_timer();

  syscall(SYS_munmap, addr, file_size);
  close(fd);
  return endTime - startTime;
}

u64 page_fault_test() {
  int fd =open("test_file.txt", O_RDONLY);
  if (fd < 0) printf("invalid fd%d\n", fd);

  void *addr = (void *)syscall(SYS_mmap, NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0);

  u64 startTime = start_timer();
  char a = *((volatile char *)addr);
  (void)a;
  u64 endTime = end_timer();

  syscall(SYS_munmap, addr, file_size);
  close(fd);
  return endTime - startTime;
}

u64 cpu_test() {
  double start = 9903290.789798798;
  double div = 3232.32;
  u64 startTime = start_timer();
  for (int i = 0; i < 500000; i ++) {
    start = start / div;
  }
  u64 endTime = end_timer();
  return endTime - startTime;
}

u64 ref_test() {
  u64 startTime = start_timer();
  u64 endTime = end_timer();
  return endTime - startTime;
}

u64 munmap_test() {
  int fd =open("test_file.txt", O_RDWR);
  if (fd < 0) printf("invalid fd%d\n", fd);
  lseek(fd, file_size-1, SEEK_SET);

  char* p = (char*)malloc(file_size);
  memset(p, 0, file_size);
  write(fd, p, file_size);
  free(p);

  void *addr = (void *)syscall(SYS_mmap, NULL, file_size, PROT_WRITE, MAP_PRIVATE, fd, 0);
  for (int i = 0; i < file_size; i++) {
    ((char *)addr)[i] = 'b';
  }
  u64 startTime = start_timer();
  syscall(SYS_munmap, addr, file_size);
  u64 endTime = end_timer();
  close(fd);
  return endTime - startTime;
}

u64 context_switch_test() {
  int iter = 100;
  u64 startTime, endTime;
  int fds1[2], fds2[2], retval;
  retval = pipe(fds1);
  if (retval != 0) printf("[error] failed to open pipe1.\n");
  retval = pipe(fds2);
  if (retval != 0) printf("[error] failed to open pipe2.\n");

  char w = 'a', r;

  int forkId = fork();
  if (forkId > 0) { // is parent
    retval = close(fds1[0]);
    if (retval != 0) printf("[error] failed to close fd1.\n");
    retval = close(fds2[1]);
    if (retval != 0) printf("[error] failed to close fd2.\n");

    cpu_set_t oldset;
    sched_getaffinity(getpid(), sizeof(oldset), &oldset);

    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(0, &set);
    retval = sched_setaffinity(getpid(), sizeof(set), &set);
    if (retval == -1) printf("[error] failed to set processor affinity.\n");
    /* retval = setpriority(PRIO_PROCESS, 0, -20);  */
    /* if (retval == -1) printf("[error] failed to set process priority.\n"); */

    read(fds2[0], &r, 1);

    startTime = start_timer();
    for (int i = 0; i < iter; i++) {
      write(fds1[1], &w, 1);
      read(fds2[0], &r, 1);
    }
    endTime = end_timer();
    int status;
    waitpid(forkId, &status, 0);

    close(fds1[1]);
    close(fds2[0]);

    sched_setaffinity(getpid(), sizeof(oldset), &oldset);

    return (endTime - startTime) / iter;
  } else if (forkId == 0){
    retval = close(fds1[1]);
    if (retval != 0) printf("[error] failed to close fd1.\n");
    retval = close(fds2[0]);
    if (retval != 0) printf("[error] failed to close fd2.\n");

    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(0, &set);
    retval = sched_setaffinity(getpid(), sizeof(set), &set);
    if (retval == -1) printf("[error] failed to set processor affinity.\n");
    /* retval = setpriority(PRIO_PROCESS, 0, -20);  */
    /* if (retval == -1) printf("[error] failed to set process priority.\n"); */

    write(fds2[1], &w, 1);
    for (int i = 0; i < iter; i++) {
      read(fds1[0], &r, 1);
      write(fds2[1], &w, 1);
    }

    kill(getpid(), SIGINT);
    printf("[error] unable to kill child process\n");
    return 0;
  } else {
    printf("[error] failed to fork.\n");
    return 0;
  }
}

int main(int argc, char *argv[])
{
#ifdef HW_linux
  printf("Benchmark (linux),         Best,     Average,\n");
#else
  printf("Benchmark (ward),     Off  Best,    On  Best,  Fast  Best,\n");
  fflush(stdout);
#endif

  u64 mask = ~0ull;
  if (argc >= 2 && strcmp(argv[1], "-") != 0) {
    mask = 1ull << atoi(argv[1]);
  }

  int base_iter = 200;
  if (argc >= 3) {
    base_iter = atoi(argv[2]);
  }

  if(mask & (1ull<<0)) one_line_test(ref_test, base_iter * 1000, "ref");
  if(mask & (1ull<<1)) one_line_test(getpid_test, base_iter * 500, "getpid");
  if(mask & (1ull<<2)) one_line_test(context_switch_test, base_iter, "context switch");


  /*****************************************/
  /*             SEND & RECV               */
  /*****************************************/
  // msg_size = 1;
  // curr_iter_limit = 50;
  // printf("msg size: %d.\n", msg_size);
  // printf("curr iter limit: %d.\n", curr_iter_limit);
  // one_line_test(send_test, base_iter * 10, "send");
  // one_line_test(recv_test, base_iter * 10, "recv");

  // msg_size = 96000;	// This size 96000 would cause blocking on older kernels!
  // curr_iter_limit = 1;
  // printf("msg size: %d.\n", msg_size);
  // printf("curr iter limit: %d.\n", curr_iter_limit);
  // one_line_test(send_test, base_iter, "big send");
  // one_line_test(recv_test, base_iter, "big recv");


  /*****************************************/
  /*         FORK & THREAD CREATE          */
  /*****************************************/
  if(mask & (1ull<<3)) two_line_test(forkTest, base_iter * 2, "fork");
  if(mask & (1ull<<4)) two_line_test(threadTest, base_iter * 5, "thr create");

  if(mask & (1ull<<5)) {
    int page_count = 6000;
    void** pages = (void**)malloc(page_count * sizeof(void*));
    for (int i = 0; i < page_count; i++) {
      pages[i] = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    }
    two_line_test(forkTest, base_iter / 2, "big fork");
    for (int i = 0; i < page_count; i++) {
      munmap(pages[i], PAGE_SIZE);
    }
    free(pages);
  }

  if(mask & (1ull<<6)) {
    int page_count = 12000;
    void** pages = (void**)malloc(page_count * sizeof(void*));
    for (int i = 0; i < page_count; i++) {
      pages[i] = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    }
    two_line_test(forkTest, base_iter / 2, "huge fork");
    for (int i = 0; i < page_count; i++) {
      munmap(pages[i], PAGE_SIZE);
    }
    free(pages);
  }

  /*****************************************/
  /*     WRITE & READ & MMAP & MUNMAP      */
  /*****************************************/

  /****** SMALL ******/
  file_size = PAGE_SIZE;
  if(mask & (0x1f<<7)) read_warmup();

  if(mask & (1ull<<7)) one_line_test(write_test, base_iter * 10, "small write");
  if(mask & (1ull<<8)) one_line_test(read_test, base_iter * 10, "small read");
  if(mask & (1ull<<9)) one_line_test(mmap_test, base_iter * 10, "small mmap");
  if(mask & (1ull<<10)) one_line_test(munmap_test, base_iter * 10, "small munmap");
  if(mask & (1ull<<11)) one_line_test(page_fault_test, base_iter * 5, "small page fault");

  /****** MID ******/
  file_size = PAGE_SIZE * 10;
  if(mask & (0x1f<<12)) read_warmup();

  if(mask & (1ull<<12)) one_line_test(read_test, base_iter * 10, "mid read");
  if(mask & (1ull<<13)) one_line_test(write_test, base_iter * 10, "mid write");
  if(mask & (1ull<<14)) one_line_test(mmap_test, base_iter * 10, "mid mmap");
  if(mask & (1ull<<15)) one_line_test(munmap_test, base_iter * 10, "mid munmap");
  if(mask & (1ull<<16)) one_line_test(page_fault_test, base_iter * 5, "mid page fault");

  /****** BIG ******/
  file_size = PAGE_SIZE * 1000;
  if(mask & (0x1f<<17)) read_warmup();

  if(mask & (1ull<<17)) one_line_test(read_test, base_iter, "big read");
  if(mask & (1ull<<18)) one_line_test(write_test, base_iter / 2, "big write");
  if(mask & (1ull<<19)) one_line_test(mmap_test, base_iter * 10, "big mmap");
  if(mask & (1ull<<20)) one_line_test(munmap_test, base_iter / 4, "big munmap");
  if(mask & (1ull<<21)) one_line_test(page_fault_test, base_iter * 5, "big page fault");

  /****** HUGE ******/
  file_size = PAGE_SIZE * 10000;
  if(mask & (0x1f<<22)) read_warmup();

  if(mask & (1ull<<22)) one_line_test(read_test, base_iter, "huge read");
  if(mask & (1ull<<23)) one_line_test(write_test, base_iter / 4, "huge write");
  if(mask & (1ull<<24)) one_line_test(mmap_test, base_iter * 10, "huge mmap");
  if(mask & (1ull<<25)) one_line_test(munmap_test, base_iter / 4, "huge munmap");
  if(mask & (1ull<<26)) one_line_test(page_fault_test, base_iter * 5, "huge page fault");

  return(0);
}
