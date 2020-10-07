#pragma clang optimize off

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
  return channel[*addr * 1024]; // should match GAP size in bin/spectrev2.cc
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

u64 *spectre_target_addr; // always equal to &safe_target

int
spectre_victim_impl(u8 *channel, u8 *addr, int input) // channel and addr will be passed to gadget
{
  // This victim code has to be an almost exact match with the training code in
  // spectrev2.cc. Normally, it would be written in C and the attacker version
  // would be copied from the compiled kernel output. We can't do that here
  // because we want the attack demo to be robust against different
  // kernel/compiler versions.
  int result;
  __asm volatile("   mov %1, %%r11\n"
                 "   mov $0, %%rdx\n"
                 "1: cmp $0x264, %%rdx\n"
                 "   jle 2f\n"
                 "   jmp 4f\n"
                 "2: jmp 3f\n"
                 "3: add $0x1, %%rdx\n"
                 "spectre_victim_branch_addr:\n"
                 "4: call __x86_indirect_thunk_r11\n"
                 "   movl %%eax, %0\n"
                 : "=r" (result) : "r" (*spectre_target_addr): "rdx", "r11");
  return result;
}

//SYSCALL
int
sys_spectre_victim(u8 *channel, u8 *addr, int input)
{
  ensure_secrets();
  return spectre_victim_impl(channel, addr, input);
}

//SYSCALL
u64
sys_spectre_get_victim_branch_addr(void)
{
  extern char spectre_victim_branch_addr[];
  return (u64)(char*)spectre_victim_branch_addr;
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
  spectre_target_addr = (u64*)kalloc("spectre_target_addr");
  *spectre_target_addr = (u64)&safe_target;
}
