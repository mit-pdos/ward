#include "types.h"
#include "mmu.h"
#include "kernel.hh"
#include "spinlock.hh"
#include "condvar.hh"
#include "proc.hh"
#include "fs.h"
#include "file.hh"
#include "cpu.hh"
#include "net.hh"
#include "kmtrace.hh"
#include "dirns.hh"
#include <uk/fcntl.h>
#include <uk/stat.h>
#include "kstats.hh"
#include <vector>
#include "kstream.hh"
#include <uk/spawn.h>
#include <uk/fs.h>
#include "filetable.hh"
#include "errno.h"
#include "fcntl.h"

sref<file>
getfile(int fd)
{
  return myproc()->ftable->getfile(fd);
}

//SYSCALL
long
sys_dup(int ofd)
{
  return myproc()->ftable->dup(ofd);
}

//SYSCALL
long
sys_dup2(int ofd, int nfd)
{
  return myproc()->ftable->dup2(ofd, nfd);
}

static off_t
compute_offset(file_inode *fi, off_t *fioffp, off_t offset, int whence)
{
  switch (whence) {
  case SEEK_SET:
    return offset;

  case SEEK_CUR: {
    off_t fioff = fi->off;
    if (fioffp) *fioffp = fioff;
    return fioff + offset;
  }

  case SEEK_END:
    if (offset < 0 && !fi->ip->is_offset_in_file(-offset - 1))
      return -1;

    return offset + fi->ip->file_size();
  }
  return -1;
}

//SYSCALL
off_t
sys_lseek(int fd, off_t offset, int whence)
{
  sref<file> f = getfile(fd);
  if (!f)
    return -1;

  file* ff = f.get();
  if (&typeid(*ff) != &typeid(file_inode))
    return -1;

  file_inode* fi = static_cast<file_inode*>(ff);
  if (!fi->ip->is_regular_file())
    return -1;                  // ESPIPE

  // Pre-validate offset and whence.  Be careful to only read fi->off
  // once, regardless of what code path we take.
  off_t fioff = -1;
  off_t orig_new_off = compute_offset(fi, &fioff, offset, whence);
  if (orig_new_off < 0)
    return -1;
  if (fioff == -1)
    fioff = fi->off;
  if (orig_new_off == fioff)
    // No change; don't acquire the lock
    return orig_new_off;

  auto l = fi->off_lock.guard();
  off_t new_offset = compute_offset(fi, nullptr, offset, whence);
  if (new_offset < 0)
    return -1;
  fi->off = new_offset;

  return new_offset;
}

//SYSCALL
long
sys_close(int fd)
{
  STRACE_PARAMS("0x%x", fd);

  ensure_secrets();
  return myproc()->ftable->close(fd) ? 0 : -1;
}

//SYSCALL
long
sys_sync(void)
{
  // Not implemented
  return -ENOSYS;
}

//SYSCALL
long
sys_fsync(int fd)
{
  // Not implemented
  return -ENOSYS;
}

ssize_t
impl_read(sref<file>&& f, userptr<void> p, size_t total_bytes)
{
  char b[PGSIZE];
  ssize_t bytes = 0;
  while (bytes < total_bytes) {
    size_t n = total_bytes - bytes;
    if (n > PGSIZE)
      n = PGSIZE;

    ssize_t ret = f->read(b, n);
    if (ret <= 0){
      return bytes ? bytes : ret;
    }
    if (!(p+bytes).store_bytes(b, ret)) {
      return bytes ? bytes : -1;
    }

    bytes += ret;
  }
  return bytes;
}

//SYSCALL
ssize_t
sys_read(int fd, userptr<void> p, size_t total_bytes)
{
  sref<file> f = getfile(fd);
  if (!f)
    return -EBADF;

  if(f->get_vnode()) {
    if(f->get_vnode()->is_directory())
      return -EISDIR;
  } else {
    ensure_secrets();
  }

  if(total_bytes >= 1024 * 1024)
    ensure_secrets();

  return impl_read(std::move(f), p, total_bytes);
}

