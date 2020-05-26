#include "user.h"
#include "stdio.h"
#include "amd64.h"

int main(int argc, int argv[]){
  cmdline_change_param("lazy_barrier", "yes");
  cmdline_change_param("kpti", "yes");
  cmdline_change_param("track_wbs", "no");

  printf("Options, transparent (cycles), intentional (cycles)\n");

  for(int style = 0; style < 4; style++) {
    switch(style) {
    case 0:
      cmdline_change_param("spectre_v2", "no");
      cmdline_change_param("mds", "no");
      printf("Neither, ");
      break;
    case 1:
      cmdline_change_param("spectre_v2", "yes");
      cmdline_change_param("mds", "no");
      printf("SpectreV2, ");
      break;
    case 2:
      cmdline_change_param("spectre_v2", "no");
      cmdline_change_param("mds", "yes");
      printf("MDS, ");
      break;
    case 3:
      cmdline_change_param("spectre_v2", "yes");
      cmdline_change_param("mds", "yes");
      printf("MDS+SpectreV2, ");
      break;
    };

    int iters = 100000;

    for(int i = 0; i < iters/10; i++)
      transparent_barrier(0);

    u64 start = serialize_and_rdtsc();
    for(int i= 0; i < iters; i++) {
      transparent_barrier(0);
    }
    u64 end = rdtscp_and_serialize();
    printf("%ld, ", (end-start)/iters);

    for(int i = 0; i < iters/10; i++)
      intentional_barrier(0);

    u64 start2 = serialize_and_rdtsc();
    for(int i= 0; i < iters; i++) {
      intentional_barrier(0);
    }
    u64 end2 = rdtscp_and_serialize();
    printf("%ld\n", (end2-start2)/iters);
  }
  return 0;
}
