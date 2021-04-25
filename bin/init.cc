// init: The initial user-level program

#include "sysstubs.h"

#define O_RDONLY  00
#define O_WRONLY  01
#define O_RDWR    02
#define O_CREAT   0100

static const char *sh_argv[] = { "sh", 0 };
static const char *app_argv[][2] = {
  { "telnetd", 0 },
  { "httpd", 0 },
};

static const char* busybox_aliases[] = {
  "/bin/cat", "/bin/cp", "/bin/clear", "/bin/du", "/bin/echo", "/bin/ls", "/bin/mkdir", "/bin/sh",
  "/bin/nsh", "/bin/sleep", "/bin/cp", "/bin/rm", "/bin/mv", "/bin/tee", "/bin/ln",
  "/bin/dd", "/bin/less", 0 };

char*
strchr(const char *s, int c)
{
  for (; *s; s++)
    if (*s == c)
      return (char*)s;
  return 0;
}

void fputs(int fd, const char* msg) {
  size_t len = 0;
  while(msg[len]) len++;

  size_t bytes_written = 0;
  while (bytes_written < len) {
    int n = ward_write(fd, msg+bytes_written, len - bytes_written);
    if (n < 0) break;
    bytes_written += n;
  }
}

static int
startone(const char * const *argv)
{
  int pid;

  pid = ward_fork_flags(0);
  if(pid < 0){
    fputs(1, "init: fork failed");
    ward_exit(-1);
  }
  if(pid == 0){
    ward_execv(argv[0], const_cast<char * const *>(argv));
    fputs(1, "init: exec ");
    fputs(1, argv[0]);
    fputs(1, " failed\n");
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
    fputs(0, "init: Starting '");
    fputs(0, argv[2]);
    fputs(0, "'\n");
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

  fputs(0, "init complete\n");

  runcmdline();

  for(;;){
    pid = startone(sh_argv);
    while((wpid=ward_wait(NULL)) >= 0 && wpid != pid)
      /*fprintf(stderr, "zombie!\n")*/;
  }
  return 0;
}
