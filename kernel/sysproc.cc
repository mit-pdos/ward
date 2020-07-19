#include "types.h"
#include "amd64.h"
#include "kernel.hh"
#include "mmu.h"
#include "spinlock.hh"
#include "condvar.hh"
#include "proc.hh"
#include "cpu.hh"
#include "vm.hh"
#include "kmtrace.hh"
#include "futex.hh"
#include "version.hh"
#include "filetable.hh"
#include "ipi.hh"
#include "cpuid.hh"
#include "cmdline.hh"
#include "kmeta.hh"
#include "nospec-branch.hh"
#include "errno.h"

#include <uk/mman.h>
#include <uk/utsname.h>
#include <uk/unistd.h>

//SYSCALL
pid_t
sys_fork_flags(int flags)
{
  ensure_secrets();
  clone_flags cflags = clone_flags::WARD_CLONE_ALL;
  if (flags & FORK_SHARE_VMAP)
    cflags |= WARD_CLONE_SHARE_VMAP;
  if (flags & FORK_SHARE_FD)
    cflags |= WARD_CLONE_SHARE_FTABLE;
  proc *p = doclone(cflags);
  if (!p)
    return -1;
  return p->pid;
}

//SYSCALL
pid_t
sys_fork(void)
{
  return sys_fork_flags(0);
}

//SYSCALL
long sys_clone(unsigned long flags, uintptr_t stack, userptr<int> parent_tid, uintptr_t child_tid, unsigned long tls)
{
  STRACE_PARAMS("0x%lx, %p, %p, %p, 0x%lx", flags, stack, parent_tid, child_tid, tls);

  // fork process:
  // CLONE_CHILD_SETTID | CLONE_CHILD_CLEARTID

  // fork thread:
  // CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND |
  // CLONE_PARENT_SETTID | CLONE_CHILD_CLEARTID | CLONE_SETTLS | CLONE_THREAD

  if (flags & CLONE_THREAD) {
    // TODO: Actually handle thread groups (i.e. TIDs and TGIDs)
  }

  if (flags & CLONE_SIGHAND) {
    // TODO
  }

  if (flags & CLONE_FS) {
    // TODO
  }

  ensure_secrets();
  clone_flags cflags = clone_flags::WARD_CLONE_ALL;
  if (flags & CLONE_VM)
    cflags |= WARD_CLONE_SHARE_VMAP;
  if (flags & CLONE_FILES)
    cflags |= WARD_CLONE_SHARE_FTABLE;

  proc *p = doclone(cflags | WARD_CLONE_NO_RUN);
  if (!p)
    return -EINVAL;

  if (flags & CLONE_SETTLS)
    p->user_fs_ = tls;

  if (stack)
    p->tf->rsp = stack;

  if (flags & CLONE_CHILD_SETTID) {
    static_assert(sizeof(int) == sizeof(pid_t), "'pid_t' is not 'int'");
    if(p->vmap->safe_write(child_tid, (char*)&p->pid, sizeof(int)) < sizeof(int))
      return -EFAULT;
  }

  if (flags & CLONE_PARENT_SETTID && !parent_tid.store((int*)&p->pid))
    return -EFAULT;

  if (flags & CLONE_CHILD_CLEARTID)
    p->tid_address = userptr<u32>((u32*)child_tid);

  acquire(&p->lock);
  addrun(p);
  release(&p->lock);

  return p->pid;
}

//SYSCALL {"noret":true}
void
sys_exit(int status)
{
#if KERNEL_STRACE
  if (strcmp(myproc()->name, STRACE_BINARY_NAME) == 0) {
    cprintf("\033[33m%d %s: exit(%d)\033[0m\n", myproc()->pid, myproc()->name, status);
  }
#endif

  procexit(status);
  panic("procexit() returned");
}

//SYSCALL {"noret":true}
void
sys_exit_group(int status)
{
  // TODO: exit other threads in group
  procexit(status);
  panic("procexit() returned");
}

//SYSCALL
pid_t
sys_waitpid(int pid,  userptr<int> status, int options)
{
  return wait(pid, status);
}

//SYSCALL
pid_t
sys_wait(userptr<int> status)
{
  return wait(-1, status);
}

//SYSCALL
pid_t
sys_wait4(pid_t pid, userptr<int> wstatus, int options, void* rusage)
{
  // TODO: actually consider options and rusage
  return wait(pid, wstatus);
}

//SYSCALL
long
sys_kill(int pid)
{
  return proc::kill(pid);
}

//SYSCALL {"nosec": true}
pid_t
sys_getpid(void)
{
  return myproc()->pid;
}

//SYSCALL
long
sys_gettid(void)
{
  return myproc()->pid;
}

