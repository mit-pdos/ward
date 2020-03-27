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
