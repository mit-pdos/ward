#pragma once

#include "spinlock.hh"
#include <atomic>
#include "cpputil.hh"
#include "fs.h"
#include "sched.hh"
#include <uk/signal.h>
#include "ilist.hh"
#include <stdexcept>
#include "vmalloc.hh"
#include "bitset.hh"
#include "futex.hh"

struct gc_handle;
class filetable;
class vnode;

#if 0
// This should be per-address space
  if (mapkva(pml4, kshared, KSHARED, KSHAREDSIZE)) {
    cprintf("vmap::vmap: mapkva out of memory\n");
    goto err;
  }
#endif


struct robust_list {
  userptr<robust_list> next;
};

struct robust_list_head {
  // circular linked list of futexes to unlock at thread exit.
  robust_list list;
  long futex_offset;
  userptr<robust_list> list_op_pending;
};

// Saved registers for kernel context switches.
// (also implicitly defined in swtch.S)
struct context {
  u64 r15;
  u64 r14;
  u64 r13;
  u64 r12;
  u64 rbp;
  u64 rbx;
  u64 rip;
} __attribute__((packed));

// Per-process, per-stack meta data for mtrace
#if MTRACE
#define MTRACE_NSTACKS 16
#define MTRACE_TAGSHIFT 24
#if NCPU > 256
#error Oops -- decrease MTRACE_TAGSHIFT
#endif
struct mtrace_stacks {
  int curr;
  unsigned long tag[MTRACE_NSTACKS];
};
#endif

struct waitstub {
  ilink<waitstub> next;
  int pid;
  int status;

  NEW_DELETE_OPS(waitstub);
};

typedef enum procstate {
  EMBRYO,
  SLEEPING,
  RUNNABLE,
  RUNNING,
  ZOMBIE,
  IDLING
} procstate_t;

struct pproc {
  proc* p;

  struct spinlock lock;
  struct condvar *oncv = nullptr;  // Where it is sleeping, for kill()
  u64 cv_wakeup = 0;               // Wakeup time for this process
  u64 curcycles = 0;
  unsigned cpuid = 0;
  bool cpu_pin = false;
  ilink<pproc> cv_waiters;      // Linked list of processes waiting for oncv
  ilink<pproc> cv_sleep;        // Linked list of processes sleeping on a cv
  const int pid;             // Process ID

private:
  procstate_t state_ = EMBRYO;
public:

  pproc(proc* p_, int pid_) : p(p_), pid(pid_) {}
  procstate_t get_state(void) const { return state_; }
  void set_state(procstate_t s);
  bool cansteal() {
    return get_state() == RUNNABLE && !cpu_pin &&
      curcycles != 0 && curcycles > VICTIMAGE;
  };

  friend struct proc;

  PUBLIC_NEW_DELETE_OPS(pproc);
};

// Per-process state
struct proc {
  std::unique_ptr<pproc> p;

  // First page of proc is quasi-user visible
  char *kstack;                // Bottom of kernel stack for this process
  char *qstack;                // Bottom of quasi user-visible stack
  int killed;                  // If non-zero, have been killed
  struct trapframe *tf;        // Trap frame for current syscall

  int uaccess_;
  u64 user_fs_;
  sref<vmap> vmap;             // va -> vma
  struct condvar *cv;          // for waiting till children exit

  ilink<proc> futex_link;
  futexkey futex_key;

  bool yield_;                 // yield cpu up when returning to user space
  u64 tsc;
  context* context;            // swtch() here to run process
  bool on_qstack;              // Only valid when proc is *not* running
  sref<vnode> cwd;             // Current directory
  sref<filetable> ftable;      // File descriptor table

  const int& pid = p->pid;
  spinlock& lock = p->lock;
  condvar*& oncv = p->oncv;
  u64& cv_wakeup = p->cv_wakeup;
  u64& curcycles = p->curcycles;
  unsigned& cpuid = p->cpuid;
  bool& cpu_pin = p->cpu_pin;
  ilink<pproc>& cv_waiters = p->cv_waiters;
  ilink<pproc>& cv_sleep = p->cv_sleep;
private:
  procstate_t& state_ = p->state_;
public:

  ilist<waitstub, &waitstub::next> waiting_children;

  u64 transparent_barriers;
  u64 intentional_barriers;

  // These pointers are set to USERTOP if invalid
  userptr<robust_list_head> robust_list_ptr;
  userptr<u32> tid_address;

  char fpu_state[XSAVE_BYTES] __attribute__ ((aligned (64)));

#if KERNEL_STRACE
  char syscall_param_string[128];
#endif

  struct proc *parent;         // Parent process
  char name[16];               // Process name (debugging)
  ilink<proc> child_next;
  ilist<proc,&proc::child_next> childq;
  struct gc_handle *gc;

  __page_pad__;

  char lockname[16];
#if MTRACE
  struct mtrace_stacks mtrace_stacks;
#endif
  u64 unmap_tlbreq_;
  int data_cpuid;              // Where vmap and kstack is likely to be cached
  int run_cpuid_;

  userptr_str upath;
  userptr<userptr_str> uargv;

  u8 __cxa_eh_global[16];

  std::atomic<int> exception_inuse;
  u8 exception_buf[256];
  sigaction sig[NSIG];
  u32 blocked_signals;
  u32 pending_signals;

  static proc* alloc();
  void         init_vmap();
  procstate_t  get_state(void) const { return state_; }
  void         set_state(procstate_t s) { p->set_state(s); }
  int          set_cpu_pin(int cpu);
  static int   kill(int pid);
  int          kill();

  static u64   hash(const u32& p);

  static bool deliver_signal(int pid, int signo);
  bool deliver_signal(int signo);

  NEW_DELETE_OPS(proc);

private:
  proc(int npid);
  proc& operator=(const proc&) = delete;
  proc(const proc& x) = delete;
} __page_align__;

class kill_exception : public std::runtime_error {
public:
    kill_exception() : std::runtime_error("killed") { };
};

#if KERNEL_STRACE
#define STRACE_PARAMS(fmt, ...) snprintf(myproc()->syscall_param_string, 128, fmt, __VA_ARGS__)
#else
#define STRACE_PARAMS(fmt, ...) do{} while(0)
#endif