//SYSCALL
ssize_t
sys_pread(int fd, void *ubuf, size_t count, off_t offset)
{
  sref<file> f = getfile(fd);
  if (!f)
    return -1;

  if (count > 4*1024*1024)
    count = 4*1024*1024;

  char* b = (char*) kmalloc(count, "preadbuf");
  auto cleanup = scoped_cleanup([&](){kmfree(b, count);});
  ssize_t r = f->pread(b, count, offset);
  if (r > 0)
    putmem(ubuf, b, r);
  return r;
}

ssize_t
impl_write(sref<file>&& f, const userptr<void> p, size_t total_bytes)
{
  char b[PGSIZE];
  ssize_t bytes = 0;
  while (bytes < total_bytes) {
    size_t n = total_bytes - bytes;
    if (n > PGSIZE)
      n = PGSIZE;

    if (!(p + bytes).load_bytes(b, n))
      return bytes ? bytes : -1;

    ssize_t ret = f->write(b, n);
    if (ret <= 0)
      return bytes ? bytes : ret;

    bytes += ret;
  }
  return bytes;
}

//SYSCALL
ssize_t
sys_write(int fd, const userptr<void> p, size_t total_bytes)
{
  kstats::timer timer_fill(&kstats::write_cycles);
  kstats::inc(&kstats::write_count);

  sref<file> f = getfile(fd);
  if (!f)
    return -1;

  if(total_bytes >= 1024 * 1024 /*|| !f->get_vnode()*/)
    ensure_secrets();

  return impl_write(std::move(f), p, total_bytes);
}

//SYSCALL
ssize_t
sys_pwrite(int fd, const void *ubuf, size_t count, off_t offset)
{
  sref<file> f = getfile(fd);
  if (!f)
    return -1;

  if (count > 4*1024*1024)
    count = 4*1024*1024;

  char* b = (char*)kmalloc(count, "pwritebuf");
  auto cleanup = scoped_cleanup([&](){kmfree(b, count);});
  fetchmem(b, ubuf, count);
  return f->pwrite(b, count, offset);
}

//SYSCALL
ssize_t
sys_writev(int fd, const void* iov, int count) {
  sref<file> f = getfile(fd);
  if (!f)
    return -1;
  char *b = kalloc("writebuf");
  if (!b)
    return -1;
  auto cleanup = scoped_cleanup([b](){kfree(b);});

  kernel_iovec v;
  for(int i = 0; i < count; i++) {
    ((userptr<kernel_iovec>)((kernel_iovec*)iov) + i).load(&v);
    if (v.len == 0)
      continue;
    if (v.len > PGSIZE)
      v.len = PGSIZE;
    fetchmem(b, v.base, v.len);
    return f->write(b, v.len);

  }
  return 0;
}

//SYSCALL
ssize_t
sys_readv(int fd, const void* iov, int count) {
  sref<file> f = getfile(fd);
  if (!f)
    return -1;
  char *b = kalloc("readbuf");
  if (!b)
    return -1;
  auto cleanup = scoped_cleanup([b](){kfree(b);});

  kernel_iovec v;
  for(int i = 0; i < count; i++) {
    ((userptr<kernel_iovec>)((kernel_iovec*)iov) + i).load(&v);
    if (v.len == 0)
      continue;
    if (v.len > PGSIZE)
      v.len = PGSIZE;

    ssize_t ret = f->read(b, v.len);
    if (ret <= 0)
      return ret;

    if (!userptr<void>(v.base).store_bytes(b, ret))
      return -EIO;
    return ret;

  }
  return 0;
}


