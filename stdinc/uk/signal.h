#pragma once

#define SIGBUS    7
#define SIGKILL   9
#define SIGSEGV   11
#define SIGPIPE   13
#define SIGSTOP   19
#define NSIG      32

#define SIG_DFL   ((void (*)(int)) 0)
#define SIG_IGN   ((void (*)(int)) 1)
#define SIG_ERR   ((void (*)(int)) -1)

struct sigaction {
  union {
    void (*sa_handler)(int);
    void (*sa_sigaction)(int, void*, void*);
  };
  unsigned int sa_flags;
  void (*sa_restorer)(void);
  unsigned int sa_mask;
};
