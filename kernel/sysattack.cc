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

u64 *spectre_target_addr __attribute__((section (".qdata"))); // always equal to &safe_target

//SYSCALL
int
sys_spectre_victim(u8 *channel, u8 *addr, int input) // channel and addr will be passed to gadget
{
  int result;
  __asm volatile("   mov %1, %%r11\n"
                 "   mov $0, %%rdx\n"
                 "1: cmp $0x64, %%rdx\n"
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
  spectre_target_addr = (u64*)palloc("spectre_target_addr");
  *spectre_target_addr = (u64)&safe_target;
}