//SYSCALL
ssize_t
sys_getdents64(int fd, const userptr<void> p, size_t total_bytes) {
  sref<file> f = getfile(fd);
  if (!f)
    return -EBADF;

  STRACE_PARAMS("0x%x, %p, 0x%lx", fd, p, total_bytes);

  file* dff = f.get();
  if (&typeid(*dff) != &typeid(file_inode))
    return -ENOTDIR;

  file_inode* dfi = static_cast<file_inode*>(dff);
  if (!dfi->ip->is_directory())
    return -ENOTDIR;

  char b[PGSIZE];
  ssize_t bytes = 0;
  while (bytes < total_bytes) {
    size_t n = total_bytes - bytes;
    if (n > PGSIZE)
      n = PGSIZE;

    ssize_t ret = dfi->getdents((linux_dirent*)b, n);
    if (ret <= 0)
      return bytes ? bytes : ret;
    if (!(p+bytes).store_bytes(b, ret))
      return bytes ? bytes : -EINVAL;

    bytes += ret;
  }
  return bytes;
}

//SYSCALL
long
sys_fstat(int fd, userptr<struct kernel_stat> st)
{
  STRACE_PARAMS("0x%x, %p", fd, st.unsafe_get());

  struct kernel_stat st_buf;
  sref<file> f = getfile(fd);
  if (!f)
    return -1;
  if (f->stat(&st_buf, STAT_NO_FLAGS) < 0)
    return -1;
  if (!st.store(&st_buf))
    return -1;
  return 0;
}

//SYSCALL
long
sys_stat(userptr_str path, userptr<struct kernel_stat> st)
{
  char path_copy[WARD_PATH_MAX];
  if (!path.load(path_copy, sizeof path_copy))
    return -EINVAL;

  STRACE_PARAMS("\"%s\", %p", path_copy, st.unsafe_get());

  sref<vnode> m = vfs_root()->resolve(myproc()->cwd, path_copy);
  if(!m)
    return -ENOENT;

  struct kernel_stat st_buf;
  m->stat(&st_buf, STAT_NO_FLAGS);
  if (!st.store(&st_buf)) {
    return -EINVAL;
  }
  return 0;
}

//SYSCALL
long
sys_lstat(userptr_str path, userptr<struct kernel_stat> st)
{
  // We don't support symlinks
  return sys_stat(path, st);
}

//SYSCALL
long
sys_access(userptr_str path, int mode)
{
  char path_copy[WARD_PATH_MAX];
  if (!path.load(path_copy, sizeof path_copy))
    return -1;

  STRACE_PARAMS("\"%s\", 0%o", path_copy, mode);

  sref<vnode> m = vfs_root()->resolve(myproc()->cwd, path_copy);
  if(!m)
    return -2;

  return 0;
}


// Create the path new as a link to the same inode as old.
//SYSCALL
long
sys_link(userptr_str old_path, userptr_str new_path)
{
  char old[WARD_PATH_MAX], newn[WARD_PATH_MAX];
  if (!old_path.load(old, sizeof old) || !new_path.load(newn, sizeof newn))
    return -1;

  return vfs_root()->hardlink(myproc()->cwd, old, newn);
}

//SYSCALL
long
sys_rename(userptr_str old_path, userptr_str new_path)
{
  char old[WARD_PATH_MAX], newn[WARD_PATH_MAX];
  if (!old_path.load(old, sizeof old) || !new_path.load(newn, sizeof newn))
    return -1;

  return vfs_root()->rename(myproc()->cwd, old, newn);
}

//SYSCALL
long
sys_unlink(userptr_str path)
{
  char path_copy[WARD_PATH_MAX];
  if (!path.load(path_copy, sizeof path_copy))
    return -1;

  return vfs_root()->remove(myproc()->cwd, path_copy);
}