//SYSCALL
char*
sys_sbrk(intptr_t n)
{
  uptr addr;

  if(myproc()->vmap->sbrk(n, &addr) < 0)
    return (char*)-1;
  return (char*)addr;
}

//SYSCALL
char*
sys_brk(char *ptr)
{
  return (char *) myproc()->vmap->brk((uptr) ptr);
}

//SYSCALL
long
sys_nsleep(u64 nsec)
{
  struct spinlock lock("sleep_lock");
  struct condvar cv("sleep_cv");
  u64 nsecto;

  scoped_acquire l(&lock);
  nsecto = nsectime()+nsec;
  while (nsecto > nsectime()) {
    if (myproc()->killed)
      return -1;
    cv.sleep_to(&lock, nsecto);
  }
  return 0;
}

// return how many clock tick interrupts have occurred
// since boot.
//SYSCALL
u64
sys_uptime(void)
{
  return nsectime();
}

//SYSCALL
void *
sys_mmap(userptr<void> addr, size_t len, int prot, int flags, int fd,
         off_t offset)
{
  sref<pageable> m;

  // TODO: handle unmapped regions
  prot |= PROT_READ;

  if (!(prot & (PROT_READ | PROT_WRITE))) {
    cprintf("not implemented: !(prot & (PROT_READ | PROT_WRITE))\n");
    return MAP_FAILED;
  }

  if (flags & MAP_ANONYMOUS) {
    if (offset)
      return MAP_FAILED;

    if (flags & MAP_SHARED) {
      m = new_shared_memory_region((len + PGSIZE - 1) / PGSIZE);
    }
  } else {
    sref<file> f = myproc()->ftable->getfile(fd);
    if (!f)
      return MAP_FAILED;

    auto v = f->get_vnode();
    if (!v || !v->is_regular_file())
      return MAP_FAILED;
    m = v;
  }

  uptr start = PGROUNDDOWN((uptr)addr);
  uptr end = PGROUNDUP((uptr)addr + len);

  if ((flags & MAP_FIXED) && start != (uptr)addr)
    return MAP_FAILED;

  vmdesc desc;
  if (!m) {
    desc = vmdesc::anon_desc();
  } else {
    desc = vmdesc(m, start - offset);
  }
  if (!(prot & PROT_WRITE))
    desc.flags &= ~vmdesc::FLAG_WRITE;
  if (flags & MAP_SHARED)
    desc.flags |= vmdesc::FLAG_SHARED;
  if (m && (flags & MAP_PRIVATE))
    desc.flags |= vmdesc::FLAG_COW;
  uptr r = myproc()->vmap->insert(std::move(desc), start, end - start);
  return (void*)r;
}

//SYSCALL
long
sys_munmap(userptr<void> addr, size_t len)
{
  uptr align_addr = PGROUNDDOWN((uptr)addr);
  uptr align_len = PGROUNDUP((uptr)addr + len) - align_addr;
  if (myproc()->vmap->remove(align_addr, align_len) < 0)
    return -1;

  return 0;
}

//SYSCALL
long
sys_madvise(userptr<void> addr, size_t len, int advice)
{
  uptr align_addr = PGROUNDDOWN((uptr)addr);
  uptr align_len = PGROUNDUP((uptr)addr + len) - align_addr;

  switch (advice) {
  case MADV_WILLNEED:
    if (myproc()->vmap->willneed(align_addr, align_len) < 0)
      return -1;
    return 0;

  case MADV_DONTNEED:
    if (myproc()->vmap->dontneed(align_addr, align_len) < 0)
      return -1;
    return 0;

  case MADV_INVALIDATE_CACHE:
    if (myproc()->vmap->invalidate_cache(align_addr, align_len) < 0)
      return -1;
    return 0;

  default:
    return -1;
  }
}

//SYSCALL
long
sys_mprotect(userptr<void> addr, size_t len, int prot)
{
  if ((uptr)addr % PGSIZE)
    return -1;                  // EINVAL
  if ((uptr)addr + len >= USERTOP || (uptr)addr + (uptr)len < (uptr)addr)
    return -1;                  // ENOMEM
  if (!(prot & (PROT_READ | PROT_WRITE | PROT_EXEC))) {
    cprintf("not implemented: PROT_NONE\n");
    return -1;
  }

  uptr align_addr = PGROUNDDOWN((uptr)addr);
  uptr align_len = PGROUNDUP((uptr)addr + len) - align_addr;
  uint64_t flags = 0;
  if (prot & PROT_WRITE)
    flags |= vmdesc::FLAG_WRITE;

  return myproc()->vmap->mprotect(align_addr, align_len, flags);
}

//SYSCALL {"noret":true}
void
sys_halt(int code)
{
  paravirtual_exit(code);
  panic("halt returned");
}

