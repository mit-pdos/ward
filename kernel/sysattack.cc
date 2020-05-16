#pragma GCC push_options
#pragma GCC optimize ("O0")

#include "types.h"
#include "amd64.h"
#include "kernel.hh"

#include <uk/unistd.h>
#include <uk/asm.h>

__attribute__((noinline))
u8
sys_gadget(volatile u8 *channel, volatile int secret)
{
  return channel[secret * 1024]; // should match GAP size in bin/attack.cc
}

//SYSCALL
u64
sys_get_gadget_addr(void)
{
  return (u64)&sys_gadget;
}

__attribute__((noinline))
int
safe_target(void)
{
  return 4;
}

//SYSCALL
u64
sys_get_safe_addr(void)
{
  return (u64)&safe_target;
}

u64 *safe_target_addr __attribute__((section (".qdata")));

//SYSCALL
void
sys_set_safe_addr(u64 addr)
{
  //*safe_target_addr = addr;
  mfence();
}

//SYSCALL
int
sys_victim(volatile u8 *channel, volatile int secret, volatile int input) // channel and addr will be passed to gadget
{
  int junk = 0;
  // set up bhb by performing >29 taken branches
  for (int i = 1; i <= 100; i++) {
    input += i;
    junk += input & i;
  }

  // perform indirect branch
  int result;
  __asm volatile("callq *%1\n"
                 "mov %%eax, %0\n"
                 : "=r" (result)
                 : "r" (*safe_target_addr)
                 : "rax", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11");

  // prevent compiler from optimizing out inputs
  result &= (u64)channel;
  result &= ((u64)(channel) >> 32);
  result &= secret;
  return result & junk;
}

//SYSCALL
u64
sys_get_victim_addr(void)
{
  return (u64)&sys_victim;
}

//SYSCALL
int
sys_get_victim_offset(void)
{
  u8 *code = (u8*) &sys_victim;
  int offset;
  for (offset = 0; *(code + offset) != 0xff; offset++); // look for call instruction
  return offset;
}

//SYSCALL
void
sys_flush_target(void)
{
  clflush((void*) safe_target_addr);
  mfence();
}

#pragma GCC pop_options
