#include <stdio.h>
#include <stdlib.h>

#include "sysstubs.h"

int
main(int argc, char *argv[])
{
  switch(argc) {
  case 1: // view all
    ward_cmdline_view_param(NULL);
    break;
  case 2: // view one
    ward_cmdline_view_param(argv[1]);
    break;
  case 3: // change one
    ward_cmdline_change_param(argv[1], argv[2]);
    break;
  default:
    printf("Usage: param [name] [value]        view/change system parameters\n");
    exit(2);
  }
  return 0;
}