//SYSCALL
long
sys_openat(int dirfd, userptr_str path, int omode, ...)
{
  ensure_secrets();
  sref<vnode> cwd;
  if (dirfd == AT_FDCWD) {
    cwd = myproc()->cwd;
  } else {
    sref<file> fdir = getfile(dirfd);
    if (!fdir)
      return -EBADF;
    file* ff = fdir.get();
    if (&typeid(*ff) != &typeid(file_inode))
      return -1;
    file_inode* fdiri = static_cast<file_inode*>(ff);
    cwd = fdiri->ip;
  }

  char path_copy[WARD_PATH_MAX];
  if (!path.load(path_copy, sizeof(path_copy)))
    return -EINVAL;

  STRACE_PARAMS("0x%x, \"%s\", 0x%x", dirfd, path_copy, omode);

  sref<vnode> m = vfs_root()->resolve(cwd, path_copy);

  if (!m && omode & O_CREAT)
    m = vfs_root()->create_file(cwd, path_copy, omode & O_EXCL);

  if (!m)
    return -ENOENT;

  int rwmode = omode & (O_RDONLY|O_WRONLY|O_RDWR);
  if (m->is_directory() && (rwmode != O_RDONLY))
    return -EISDIR;

  if (m->is_regular_file() && (omode & O_TRUNC))
    if (m->truncate() < 0)
      return -1;

  sref<file> f = make_sref<file_inode>(
    m, !(rwmode == O_WRONLY), !(rwmode == O_RDONLY), !!(omode & O_APPEND));
  if (!f)
    return -1;
  return myproc()->ftable->allocfd(std::move(f), 0, omode & O_CLOEXEC);
}

//SYSCALL
long
sys_open(userptr_str path, int omode)
{
  return sys_openat(AT_FDCWD, path, omode);
}

//SYSCALL
long
sys_mkdirat(int dirfd, userptr_str path, mode_t mode)
{
  sref<vnode> cwd;
  if (dirfd == AT_FDCWD) {
    cwd = myproc()->cwd;
  } else {
    sref<file> fdir = getfile(dirfd);
    if (!fdir)
      return -1;
    file* ff = fdir.get();
    if (&typeid(*ff) != &typeid(file_inode))
      return -1;
    file_inode* fdiri = static_cast<file_inode*>(ff);
    cwd = fdiri->ip;
  }

  char path_copy[WARD_PATH_MAX];
  if (!path.load(path_copy, sizeof(path_copy)))
    return -1;

  if (!vfs_root()->create_dir(cwd, path_copy))
    return -1;

  return 0;
}

//SYSCALL
long
sys_mkdir(userptr_str path, mode_t mode)
{
  char path_copy[WARD_PATH_MAX];
  if (!path.load(path_copy, sizeof(path_copy)))
    return -1;

  if (!vfs_root()->create_dir(myproc()->cwd, path_copy))
    return -1;

  return 0;
}

//SYSCALL
long
sys_mknod(userptr_str path, int major, int minor)
{
  char path_copy[WARD_PATH_MAX];
  if (!path.load(path_copy, sizeof(path_copy)))
    return -1;

  if (!vfs_root()->create_device(myproc()->cwd, path_copy, major, minor))
    return -1;

  return 0;
}

//SYSCALL
long
sys_utime(userptr_str path, userptr<time_t> times)
{
  char path_copy[WARD_PATH_MAX];
  if (!path.load(path_copy, sizeof(path_copy)))
    return -EINVAL;

  STRACE_PARAMS("\"%s\", %p", path_copy, times);

  extern time_t sys_time(userptr<time_t> tloc);

  time_t times_copy[2];
  if (!times)
    times_copy[0] = times_copy[1] = sys_time(nullptr);
  else if (!times.load(times_copy, 2))
    return -EFAULT;

  sref<vnode> m = vfs_root()->resolve(myproc()->cwd, path_copy);
  if(!m)
    return -ENOENT;

  if(!m->set_mtime(times_copy[1] * 1000000000ull))
    return -EPERM;

  return 0;
}

//SYSCALL
long
sys_chdir(userptr_str path)
{
  char path_copy[WARD_PATH_MAX];
  if (!path.load(path_copy, sizeof(path_copy)))
    return -1;

  sref<vnode> m = vfs_root()->resolve(myproc()->cwd, path_copy);
  if (!m || !m->is_directory())
    return -1;

  myproc()->cwd = m;
  return 0;
}

