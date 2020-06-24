#include <atomic>
#include "percpu.hh"
#include "ref.hh"
// XXX If we move the filetable implementation to a source file, we
// won't need file.hh
#include "file.hh"

class filetable : public referenced {
public:
  static sref<filetable> alloc() {
    return sref<filetable>::transfer(new filetable());
  }

  sref<filetable> copy(bool cloexec = false) {
    filetable* t = new filetable();

    scoped_acquire lk(&lock_);
    t->open_ = open_;
    t->cloexec_ = cloexec_;
    for(int fd = 0; fd < NOFILE; fd++)
      t->table_[fd] = table_[fd];
    for(int fd = 0; fd < NOFILE; fd++) {
        t->entries_[fd].refcount = entries_[fd].refcount;
        t->entries_[fd].f = entries_[fd].f;
    }

    if (cloexec) {
      t->close_cloexec();
    }

    return sref<filetable>::transfer(t);
  }

  void close_cloexec() {
    scoped_acquire lk(&lock_);
    for (int fd = 0; fd < NOFILE; fd++) {
      if (open_[fd] && cloexec_[fd]) {
        __replace(fd, sref<file>());
      }
    }
  }

  // Return the file referenced by FD fd.  If fd is not open, returns
  // sref<file>().
  sref<file> getfile(int fd) {
    if (fd < 0 || fd >= NOFILE)
      return sref<file>();

    scoped_acquire lk(&lock_);
    return open_[fd] ? entries_[table_[fd]].f : sref<file>();
  }

  // Allocate a FD and point it to f.  This takes over the reference
  // to f from the caller.
  int allocfd(sref<file>&& f, int minfd, bool cloexec) {
    scoped_acquire lk(&lock_);

    for (int fd = minfd; fd < NOFILE; fd++) {
      if (!open_[fd]) {
        __replace(fd, std::move(f), cloexec);
        return fd;
      }
    }
    cprintf("filetable::allocfd: failed\n");
    return -1;
  }

  void close(int fd) {
    replace(fd, sref<file>());
  }

  bool replace(int fd, sref<file>&& newf, bool cloexec = false) {
    if (fd < 0 || fd >= NOFILE) {
      cprintf("filetable::replace: bad fd %u\n", fd);
      return false;
    }

    scoped_acquire lk(&lock_);
    return __replace(fd, std::move(newf), cloexec);
  }

private:
  filetable() {
    open_.reset();
    for(int i = 0; i < NOFILE; i++){
      entries_[i].f = sref<file>();
    }
  }

  ~filetable() {
    scoped_acquire lk(&lock_);
    for(int i = 0; i < NOFILE; i++){
      if (entries_[i].f) {
        entries_[i].f->pre_close();
        entries_[i].f.reset();
      }
    }
  }

  bool __replace(int fd, sref<file>&& newf, bool cloexec = false) {
    if (open_[fd]) {
      int i = table_[fd];
      if (--entries_[i].refcount == 0) {
        entries_[i].f->pre_close();
        entries_[i].f.reset();
      }
    }

    if (newf) {
      int i = alloc_entry();
      entries_[i].refcount = 1;
      entries_[i].f = std::move(newf);
      table_[fd] = i;
      open_.set(fd);
      cloexec_.set(fd, cloexec);
    } else {
      open_.reset(fd);
    }
    return true;
  }

  u16 alloc_entry() {
    for (auto i = 0; i < NOFILE; i++) {
      if (!entries_[i].f) {
        return i;
      }
    }
    panic("filetable: out of entries");
  }

  filetable& operator=(const filetable&) = delete;
  filetable(const filetable& x) = delete;
  filetable& operator=(filetable &&) = delete;
  filetable(filetable &&) = delete;
  NEW_DELETE_OPS(filetable);

  struct entry {
    u16 refcount;
    sref<file> f;
  };

  spinlock lock_;

  u16 table_[NOFILE];
  bitset<NOFILE> open_;
  bitset<NOFILE> cloexec_;

  entry entries_[NOFILE];
};
