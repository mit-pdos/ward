#include <atomic>
#include "percpu.hh"
#include "ref.hh"

class filetable : public referenced {
private:
  static const int cpushift = 16;
  static const int fdmask = (1 << cpushift) - 1;

public:
  static sref<filetable> alloc() {
    return sref<filetable>::transfer(new filetable());
  }

  sref<filetable> copy(bool close_cloexec = false) {
    filetable* t = new filetable(false);

    fdinfo init(nullptr, false);
    for(int cpu = 0; cpu < NCPU; cpu++) {
      for(int fd = 0; fd < NOFILE; fd++) {
        // XXX Relaxed load?
        fdinfo info;
        // Avoid reading info_ altogether if we're closing cloexec FDs
        // and this is a cloexec FD.
        if (close_cloexec && cloexec_[cpu][fd])
          info = init;
        else
          info = info_[cpu][fd].load();
        file *f = info.get_file();
        if (f && (!close_cloexec || !info.get_cloexec())) {
          // XXX f's refcount could have dropped to zero between the
          // load and here
          f->inc();
          t->info_[cpu][fd].store(
            info.with_locked(false), std::memory_order_relaxed);
          t->cloexec_[cpu][fd].store(
            info.get_cloexec(), std::memory_order_relaxed);
        } else {
          t->info_[cpu][fd].store(init, std::memory_order_relaxed);
          t->cloexec_[cpu][fd].store(true, std::memory_order_relaxed);
        }
      }
    }
    std::atomic_thread_fence(std::memory_order_release);
    return sref<filetable>::transfer(t);
  }

  // Return the file referenced by FD fd.  If fd is not open, returns
  // sref<file>().
  sref<file> getfile(int fd) {
    int cpu = fd >> cpushift;
    fd = fd & fdmask;

    if (cpu < 0 || cpu >= NCPU)
      return sref<file>();

    if (fd < 0 || fd >= NOFILE)
      return sref<file>();

    // XXX This isn't safe: there could be a concurrent close that
    // drops the reference count to zero.
    file* f = info_[cpu][fd].load().get_file();
    return sref<file>::newref(f);
  }

  int allocfd(struct file *f, bool percpu = false, bool cloexec = false) {
    int cpu = percpu ? myid() : 0;
    fdinfo none(nullptr, false);
    fdinfo newinfo(f, cloexec, true);
    for (int fd = 0; fd < NOFILE; fd++) {
      // Note that we skip over locked FDs because that means they're
      // either non-null or about to be.
      if (info_[cpu][fd].load(std::memory_order_relaxed) == none &&
          cmpxch(&info_[cpu][fd], none, newinfo)) {
        // The default state of cloexec_ is 'true', so we only need to
        // write to it if this is a keep-exec FD.
        if (!cloexec)
          cloexec_[cpu][fd] = cloexec;
        // Unlock FD
        info_[cpu][fd].store(newinfo.with_locked(false),
                             std::memory_order_release);
        return (cpu << cpushift) | fd;
      }
    }
    cprintf("filetable::allocfd: failed\n");
    return -1;
  }

  void close(int fd) {
    // XXX(sbw) if f->ref_ > 1 the kernel will not actually close 
    // the file when this function returns (i.e. sys_close can return 
    // while the file/pipe/socket is still open).
    int cpu = fd >> cpushift;
    fd = fd & fdmask;

    if (cpu < 0 || cpu >= NCPU) {
      cprintf("filetable::close: bad fd cpu %u\n", cpu);
      return;
    }

    if (fd < 0 || fd >= NOFILE) {
      cprintf("filetable::close: bad fd %u\n", fd);
      return;
    }

    // Lock the FD to prevent concurrent modifications
    std::atomic<fdinfo> *infop = &info_[cpu][fd];
    fdinfo info = lock_fdinfo(infop);

    // Clear cloexec_ back to default state of 'true'
    if (!cloexec_[cpu][fd])
      cloexec_[cpu][fd] = true;

    // Update and unlock the FD
    fdinfo newinfo(nullptr, false);
    infop->store(newinfo, std::memory_order_release);

    // Close old file
    if (info.get_file())
      info.get_file()->dec();
    else
      cprintf("filetable::close: bad fd %u\n", fd);
  }