//SYSCALL
long
sys_getcwd(userptr<void> out_path, size_t out_len)
{
  size_t path_len = 1;
  char path[WARD_PATH_MAX] = { 0 };

  sref<vnode> node = myproc()->cwd;
  while(1) {
    strbuf<FILENAME_MAX> name;
    sref<vnode> parent = vfs_root()->resolve(node, "..");
    if (!parent || parent->is_same(node))
      break;

    if(!parent->next_dirent(nullptr, &name))
      return -EINVAL;

    while(!vfs_root()->resolve(parent, name.ptr())->is_same(node)) {
      if(!parent->next_dirent(name.ptr(), &name))
        return -EINVAL;
    }

    node = parent;

    size_t name_len = strlen(name.ptr());
    assert(name_len + path_len + 1 < WARD_PATH_MAX);

    memmove(path+name_len+1, path, path_len+1);
    memmove(path+1, name.ptr(), name_len);
    path[0] = '/';
    path_len += name_len + 1;
  }

  if(path_len == 1) {
    path_len = 2;
    path[0] = '/';
    path[1] = '\0';
  }

  if(out_len < path_len)
    return -EACCES;

  if(!out_path.store_bytes(path, path_len))
    return -EINVAL;

  return path_len - 1;
}


// Load NULL-terminated char** list, such as the argv argument to
// exec.
static int
load_str_list(userptr<userptr_str> list, size_t listmax, size_t strmax,
              std::vector<std::unique_ptr<char[]>, kmalloc_allocator<std::unique_ptr<char[]>>> *out)
{
  std::vector<std::unique_ptr<char[]>, kmalloc_allocator<std::unique_ptr<char[]>>> argv;
  for (int i = 0; ; ++i) {
    if (i == listmax)
      return -1;
    userptr_str uarg;
    if (!(list + (ptrdiff_t)i).load(&uarg))
      return -1;
    if (!uarg)
      break;
    auto arg = uarg.load_alloc(strmax);
    if (!arg)
      return -1;
    argv.push_back(std::move(arg));
  }
  *out = std::move(argv);
  return 0;
}

int
doexec(userptr_str upath, userptr<userptr_str> uargv)
{
  std::unique_ptr<char[]> path;
  if (!(path = upath.load_alloc(FILENAME_MAX+1)))
    return -1;

  std::vector<std::unique_ptr<char[]>, kmalloc_allocator<std::unique_ptr<char[]>>> xargv;
  if (load_str_list(uargv, MAXARG, MAXARGLEN, &xargv) < 0)
    return -1;

  std::vector<char*, kmalloc_allocator<char*>> argv;
  for (auto &p : xargv)
    argv.push_back(p.get());
  argv.push_back(nullptr);

  return exec(path.get(), argv.data());
}

//SYSCALL {"uargs":["const char *upath", "char * const uargv[]"]}
long
sys_execv(userptr_str upath, userptr<userptr_str> uargv)
{
  myproc()->data_cpuid = myid();
  return doexec(upath, uargv);
}

//SYSCALL {"uargs":["const char *upath", "char * const uargv[]"]}
long
sys_execve(userptr_str upath, userptr<userptr_str> uargv, char *const envp[])
{
  // TODO: actually pass env.
  myproc()->data_cpuid = myid();
  return doexec(upath, uargv);
}

//SYSCALL
long
sys_pipe2(userptr<int> fd, int flags)
{
  sref<file> rf, wf;
  if (pipealloc(&rf, &wf, flags) < 0)
    return -1;

  int fd_buf[2] = { -1, -1 };
  myproc()->ftable->alloc_pair(std::move(rf), std::move(wf), &fd_buf[0], &fd_buf[1], flags & O_CLOEXEC);

  if (fd_buf[0] >= 0 && fd_buf[1] >= 0 && fd.store(fd_buf, 2))
    return 0;

  if (fd_buf[0] >= 0)
    myproc()->ftable->close(fd_buf[0]);
  if (fd_buf[1] >= 0)
    myproc()->ftable->close(fd_buf[1]);
  return -1;
}

