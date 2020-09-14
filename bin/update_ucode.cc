#include "sysstubs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

int
main(int argc, char *argv[])
{
  unsigned long info = ward_cpu_info();

  char file[1024];
  snprintf(file, 1024, "intel-ucode/%02lx-%02lx-%02lx", ((info>>12) & 0xfff), ((info>>4) & 0xff), (info & 0xf));

  int fd = open(file, O_RDONLY);
  if(fd < 0) {
    printf("Failed to open '%s'\n", file);
    return 0;
  }

  int bytes_read = 0;
  int max_bytes = 0x100000;
  char* contents = (char*)malloc(max_bytes);

  while (bytes_read < max_bytes) {
    int r = read(fd, contents + bytes_read, max_bytes - bytes_read);
    if (r <= 0) break;
    bytes_read += r;
  }

  unsigned int revision = *(unsigned int*)(contents+4);
  unsigned int date = *(unsigned int*)(contents+8);
  printf("Loading microcode revision=0x%x released=%x/%x/%x\n",
         revision, date >> 24, (date >> 16) & 0xff, (date & 0xffff));
  fflush(stdout);

  int ret = ward_update_microcode(contents, 0x100000);
  printf("update_microcode returned %d\n", ret);
  return 0;
}
