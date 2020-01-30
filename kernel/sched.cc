#include "types.h"
#include "kernel.hh"
#include "mmu.h"
#include "amd64.h"
#include "spinlock.hh"
#include "condvar.hh"
#include "proc.hh"
#include "cpu.hh"
#include "bits.hh"
#include "kmtrace.hh"
#include "vm.hh"
#include "major.h"
#include "rnd.hh"
#include "lb.hh"
#include "ilist.hh"
#include "kstream.hh"
#include "file.hh"

enum { sched_debug = 0 };

struct proc_group {
  u64 asid;

  ilist<proc, &proc::sched_link> procs;
  u64 nprocs;

  ilink<proc_group> link;
};

struct schedule {
  struct spinlock lock;
  sref<vmap> current_;
  sched_stat stats_;
};

struct sched_dir {
private:
  ilist<proc, &proc::sched_link> kernel_procs_;
  ilist<vmap, &vmap::sched_link> user_procs_;
  spinlock lock_;

  percpu<schedule> schedule_;
public:
  sched_dir() {}
  ~sched_dir() {};
  NEW_DELETE_OPS(sched_dir);

  // schedule* balance_get(int id) const {
  //   return schedule_[id];
  // }

  void steal() {
    // if (!SCHED_LOAD_BALANCE)
    //   return;

    // scoped_cli cli;
    // b_.balance();
  }

  void addrun(struct proc* p) {
    p->set_state(RUNNABLE);

    if(p->vmap) {
      scoped_acquire l(&p->vmap->sched_lock_);
      bool enqueue_vmap = p->vmap->run_queue_.empty();
      p->vmap->run_queue_.push_back(p);
      if (enqueue_vmap) {
        scoped_acquire l2(&lock_);
        user_procs_.push_back(p->vmap.get());
      }
    } else {
      scoped_acquire l2(&lock_);
      kernel_procs_.push_back(p);
    }
  }

  proc* next_for_qdomain(vmap* vmap) {
    scoped_acquire l(&vmap->sched_lock_);
    if (vmap->run_queue_.empty())
      return nullptr;

    proc* p = &vmap->run_queue_.front();
    if (p->cpu_pin && p->cpuid != mycpu()->id)
      return nullptr;
    vmap->run_queue_.pop_front();
    if (vmap->run_queue_.empty()) {
      scoped_acquire l2(&lock_);
      user_procs_.erase(user_procs_.iterator_to(vmap));
    }

    return p;
  }

  proc* next_proc() {
    vmap* vmap = nullptr;
    {
      scoped_acquire l2(&lock_);
      if (user_procs_.empty()) {
        if (kernel_procs_.empty())
          return nullptr;

        proc* p = &kernel_procs_.front();
        if (p->cpu_pin && p->cpuid != mycpu()->id)
          return nullptr;
        kernel_procs_.pop_front();
        // cprintf("[%d]: running pid=%d name=%s\n", mycpu()->id, p.pid, p.name);
        return p;
      } else {
        vmap = &user_procs_.front();
        user_procs_.pop_front();
      }
    }
    scoped_acquire l(&vmap->sched_lock_);
    if (vmap->run_queue_.empty())
      return nullptr;

    proc* p = &vmap->run_queue_.front();
    if (p->cpu_pin && p->cpuid != mycpu()->id) {
      p = nullptr;
    } else {
      vmap->run_queue_.pop_front();
    }
    if (!vmap->run_queue_.empty()) {
      scoped_acquire l2(&lock_);
      user_procs_.push_back(vmap);
    }
    // cprintf("[%d]: running pid=%d name=%s\n", mycpu()->id, p->pid, p->name);
    return p;
  }