//SYSCALL
long
sys_pipe(userptr<int> fd)
{
  return sys_pipe2(fd, 0);
}

//SYSCALL
long
sys_readdir(int dirfd, const userptr<char> prevptr, userptr<char> nameptr)
{
  sref<file> df = getfile(dirfd);
  if (!df)
    return -1;

  file* dff = df.get();
  if (&typeid(*dff) != &typeid(file_inode))
    return -1;

  file_inode* dfi = static_cast<file_inode*>(dff);
  if (!dfi->ip->is_directory())
    return -1;

  strbuf<FILENAME_MAX> prev;
  if (prevptr && !prevptr.load(prev.buf_, FILENAME_MAX))
    return -1;
  prev.buf_[FILENAME_MAX] = '\0';

  strbuf<FILENAME_MAX> name;
  if (!dfi->ip->next_dirent(prevptr ? prev.ptr() : nullptr, &name))
    return 0;

  if (!nameptr.store(name.buf_, FILENAME_MAX))
    return -1;

  return 1;
}

//SYSCALL
long
sys_readlink(userptr_str path, userptr<void> buf, size_t size)
{
  return -EINVAL;
}

//SYSCALL
long
sys_chmod(userptr_str path, mode_t mode)
{
  return 0;
}

//SYSCALL
long
sys_fcntl(int fd, int cmd, u64 arg)
{
  switch(cmd) {
  case F_GETFL:
    return O_RDWR;
  case F_DUPFD:
  {
    auto f = getfile(fd);
    if (!f)
      return -EBADFD;
    return myproc()->ftable->allocfd(std::move(f), arg, false);
  }
  case F_DUPFD_CLOEXEC:
  {
    auto f = getfile(fd);
    if (!f)
      return -EBADFD;
    return myproc()->ftable->allocfd(std::move(f), arg, true);
  }
  case F_SETFD:
  {
    if (!myproc()->ftable->set_cloexec(fd, arg & O_CLOEXEC))
      return -EBADFD;
    return 0;
  }

  default:
    return -EINVAL;
  };
}

//SYSCALL
long
sys_ioctl(int fd, int request, void* argp)
{
  struct termios {
    u32 iflag;		/* input mode flags */
    u32 oflag;		/* output mode flags */
    u32 cflag;		/* control mode flags */
    u32 lflag;		/* local mode flags */
    u8 line;			/* line discipline */
    u8 cc[23];		/* control characters */
  };

  if (request == 0x5413) { // TIOCGWINSZ
    struct winsize {
      u16 rows;
      u16 cols;
      u16 xpixel;
      u16 ypixel;
    };

    userptr<winsize> window((winsize*)argp);
    auto output = winsize { .rows = 24, .cols = 80, .xpixel = 24*8, .ypixel = 80*16 };
    window.store(&output);
    return 0;
  } else if (request == 0x5401) { // TCGETS
    termios t{0};
    return userptr<termios>((termios*)argp).store(&t) ? 0 : -1;
  } else if (request == 0x5402) { // TCSETS
    return 0;
  }

  return -EINVAL;
}

//SYSCALL
long
sys_poll(void* fds, long nfds, const struct timespec* tmo_p, void* sigmask)
{
  // TODO: Actually implement this syscall, instead of just returning
  // immediately.

  if (nfds != 1)
    return -ENOSYS;

  struct pollfd {
    int   fd;         /* file descriptor */
    short events;     /* requested events */
    short revents;    /* returned events */
  } p;
  userptr<pollfd> ptr((pollfd*)fds);
  ptr.load(&p);

  p.revents = (0x0001 | 0x0004) & p.events; // POLLIN | POLLOUT
  ptr.store(&p);
  return 1;
}
