#pragma GCC push_options
#pragma GCC optimize ("O0")

#include "types.h"
#include "amd64.h"
#include "kernel.hh"

#include <uk/unistd.h>

char *secret = (char*)"The Magic Words are Please Make My Code Work.";
const int secret_len = 45;

//SYSCALL
u64
sys_spectre_get_secret_addr(int *len)
{
  *len = secret_len;
  return (u64)secret;
}

__attribute__((noinline))
u8
gadget(u8 *channel, u8 *addr)
{
  return channel[*addr * 1024]; // should match GAP size in bin/spectrev2k.cc
}

//SYSCALL
u64
sys_spectre_get_gadget_addr(void)
{
  return (u64)&gadget;
}

__attribute__((noinline))
int
safe_target(void)
{
  return 4;
}

u64 *spectre_target_addr __attribute__((section (".qdata"))); // always equal to &safe_target

//SYSCALL
int
sys_spectre_victim(u8 *channel, u8 *addr, int input) // channel and addr will be passed to gadget
{
  int junk = 0;
  // set up bhb by performing >29 taken branches
  for (int i = 1; i <= 100; i++) {
    input += i;
    junk += input & i;
  }

  // perform indirect branch
  int result = ((int (*)(void))(*spectre_target_addr))();

  // prevent compiler from optimizing out inputs
  result &= (u64)channel;
  result &= ((u64)(channel) >> 32);
  result &= (u64)addr;
  result &= ((u64)(addr) >> 32);
  return result & junk;
}

//SYSCALL
u64
sys_spectre_get_victim_addr(void)
{
  return (u64)&sys_spectre_victim;
}

//SYSCALL
int
sys_spectre_get_victim_call_offset(void)
{
  u8 *code = (u8*) &sys_spectre_victim;
  int offset;
  for (offset = 0; *(code + offset) != 0xff; offset++); // look for call instruction
  return offset;
}

//SYSCALL
void
sys_spectre_flush_target(void)
{
  clflush((void*) spectre_target_addr);
  mfence();
}

void
initattack(void)
{
  spectre_target_addr = (u64*)palloc("spectre_target_addr");
  *spectre_target_addr = (u64)&safe_target;
}

#pragma GCC pop_options
