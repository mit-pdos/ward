#include <atomic>
#include <optional>
#include "percpu.hh"
#include "ref.hh"
// XXX If we move the filetable implementation to a source file, we
// won't need file.hh
#include "file.hh"
#include "errno.h"

const u64 FILETABLE_SIZE = 450;

class filetable_entries {
  struct entry {
    u16 refcount = 0;
    u16 partner_index = 0;
    bool mapped : 1 = false;
    bool has_partner : 1 = false;
    sref<file> f = sref<file>();

    entry() {}
    entry(sref<file>&& _f) : f(_f), refcount(1) {}
    entry(sref<file>&& _f, u16 partner) : f(_f), refcount(1), partner_index(partner),
                                          has_partner(true) {}
  };
  entry entries_[FILETABLE_SIZE];

  int alloc(int min = 0) {
    for (int i = min; i < FILETABLE_SIZE; i++)
      if (!entries_[i].f)
        return i;

    panic("filetable: out of entries");
  }

  void remove(int i, filetable* t) {
    if (entries_[i].has_partner)
      entries_[entries_[i].partner_index].has_partner = false;
    else if (entries_[i].mapped)
      entries_[i].f->on_ftable_remove(t);

    entries_[i] = entry();
  }

public:
  filetable_entries() = default;
  filetable_entries(filetable_entries&&) = default;
  filetable_entries(const filetable_entries&) = delete;
  filetable_entries& operator=(const filetable_entries& other) = delete;
  filetable_entries& operator=(filetable_entries&& other) = default;
  ~filetable_entries() = default;

  filetable_entries copy() {
    filetable_entries t;
    for (int i = 0; i < FILETABLE_SIZE; i++) {
      t.entries_[i] = entries_[i];
      t.entries_[i].mapped = false;
    }
    return t;
  }

  int insert(sref<file>&& f) {
    int i = alloc();
    entries_[i] = entry(std::move(f));
    return i;
  }

  void insert_pair(sref<file>&& f1, sref<file>&& f2, int* e1, int* e2) {
    *e1 = alloc();
    *e2 = alloc(*e1 + 1);

    entries_[*e1] = entry(std::move(f1)/*, *e2*/);
    entries_[*e2] = entry(std::move(f2)/*, *e1*/);
  }

  void increment(int i) {
    entries_[i].refcount++;
  }

  void decrement(int i, filetable* t) {
    if (--entries_[i].refcount == 0)
      remove(i, t);
  }

  void clear(filetable* t) {
    for(int i = 0; i < FILETABLE_SIZE; i++)
      if (entries_[i].f)
        remove(i, t);
  }

  const sref<file>& get(int i, filetable* t) {
    if (entries_[i].f && !entries_[i].mapped) {
      entries_[i].f->on_ftable_insert(t);
      entries_[i].mapped = true;
      if (entries_[i].has_partner)
        entries_[entries_[i].partner_index].mapped = true;
    }
    return entries_[i].f;
  }
};

class filetable : public referenced {
public:
  static sref<filetable> alloc(sref<vmap> v) {
    return sref<filetable>::transfer(new filetable(v));
  }

  sref<filetable> copy(sref<vmap> v) {
    filetable* t = new filetable(v);

    scoped_acquire lk(&lock_);

    t->open_ = open_;
    t->cloexec_ = cloexec_;
    t->entries_ = entries_.copy();

    for(int fd = 0; fd < FILETABLE_SIZE; fd++)
      t->table_[fd] = table_[fd];

    return sref<filetable>::transfer(t);
  }

  void close_cloexec() {
    scoped_acquire lk(&lock_);
    for (int fd = 0; fd < FILETABLE_SIZE; fd++) {
      if (open_[fd] && cloexec_[fd]) {
        __replace(fd, sref<file>());
      }
    }
  }

  // Return the file referenced by FD fd.  If fd is not open, returns
  // sref<file>().
  sref<file> getfile(int fd) {
    if (fd < 0 || fd >= FILETABLE_SIZE)
      return sref<file>();

    scoped_acquire lk(&lock_);
    if (!open_[fd])
      return sref<file>();

    auto f = entries_.get(table_[fd], this);
    return f;
  }

