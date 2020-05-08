#pragma once

#include "compiler.h"
#include <sys/types.h>
#include <uk/unistd.h>

BEGIN_DECLS

struct stat;

ssize_t write(int fd, const void *buf, size_t count);
ssize_t read(int fd, void *buf, size_t count);
ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset);
ssize_t pread(int fd, void *buf, size_t count, off_t offset);
long close(int fd);
long link(const char *oldpath, const char *newpath);
long unlink(const char *pathname);
long execv(const char *path, char *const argv[]);
long dup(int oldfd);
long dup2(int oldfd, int newfd);
off_t lseek(int fd, off_t offset, int whence);
long chdir(const char *path);
long pipe(int pipefd[2]);
long pipe2(int pipefd[2], int flags);
long sync(void);
long fsync(int fd);

unsigned sleep(unsigned);
pid_t getpid(void);
pid_t fork(void);

extern char* optarg;
extern int optind;
int getopt(int ac, char* const av[], const char* optstring);

long stat(const char*, struct stat*);
long lstat(const char*, struct stat*);
long fstat(int fd, struct stat *buf);

// xv6-specific
long fstatx(int fd, struct stat *buf, enum stat_flags flags);

END_DECLS