//SYSCALL {"noret":true}
void
sys_reboot(void)
{
  acpi_reboot();
  halt();
  panic("halt returned");
}

//SYSCALL
long
sys_cpuhz(void)
{
  return (mycpu()->tsc_period * 1000000000) / TSC_PERIOD_SCALE;
}

//SYSCALL
long
sys_setfs(u64 base)
{
  proc *p = myproc();
  p->user_fs_ = base;
  return 0;
}

//SYSCALL
long
sys_setaffinity(int cpu)
{
  return myproc()->set_cpu_pin(cpu);
}

//SYSCALL
long
sys_sched_getaffinity(pid_t pid, size_t cpusetsize, userptr<char> mask)
{
  if (pid != 0 && pid != myproc()->pid)
    return -EINVAL;

  u8 mask_copy[(NCPU+7) / 8] = { 0 };

  if (myproc()->cpu_pin) {
    mask_copy[myproc()->cpuid / 8] |= 1 << (myproc()->cpuid / 8);
  } else {
    for (int i = 0; i < ncpu; i++) {
      mask_copy[i / 8] |= 1 << (i % 8);
    }
  }

  if (cpusetsize > sizeof(mask_copy))
    cpusetsize = sizeof(mask_copy);
  if (mask.store((char*)&mask_copy[0], cpusetsize))
    return cpusetsize;
  return -EFAULT;
}

//SYSCALL
long
sys_sched_setaffinity(pid_t pid, size_t cpusetsize, userptr<char> mask)
{
  if (pid != 0 && pid != myproc()->pid)
    return -EINVAL;

  u8 mask_copy[(NCPU+7) / 8] = { 0 };

  if (cpusetsize > sizeof(mask_copy))
    cpusetsize = sizeof(mask_copy);
  mask.load((char*)mask_copy, cpusetsize);

  int nset = 0;
  int cpu = -1;
  for (int i = 0; i < ncpu; i++) {
    if (mask_copy[i/8] & (1 << (i%8))) {
      cpu = i;
      nset++;
    }
  }

  if (nset == ncpu)
    return myproc()->set_cpu_pin(-1);
  else if (nset == 1)
    return myproc()->set_cpu_pin(cpu);
  else
    panic("Pinning to multiple cores unsupported");
}

//SYSCALL
long
sys_futex(uintptr_t addr, int op, u32 val, u64 timer)
{
  if ((addr & 3) != 0)
    return -EINVAL;

  futexkey key(addr, myproc()->vmap, op & FUTEX_PRIVATE_FLAG);
  switch(op & 1) {
  case FUTEX_WAIT:
    return futexwait(std::move(key), val, timer);
  case FUTEX_WAKE:
    return futexwake(std::move(key), val);
  default:
    return -EINVAL;
  }
}

//SYSCALL
long
sys_set_robust_list(intptr_t ptr) {
  myproc()->robust_list_ptr = userptr<robust_list_head>((robust_list_head*)ptr);
  return 0;
}

//SYSCALL
long
sys_sched_yield(void)
{
  yield();
  return 0;
}

//SYSCALL
long
sys_uname(userptr<struct utsname> buf)
{
  static struct utsname uts
  {
    "ward",
#define xstr(s) str(s)
#define str(s) #s
      xstr(XV6_HW),
#undef xstr
#undef str
    "9: ", // binaries linked with glibc abort the first character isn't a large enough number.
    "",
    "x86_64"
  };
  static bool initialized;
  if (!initialized) {
    strncpy(uts.version, kmeta::version_string(), sizeof(uts.version) - 1);
    strncpy(uts.release+3, kmeta::release_string(), sizeof(uts.release) - 4);
    initialized = true;
  }
  if (!buf.store(&uts))
    return -1;
  return 0;
}

// XXX(Austin) This is a hack for benchmarking.  See vmap::dup_page.
//SYSCALL
long
sys_dup_page(userptr<void> dest, userptr<void> src)
{
  return myproc()->vmap->dup_page((uptr)dest, (uptr)src);
}

//SYSCALL
long
sys_sigaction(int signo, userptr<struct sigaction> act, userptr<struct sigaction> oact)
{
  if (signo < 0 || signo >= NSIG)
    return -1;

  if (oact && !oact.store(&myproc()->sig[array_index_nospec(signo, NSIG)]))
    return -1;
  if (act && !act.load(&myproc()->sig[array_index_nospec(signo, NSIG)]))
    return -1;
  return 0;
}

static u32 microcode_rev(){
  writemsr(MSR_INTEL_UCODE_REV, 0);
  cpuid(1, NULL, NULL, NULL, NULL);
  return readmsr(MSR_INTEL_UCODE_REV) >> 32;
}

