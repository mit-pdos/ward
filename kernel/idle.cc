#include "types.h"
#include "kernel.hh"
#include "amd64.h"
#include "spinlock.hh"
#include "condvar.hh"
#include "proc.hh"
#include "cpu.hh"
#include "spercpu.hh"
#include "kmtrace.hh"
#include "bits.hh"
#include "codex.hh"
#include "benchcodex.hh"
#include "cpuid.hh"
#include "ilist.hh"
#include "vm.hh"
#include "file.hh"
#include "filetable.hh"

struct idle {
  struct proc *cur;
};

namespace {
  DEFINE_QPERCPU(idle, idlem);
};

struct proc *
idleproc(void)
{
  return idlem->cur;
}

void
idleloop(void)
{
  mtstart(idleloop, myproc());

  sti();
  for (;;) {
    acquire(&myproc()->lock);
    myproc()->set_state(RUNNABLE);
    sched(false);
    if (steal() == 0) {
        // XXX(Austin) This will prevent us from immediately picking
        // up work that's trying to push itself to this core (pinned
        // thread).  Use an IPI to poke idle cores.
        asm volatile("hlt");
    }
  }
}

void
initidle(void)
{
  struct proc *p = proc::alloc();
  if (!p)
    panic("initidle proc::alloc");

  if (myid() == 0) {
    if (cpuid::features().mwait) {
      // Check smallest and largest line sizes
      auto info = cpuid::mwait();
      assert((u16)info.smallest_line == 0x40);
      assert((u16)info.largest_line == 0x40);
    }
  }

  snprintf(p->name, sizeof(p->name), "idle_%u", myid());
  mycpu()->proc = p;
  myproc()->cpuid = myid();
  myproc()->cpu_pin = true;
  idlem->cur = p;
}
