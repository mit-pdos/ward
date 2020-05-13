// User/kernel shared assembly
#pragma once

#include "types.h"

__attribute__((always_inline))
void clflush(volatile void *p)
{
  __asm volatile("clflush (%0)" :: "r" (p));
}

__attribute__((always_inline))
void mfence()
{
  __asm volatile("mfence");
}

__attribute__((always_inline))
u64 start_timer() {
  u32 cycles_low, cycles_high;
  __asm volatile ("cpuid\n"
                  "rdtsc\n"
                  "mov %%edx, %0\n"
                  "mov %%eax, %1\n"
                  : "=r" (cycles_high), "=r" (cycles_low)
                  :: "%rax", "%rbx", "%rcx", "%rdx");
  return ((u64)cycles_high << 32) | (u64)cycles_low;
}

__attribute__((always_inline))
u64 end_timer() {
  u32 cycles_low, cycles_high;
  __asm volatile("rdtscp\n"
                 "mov %%edx, %0\n"
                 "mov %%eax, %1\n"
                 "cpuid\n"
                 : "=r" (cycles_high), "=r" (cycles_low)
                 :: "%rax", "%rbx", "%rcx", "%rdx");
  return ((u64)cycles_high << 32) | (u64)cycles_low;
}