  bool replace(int fd, struct file* newf, bool cloexec = false) {
    assert(newf);

    int cpu = fd >> cpushift;
    fd = fd & fdmask;

    if (cpu < 0 || cpu >= NCPU) {
      cprintf("filetable::replace: bad fd cpu %u\n", cpu);
      return false;
    }

    if (fd < 0 || fd >= NOFILE) {
      cprintf("filetable::replace: bad fd %u\n", fd);
      return false;
    }

    // Lock the FD to prevent concurrent modifications
    std::atomic<fdinfo> *infop = &info_[cpu][fd];
    fdinfo oldinfo = lock_fdinfo(infop);

    // Update to new info and unlock.  It's safe to update cloexec_
    // non-atomically with info even with concurrent lock-free readers
    // because any that care will double-check the fdinfo bit.
    fdinfo newinfo(newf, cloexec);
    if (cloexec != cloexec_[cpu][fd])
      cloexec_[cpu][fd] = cloexec;
    infop->store(newinfo, std::memory_order_release);

    if (oldinfo.get_file() && oldinfo.get_file() != newf)
      oldinfo.get_file()->dec();
    return true;
  }

private:
  filetable(bool clear = true) {
    if (!clear)
      return;
    fdinfo none(nullptr, false);
    for(int cpu = 0; cpu < NCPU; cpu++) {
      for(int fd = 0; fd < NOFILE; fd++) {
        info_[cpu][fd].store(none, std::memory_order_relaxed);
        cloexec_[cpu][fd].store(true, std::memory_order_relaxed);
      }
    }
    std::atomic_thread_fence(std::memory_order_release);
  }

  ~filetable() {
    for(int cpu = 0; cpu < NCPU; cpu++){
      for(int fd = 0; fd < NOFILE; fd++){
        fdinfo info = info_[cpu][fd].load();
        if (info.get_file())
          info.get_file()->dec();
      }
    }
  }

  filetable& operator=(const filetable&);
  filetable(const filetable& x);
  NEW_DELETE_OPS(filetable);  

  class fdinfo
  {
    uintptr_t data_;

    constexpr fdinfo(uintptr_t data) : data_(data) { }

  public:
    fdinfo() = default;

    fdinfo(file* fp, bool cloexec, bool locked = false)
      : data_((uintptr_t)fp | (uintptr_t)cloexec | ((uintptr_t)locked << 1)) { }

    file* get_file() const
    {
      return (file*)(data_ & ~3);
    }

    bool get_cloexec() const
    {
      return data_ & 1;
    }

    bool get_locked() const
    {
      return data_ & 2;
    }

    fdinfo with_locked(bool locked)
    {
      return fdinfo((data_ & ~2) | ((uintptr_t)locked << 1));
    }

    bool operator==(const fdinfo &o) const
    {
      return data_ == o.data_;
    }

    bool operator!=(const fdinfo &o) const
    {
      return data_ != o.data_;
    }
  };

  fdinfo lock_fdinfo(std::atomic<fdinfo> *infop)
  {
    fdinfo info, newinfo;
    while (true) {
      info = infop->load(std::memory_order_relaxed);
    retry:
      if (info.get_locked())
        nop_pause();
      else
        break;
    }
    if (!infop->compare_exchange_weak(info, info.with_locked(true)))
      goto retry;
    return info;
  }

  percpu<std::atomic<fdinfo>[NOFILE]> info_;
  // In addition to storing O_CLOEXEC with each fdinfo so it can be
  // read atomically with the FD, we store it separately so we can
  // scan for keep-exec FDs without reading from info_, which would
  // cause unnecessary sharing between the scan and creating O_CLOEXEC
  // FDs.  To avoid unnecessary sharing on this array itself, the
  // *default* state of this array for closed FDs must be 'true', so
  // we only have to write to it when opening a keep-exec FD.
  // Modifications to this array are protected by the fdinfo lock.
  // Lock-free readers should double-check the O_CLOEXEC bit in
  // fdinfo.
  percpu<std::atomic<bool>[NOFILE]> cloexec_;
};
