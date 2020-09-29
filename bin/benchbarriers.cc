#include "sysstubs.h"
#include "stdio.h"

static inline uint64_t serialize_and_rdtsc() {
  uint32_t cycles_low, cycles_high;
  __asm volatile ("CPUID\n\t"
                  "RDTSC\n\t"
                  "mov %%edx, %0\n\t"
                  "mov %%eax, %1\n\t"
                  : "=r" (cycles_high), "=r" (cycles_low)
                  :: "%rax", "%rbx", "%rcx", "%rdx");
  return ((uint64_t)cycles_high << 32) | (uint64_t)cycles_low;
}

static inline uint64_t rdtscp_and_serialize() {
  uint32_t cycles_low, cycles_high;
  __asm volatile("RDTSCP\n\t"
                 "mov %%edx, %0\n\t"
                 "mov %%eax, %1\n\t"
                 "CPUID\n\t"
                 : "=r" (cycles_high), "=r" (cycles_low)
                 :: "%rax", "%rbx", "%rcx", "%rdx");
  return ((uint64_t)cycles_high << 32) | (uint64_t)cycles_low;
}

int main(int argc, char** argv){
  ward_cmdline_change_param("keep_retpolines", "no");
  ward_cmdline_change_param("lazy_barrier", "yes");
  ward_cmdline_change_param("kpti", "yes");
  ward_cmdline_change_param("track_wbs", "no");

  printf("Options, transparent (cycles), intentional (cycles)\n");

  for(int style = 0; style < 5; style++) {
    switch(style) {
    case 0:
      ward_cmdline_change_param("keep_retpolines", "no");
      ward_cmdline_change_param("spectre_v2", "no");
      ward_cmdline_change_param("mds", "no");
      printf("Neither, ");
      break;
    case 1:
      ward_cmdline_change_param("keep_retpolines", "no");
      ward_cmdline_change_param("spectre_v2", "yes");
      ward_cmdline_change_param("mds", "no");
      printf("SpectreV2, ");
      break;
    case 2:
      ward_cmdline_change_param("keep_retpolines", "no");
      ward_cmdline_change_param("spectre_v2", "no");
      ward_cmdline_change_param("mds", "yes");
      printf("MDS, ");
      break;
    case 3:
      ward_cmdline_change_param("keep_retpolines", "no");
      ward_cmdline_change_param("spectre_v2", "yes");
      ward_cmdline_change_param("mds", "yes");
      printf("MDS+SpectreV2, ");
      break;
    case 4:
      ward_cmdline_change_param("keep_retpolines", "yes");
      ward_cmdline_change_param("spectre_v2", "yes");
      ward_cmdline_change_param("mds", "yes");
      printf("MDS+SpectreV2+QRetpoline, ");
      break;
    };

    int iters = 100000;

    for(int i = 0; i < iters/10; i++)
      ward_transparent_barrier(0);

    u64 start = serialize_and_rdtsc();
    for(int i= 0; i < iters; i++) {
      ward_transparent_barrier(0);
    }
    u64 end = rdtscp_and_serialize();
    printf("%ld, ", (end-start)/iters);

    for(int i = 0; i < iters/10; i++)
      ward_intentional_barrier(0);

    u64 start2 = serialize_and_rdtsc();
    for(int i= 0; i < iters; i++) {
      ward_intentional_barrier(0);
    }
    u64 end2 = rdtscp_and_serialize();
    printf("%ld\n", (end2-start2)/iters);
  }
  return 0;
}