//SYSCALL
u64
sys_cpu_info(void)
{
  if (!cpuid::vendor_is_intel())
    return (u64)-1;

  u64 rev = microcode_rev();
  auto model = cpuid::model();

  return (rev << 32) |
    ((model.family & 0xfff) << 12) |
    ((model.model & 0xff) << 4) |
    (model.stepping & 0xf);
}

//SYSCALL
long
sys_update_microcode(const void* data, u64 len)
{
  if (len < 48 || len > 0x100000)
    return -1;

  if (!cpuid::vendor_is_intel())
    return -1;

  char* microcode = kalloc("microcode", 0x100000);
  if(fetchmem(microcode, data, len)) {
    kfree(microcode);
    return -1;
  }

  if (((u32*)microcode)[3] != cpuid::get_leaf(cpuid::leafid::features).a)
    return -1;

  auto install_microcode = [microcode]() -> bool {
    u32 initial_rev = microcode_rev();
    writemsr(MSR_INTEL_UCODE_WRITE, (u64)microcode + 48);
    return microcode_rev() > initial_rev;
  };

  bitset<NCPU> remote_cpus;
  {
    scoped_cli cli;

    if (!install_microcode()) {
      kfree(microcode);
      return -1;
    }

    for (cpuid_t i = 0; i < ncpu; i++) {
      if (i != myid())
        remote_cpus.set(i);
    }
  }

  run_on_cpus(remote_cpus, install_microcode);
  kfree(microcode);
  return 0;
}

//SYSCALL
long
sys_cmdline_view_param(userptr_str name)
{
  char name_copy[64];
  if (!name.load(name_copy, sizeof(name_copy)))
    return -1;

  return cmdline_view_param(name_copy);
}

//SYSCALL
long
sys_cmdline_change_param(userptr_str name, userptr_str value)
{
  char name_copy[64], value_copy[64];
  if (!name.load(name_copy, sizeof(name_copy)))
    return -1;
  if (!value.load(value_copy, sizeof(value_copy)))
    return -1;

  return cmdline_change_param(name_copy, value_copy);
}

//SYSCALL
long
sys_arch_prctl(int code, userptr<u64> addr)
{
  if (code == 0x1001) {
    cprintf("arch_prctl(ARCH_SET_GS) is unimplemented\n");
    return -1;
  } else if(code == 0x1002) {
    myproc()->user_fs_ = (uptr)addr;
    return 0;
  } else if(code == 0x1003) {
    cprintf("arch_prctl(ARCH_SET_GS) is unimplemented\n");
    return -1;
  } else if(code == 0x1004) {
    addr.store(&myproc()->user_fs_);
    return 0;
  }
  return -1;
}

//SYSCALL
long
sys_sigprocmask(int how, userptr<u32> set, userptr<u32> oldset)
{
  if (oldset) {
    oldset.store(&myproc()->blocked_signals);
  }

  if (set) {
    u32 v;
    set.load(&v);

    if (how == 0) // SIG_BLOCK
      myproc()->blocked_signals |= v & (~((1<<SIGKILL) | (1<<SIGSTOP)));
    else if (how == 1) // SIG_UNBLOCK
      myproc()->blocked_signals &= ~v;
    else if (how == 2) // SIG_SETMASK
      myproc()->blocked_signals = v & (~((1<<SIGKILL) | (1<<SIGSTOP)));
    else
      return -1;

    u32 pending = myproc()->pending_signals;
    u32 blocked = myproc()->blocked_signals;
    u32 activated = pending & (~blocked);
    if (activated != 0) {
      for (int i = 0; i < NSIG; i++) {
        if ((1<<i) & activated) {
          myproc()->deliver_signal(i);
          break;
        }
      }
    }
  }

  return 0;
}

//SYSCALL
long
sig_tgkill(int pid, int tid, int sig)
{
  if (pid != tid)
    return -1;

  proc::deliver_signal(pid, sig);
  return 0;
}

//SYSCALL
long
sys_set_tid_address(userptr<u32> tid_address)
{
  myproc()->tid_address = tid_address;
  return 0;
}


//SYSCALL
long
sys_display_image(userptr<u32> data, unsigned int width, unsigned int height) {
  if (width > 2048 || height > 2048)
    return -1;

  u32* image = (u32*)kalloc("display_image", 2048 * 2048 * 4);
  auto cleanup = scoped_cleanup([&](){ kfree(image, 2048 * 2048 * 4); });

  if (!data.load(image, width * height))
    return -1;

  vga_put_image(image, width, height);
  return 0;
}

static volatile int t = 0;

//SYSCALL
long
sys_transparent_barrier(int n)
{
  return t ^ n;
}

//SYSCALL
long
sys_intentional_barrier(int n)
{
  ensure_secrets();
  return t ^ n;
}
