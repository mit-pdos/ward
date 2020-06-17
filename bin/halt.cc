#ifdef XV6_USER
#include <stdlib.h>
#include "sysstubs.h"
#else
#include <stdio.h>
#include <unistd.h>
#include <linux/reboot.h>
#include <sys/reboot.h>
#endif

int
main(int argc, char *argv[])
{
#ifdef XV6_USER
  if(argc >= 2)
    ward_halt(atoi(argv[1]));
  else
    ward_halt(0);
#else
  reboot(LINUX_REBOOT_CMD_POWER_OFF);
  perror("reboot failed");
#endif
  return 0;
}
