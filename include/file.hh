#pragma once

#include "cpputil.hh"
#include "ns.hh"
#include "gc.hh"
#include <atomic>
#include "refcache.hh"
#include "eager_refcache.hh"
#include "condvar.hh"
#include "semaphore.hh"
#include "seqlock.hh"
#include "sleeplock.hh"
#include "vfs.hh"
#include <uk/unistd.h>

class dirns;
class filetable;

u64 namehash(const strbuf<DIRSIZ>&);

// lwIP and musl have conflicting definitions for these types. These versions
// match musl (and thus by extension Linux), but are prefixed so they don't 
// conflict with the lwIP versions are in scope.
#define WARD_AF_INET         2
#define WARD_AF_INET6        10
typedef unsigned short ward_sa_family_t;
struct ward_sockaddr {
	ward_sa_family_t sa_family;
	char sa_data[14];
};
struct ward_sockaddr_storage {
	ward_sa_family_t ss_family;
	char __ss_padding[128-sizeof(long)-sizeof(ward_sa_family_t)];
	unsigned long __ss_align;
};

struct file {

  virtual void on_ftable_insert(filetable* v) {}
  virtual void on_ftable_remove(filetable* v) {}

  virtual int stat(struct kernel_stat*, enum stat_flags) { return -1; }
  virtual ssize_t read(char *addr, size_t n) { return -1; }
  virtual ssize_t write(const userptr<void> data, size_t n) { return -1; }
  virtual ssize_t pread(char *addr, size_t n, off_t offset) { return -1; }
  virtual ssize_t pwrite(const userptr<void> data, size_t n, off_t offset) { return -1; }

  // Directory operations
  virtual ssize_t getdents(linux_dirent* out_dirents, size_t bytes) { return -1; }

  // Socket operations
  virtual int bind(const struct ward_sockaddr *addr, size_t addrlen) { return -1; }
  virtual int listen(int backlog) { return -1; }
  // Unlike the syscall, the return is only an error status.  The
  // caller will allocate an FD for *out on success.  addrlen is only
  // an out-argument.
  virtual int accept(struct ward_sockaddr_storage *addr, size_t *addrlen, file **out)
  { return -1; }
  // sendto and recvfrom take a userptr to the buf to avoid extra
  // copying in the kernel.  The other pointers will be kernel
  // pointers.  dest_addr may be null.
  virtual ssize_t sendto(userptr<void> buf, size_t len, int flags,
                         const struct ward_sockaddr *dest_addr, size_t addrlen)
  { return -1; }
  // Unlike the syscall, addrlen is only an out-argument, since
  // src_addr will be big enough for any sockaddr.  src_addr may be
  // null.
  virtual ssize_t recvfrom(userptr<void> buf, size_t len, int flags,
                           struct ward_sockaddr_storage *src_addr,
                           size_t *addrlen)
  { return -1; }

  virtual sref<vnode> get_vnode() { return sref<vnode>(); }

  virtual void inc() = 0;
  virtual void dec() = 0;

protected:
  file() {}
};

struct file_inode : public refcache::referenced, public file {
public:
  file_inode(sref<vnode> i, bool r, bool w, bool a)
    : ip(i), readable(r), writable(w), append(a), off(0) {}
  PUBLIC_NEW_DELETE_OPS(file_inode);

  void inc() override { refcache::referenced::inc(); }
  void dec() override { refcache::referenced::dec(); }

  const sref<vnode> ip;
  const bool readable;
  const bool writable;
  const bool append;
  u32 off;
  std::unique_ptr<strbuf<FILENAME_MAX>> last_dirent;
  sleeplock off_lock;

  int stat(struct kernel_stat*, enum stat_flags) override;
  ssize_t read(char *addr, size_t n) override;
  ssize_t write(const userptr<void> data, size_t n) override;
  ssize_t pread(char* addr, size_t n, off_t off) override;
  ssize_t pwrite(const userptr<void> data, size_t n, off_t offset) override;
  ssize_t getdents(linux_dirent* out_dirents, size_t bytes) override;
  void onzero() override
  {
    delete this;
  }

  sref<vnode> get_vnode() override { return ip; }
};

struct file_pipe_reader : public refcache::referenced, public file {
public:
  file_pipe_reader(struct pipe* p) : pipe(p) {}
  PUBLIC_NEW_DELETE_OPS(file_pipe_reader);

  void on_ftable_insert(filetable* v) override;
  void on_ftable_remove(filetable* v) override;

  void inc() override { refcache::referenced::inc(); }
  void dec() override { refcache::referenced::dec(); }

  int stat(struct kernel_stat*, enum stat_flags) override;
  ssize_t read(char *addr, size_t n) override;
  void onzero() override;

private:
  struct pipe* const pipe;
};

struct file_pipe_writer : public referenced, public file {
public:
  file_pipe_writer(struct pipe* p) : pipe(p) {}
  PUBLIC_NEW_DELETE_OPS(file_pipe_writer);

  void inc() override { referenced::inc(); }
  void dec() override { referenced::dec(); }

  void on_ftable_insert(filetable* v) override;
  void on_ftable_remove(filetable* v) override;

  int stat(struct kernel_stat*, enum stat_flags) override;
  ssize_t write(const userptr<void> data, size_t n) override;
  void onzero() override;

private:
  struct pipe* const pipe;
};

// in-core file system types
struct inode : public referenced, public rcu_freed
{
  void  init();
  void  link();
  void  unlink();
  short nlink();

  inode& operator=(const inode&) = delete;
  inode(const inode& x) = delete;

  void do_gc() override { delete this; }

  // const for lifetime of object:
  const u32 dev;
  const u32 inum;

  // const unless inode is reused:
  u32 gen;
  std::atomic<short> type;
  short major;
  short minor;

  // locks for the rest of the inode
  seqcount<u64> seq;
  struct condvar cv;
  struct spinlock lock;
  char lockname[16];

  // initially null, set once:
  std::atomic<dirns*> dir;
  std::atomic<bool> valid;

  // protected by seq/lock:
  std::atomic<bool> busy;
  std::atomic<int> readbusy;

  u32 size;
  std::atomic<u32> addrs[NDIRECT+2];
  std::atomic<volatile u32*> iaddrs;
  short nlink_;

  // ??? what's the concurrency control plan?
  struct localsock *localsock;
  char socketpath[WARD_PATH_MAX];

private:
  inode(u32 dev, u32 inum);
  ~inode();
  PUBLIC_NEW_DELETE_OPS(inode)

  static sref<inode> alloc(u32 dev, u32 inum);
  friend void initinode();
  friend sref<inode> iget(u32, u32);

protected:
  void onzero() override;
};


// device implementations

struct devsw {
  int (*read)(char*, u32);
  int (*pread)(char*, u32, u32);
  int (*write)(const char*, u32);
  int (*pwrite)(const char*, u32, u32);
  void (*stat)(struct kernel_stat*);
};

extern struct devsw devsw[];
