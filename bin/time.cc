#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <sys/wait.h>

#include <vector>

int
main(int ac, char * const av[])
{
  if (ac <= 1) {
    fprintf(stderr, "usage: %s command...", av[0]);
    return 1;
  }

  struct timespec start, end;
  clock_gettime(CLOCK_REALTIME, &start);

  int pid = fork();
  if (pid < 0) {
    fprintf(stderr, "time: fork failed");
    return 1;
  }

  if (pid == 0) {
    std::vector<const char *> args(av + 1, av + ac);
    args.push_back(nullptr);
    execv(args[0], const_cast<char * const *>(args.data()));
    fprintf(stderr, "time: exec failed");
    return 1;
  }

  wait(NULL);
  clock_gettime(CLOCK_REALTIME, &end);
  unsigned long delta = (end.tv_sec - start.tv_sec) * 1000000000UL +
    (unsigned long)end.tv_nsec - (unsigned long)start.tv_nsec;
  printf("%lu ns\n", delta);
  return 0;
}