  // Allocate a FD and point it to f.  This takes over the reference
  // to f from the caller.
  int allocfd(sref<file>&& f, int minfd, bool cloexec) {
    scoped_acquire lk(&lock_);

    for (int fd = minfd; fd < FILETABLE_SIZE; fd++) {
      if (!open_[fd]) {
        __replace(fd, std::move(f), cloexec);
        return fd;
      }
    }
    cprintf("filetable::allocfd: failed\n");
    return -1;
  }

  void alloc_pair(sref<file>&& f1, sref<file>&& f2, int* fd1, int* fd2, bool cloexec) {
    scoped_acquire lk(&lock_);

    *fd1 = -1;
    *fd2 = -1;
    for (int i = 0; i < FILETABLE_SIZE; i++) {
      if (open_[i])
        continue;

      if (*fd1 == -1) {
        *fd1 = i;
      } else {
        *fd2 = i;
        break;
      }
    }
    assert(*fd2 != -1);

    int e1, e2;
    entries_.insert_pair(std::move(f1), std::move(f2), &e1, &e2);

    table_[*fd1] = e1;
    open_.set(*fd1);
    cloexec_.set(*fd1, cloexec);

    table_[*fd2] = e2;
    open_.set(*fd2);
    cloexec_.set(*fd2, cloexec);
  }

  bool close(int fd) {
    if (fd < 0 || fd >= FILETABLE_SIZE) {
      return false;
    }

    scoped_acquire lk(&lock_);

    if (!open_[fd])
      return false;

    __replace(fd, sref<file>());
    return true;
  }

  long dup(int ofd) {
    scoped_acquire lk(&lock_);

    if (ofd < 0 || ofd >= FILETABLE_SIZE || !open_[ofd])
      return -EBADF;

    for (int nfd = 0; nfd < FILETABLE_SIZE; nfd++) {
      if (!open_[nfd]) {
        open_.set(nfd);
        cloexec_.reset(nfd);
        table_[nfd] = table_[ofd];
        entries_.increment(table_[nfd]);
        return nfd;
      }
    }
  }

  long dup2(int ofd, int nfd) {
    scoped_acquire lk(&lock_);

    if (ofd < 0 || ofd >= FILETABLE_SIZE || nfd < 0 || nfd >= FILETABLE_SIZE || !open_[ofd])
      return -EBADF;

    if (ofd == nfd)
      // Do nothing, aggressively.  Remarkably, while dup2 usually
      // clears O_CLOEXEC on nfd (even if ofd is O_CLOEXEC), POSIX 2013
      // is very clear that it should *not* do this if ofd == nfd.
      return nfd;

    if (open_[nfd]) {
      __replace(nfd, sref<file>());
    }

    open_.set(nfd);
    cloexec_.reset(nfd);
    table_[nfd] = table_[ofd];
    entries_.increment(table_[nfd]);

    return nfd;
  }

  bool set_cloexec(int fd, bool value) {
    if (fd < 0 || fd >= FILETABLE_SIZE || !open_[fd])
      return false;

    cloexec_.set(fd, value);
    return true;
  }

  vmap* get_vmap() { return vmap_.get(); }

private:
  filetable(sref<vmap> v): vmap_(v) {
    for (int i = 0; i < FILETABLE_SIZE; i++)
      assert(!open_[i]);
  }

  virtual ~filetable() {
    entries_.clear(this);
  }

  void __replace(int fd, sref<file>&& newf, bool cloexec = false) {
    if (open_[fd]) {
      entries_.decrement(table_[fd], this);
    }

    if (newf) {
      table_[fd] = entries_.insert(std::move(newf));
      open_.set(fd);
      cloexec_.set(fd, cloexec);
    } else {
      open_.reset(fd);
    }
  }

  filetable& operator=(const filetable&) = delete;
  filetable(const filetable& x) = delete;
  filetable& operator=(filetable &&) = delete;
  filetable(filetable &&) = delete;
  NEW_DELETE_OPS(filetable);

  sref<vmap> vmap_;

  spinlock lock_;

  filetable_entries entries_;
  u16 table_[FILETABLE_SIZE];
  bitset<FILETABLE_SIZE> open_;
  bitset<FILETABLE_SIZE> cloexec_;

};
