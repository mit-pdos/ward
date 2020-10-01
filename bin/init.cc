// init: The initial user-level program

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>

#include "sysstubs.h"

static const char *sh_argv[] = { "sh", 0 };
static const char *app_argv[][2] = {
  // { "telnetd", 0 },
  { "httpd", 0 },
};

static const char* busybox_aliases[] = {
  "/bin/cat", "/bin/cp", "/bin/clear", "/bin/du", "/bin/echo", "/bin/ls", "/bin/mkdir", "/bin/sh",
  "/bin/nsh", "/bin/sleep", "/bin/cp", "/bin/rm", "/bin/mv", "/bin/tee", "/bin/ln",
  "/bin/dd", "/bin/less", 0 };

static int
startone(const char * const *argv)
{
  int pid;

  pid = ward_fork_flags(0);
  if(pid < 0){
    fprintf(stderr, "init: fork failed");
    ward_exit(-1);
  }
  if(pid == 0){
    ward_execv(argv[0], const_cast<char * const *>(argv));
    fprintf(stderr, "init: exec %s failed", argv[0]);
    ward_exit(-1);
  }
  return pid;
}

static void
runcmdline(void)
{
  const char* argv[4] = { "sh", "-c", 0 };
  char buf[256];
  char* b;
  long r;
  int fd;

  fd = ward_openat(0, "/dev/cmdline", O_RDONLY);

  if (fd < 0)
    return;

  r = ward_read(fd, buf, sizeof(buf)-1);
  if (r < 0)
    return;
  buf[r] = 0;

  ward_close(fd);

  if ((b = strchr(buf, '%'))) {
    argv[2] = b+1;
    printf("init: Starting %s\n", argv[2]);
    startone(argv);
  }
}

int
main(void)
{
  int pid, wpid;

  if(ward_open("/dev/tty", O_RDWR) < 0){
    ward_mknod("/dev/tty", 1, 1);
    ward_open("/dev/tty", O_RDWR);
  }
  ward_dup(0);  // stdout
  ward_dup(0);  // stderr

  ward_mkdir("etc", 0777);
  int fd = ward_openat(0, "/etc/gitconfig", O_RDWR | O_CREAT);
  if(fd >= 0) ward_close(fd);

  for(const char** a = busybox_aliases; *a; a++) {
    ward_link("/bin/busybox", *a);
  }

  for (auto &argv : app_argv)
    startone(argv);

  time_t now = ward_time(nullptr);
  printf("init complete at %s", ctime(&now));
  fflush(stdout);

  runcmdline();

  for(;;){
    pid = startone(sh_argv);
    while((wpid=ward_wait(NULL)) >= 0 && wpid != pid)
      /*fprintf(stderr, "zombie!\n")*/;
  }
  return 0;
}