  void sched(bool voluntary)
  {
    extern void forkret(void);
    int intena;
    proc* prev;

    // Poke the watchdog
    wdpoke();

#if SPINLOCK_DEBUG
    if(!holding(&myproc()->lock))
      panic("sched proc->lock");
#endif

    if(mycpu()->ncli != 1)
      panic("sched locks (ncli = %d)", mycpu()->ncli);
    if(myproc()->get_state() == RUNNING)
      panic("sched running");
    if(readrflags()&FL_IF)
      panic("sched interruptible");
    intena = mycpu()->intena;
    myproc()->curcycles += rdtsc() - myproc()->tsc;

    // Interrupts are disabled
    proc* next = nullptr;
    if (voluntary && myproc()->vmap) {
      next = next_for_qdomain(myproc()->vmap.get());
    }
    if (!next) {
      next = next_proc();
    }

    u64 t = rdtsc();
    // if (myproc() == idleproc())
    //   schedule_[mycpu()->id].stats_.idle += t - schedule_[mycpu()->id].stats_.schedstart;
    // else
    //   schedule_[mycpu()->id].stats_.busy += t - schedule_[mycpu()->id].stats_.schedstart;
    // schedule_[mycpu()->id].stats_.schedstart = t;

    if (next == nullptr) {
      if (myproc()->get_state() != RUNNABLE ||
          // proc changed its CPU pin?
          myproc()->cpuid != mycpu()->id) {
        next = idleproc();
      } else {
        myproc()->set_state(RUNNING);
        mycpu()->intena = intena;
        release(&myproc()->lock);
        return;
      }
    }

    if (next->get_state() != RUNNABLE)
      panic("non-RUNNABLE next %s %u", next->name, next->get_state());

    prev = myproc();

    if (prev->get_state() == ZOMBIE)
      mtstop(prev);
    else
      mtpause(prev);
    mtign();

    next->set_state(RUNNING);
    next->tsc = rdtsc();

    if (next->context.ptr->rip != (uptr)threadstub && next->context.ptr->rip != (uptr)forkret) {
      mtresume(next);
    }
    mtrec();

    // Set task-switched and monitor coprocessor bit and clear emulation
    // bit so we get a #NM exception if the new process tries to use FPU
    // or MMX instructions.
    auto cr0 = rcr0();
    auto ncr0 = (cr0 | CR0_TS | CR0_MP) & ~CR0_EM;
    if (cr0 != ncr0)
      lcr0(ncr0);

    mycpu()->ts.rsp[0] = (u64) next->kstack + KSTACKSIZE;

    switchvm(prev->vmap.get(), next->vmap.get());

    if (!prev->on_qstack && next->on_qstack) {
    //   u64 rsp = (u64)next->context.ptr;
    //   cprintf("rsp = %lx kstack = %lx\n", rsp, next->kstack);
    //   assert(rsp >= (u64)next->kstack);
    //   assert(rsp < (u64)next->kstack + KSTACKSIZE);
    //   memcpy((char*)rsp, (char*)rsp - (u64)next->kstack + (u64)next->qstack,
    //          (u64)next->kstack + KSTACKSIZE - rsp);
      next->on_qstack = false;
      memcpy(next->kstack, next->qstack, KSTACKSIZE);
    }

    // assert(prev->on_qstack == !secrets_mapped);
    // ensure_secrets();
    mycpu()->proc = next; // needs secrets before this line?
    mycpu()->prev = prev;

    prev->on_qstack = !secrets_mapped;
    assert(!next->on_qstack);
    // if (prev->on_qstack && !next->on_qstack) {
      swtch_and_barrier((contextptr*)&prev->context.ptr, next->context.ptr);
    // } else {
    //   swtch((contextptr*)&prev->context.ptr, next->context.ptr);
    // }

    mycpu()->intena = intena;
    post_swtch();
  }

  // void
  // scheddump(print_stream *s)
  // {
  //   for (int i = 0; i < NCPU; i++) {
  //     s->print("CPU: ", i);
  //     schedule_[i]->dump(s);
  //     s->println();
  //   }
  // }

};

sched_dir thesched_dir __mpalign__;

void
post_swtch(void)
{
  if (mycpu()->prev->get_state() == RUNNABLE && mycpu()->prev != idleproc())
    addrun(mycpu()->prev);
  release(&mycpu()->prev->lock);
}

void
sched(bool voluntary)
{
  thesched_dir.sched(voluntary);
}

void
addrun(struct proc* p)
{
  thesched_dir.addrun(p);
}

static int
statread(char *dst, u32 off, u32 n)
{
  window_stream s(dst, off, n);
  // thesched_dir.scheddump(&s);
  return s.get_used();
}

int
steal()
{
  thesched_dir.steal();
  return 0;
}

void
initsched(void)
{
  devsw[MAJ_STAT].pread = statread;
}
