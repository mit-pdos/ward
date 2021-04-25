#include "types.h"
#include "kernel.hh"
#include "spinlock.hh"
#include "condvar.hh"
#include "fs.h"
#include "file.hh"
#include "filetable.hh"
#include <uk/stat.h>
#include "net.hh"
#include <errno.h>

struct devsw __mpalign__ devsw[NDEV];


int
file_inode::stat(struct kernel_stat *st, enum stat_flags flags)
{
  memset(st, 0, sizeof(struct kernel_stat));
  ip->stat(st, flags);
  u16 major, minor;
  if (ip->as_device(&major, &minor) && major < NDEV && devsw[major].stat)
    devsw[major].stat(st);
  return 0;
}

ssize_t
file_inode::read(userptr<void> addr, size_t total_bytes)
{
  if (!readable)
    return -1;

  lock_guard<sleeplock> l;
  ssize_t r = 0;
  u16 major, minor;
  if (ip->as_device(&major, &minor)) {
    if (major >= NDEV)
      return -1;

    char b[PGSIZE];
    if (total_bytes > PGSIZE)
      total_bytes = PGSIZE;

    if (devsw[major].read) {
      r = devsw[major].read(b, total_bytes);
      if (r > 0 && !addr.store_bytes(b, r))
        r = -1;
      return r;
    } else if (devsw[major].pread) {
      l = off_lock.guard();
      r = devsw[major].pread(b, off, total_bytes);
      if (r > 0 && !addr.store_bytes(b, r))
        r = -1;
    } else {
      return -1;
    }
  } else if (!ip->is_regular_file()) {
    return -1;
  } else {
    if (!ip->is_offset_in_file(off))
      return 0;


    l = off_lock.guard();

    char b[PGSIZE];
    ssize_t bytes = 0;
    while (bytes < total_bytes) {
      size_t n = total_bytes - bytes;
      if (n > PGSIZE)
        n = PGSIZE;

      r = ip->read_at(b, off + bytes, n);
      if (r <= 0){
        break;
      }
      if (!(addr+bytes).store_bytes(b, r)) {
        r = -1;
        break;
      }

      bytes += r;
      if (r < PGSIZE)
        break;
    }

    if (bytes > 0)
      r = bytes;
  }
  if (r > 0)
    off += r;
  return r;
}

ssize_t
file_inode::write(const userptr<void> data, size_t n) {
  if (!writable)
    return -1;

  lock_guard<sleeplock> l;
  ssize_t r;
  u16 major, minor;
  if (ip->as_device(&major, &minor)) {
    char buf[PGSIZE];
    if (n > PGSIZE)
      n = PGSIZE;
    if (!data.load_bytes(buf, n))
      return -1;

    if (major >= NDEV)
      return -1;
    if (devsw[major].write) {
      return devsw[major].write(buf, n);
    } else if (devsw[major].pwrite) {
      l = off_lock.guard();
      r = devsw[major].pwrite(buf, off, n);
    } else {
      return -1;
    }
  } else if (!ip->is_regular_file()) {
    return -1;
  } else {
    l = off_lock.guard();
    r = ip->write_at(data, off, n, append);
  }

  if (r > 0)
    off += r;
  return r;
}

ssize_t
file_inode::pread(userptr<void> addr, size_t n, off_t off)
{
  if (!readable)
    return -1;

  if (n > 4*1024*1024)
    n = 4*1024*1024;

  char* b = (char*) kmalloc(n, "preadbuf");
  auto cleanup = scoped_cleanup([&](){kmfree(b, n);});

  ssize_t ret;
  u16 major, minor;
  if (ip->as_device(&major, &minor)) {
    if (major >= NDEV || !devsw[major].pread)
      return -1;
    ret = devsw[major].pread(b, off, n);
  } else {
    ret =  ip->read_at(b, off, n);
  }

  if (ret <= 0 || addr.store_bytes(b, n))
    return ret;
  return -1;
}

ssize_t
file_inode::pwrite(const userptr<void> data, size_t n, off_t off)
{
  if (!writable)
    return -1;
  u16 major, minor;
  if (ip->as_device(&major, &minor)) {
    if (major >= NDEV || !devsw[major].pwrite)
      return -1;

    char buf[PGSIZE];
    if (n > PGSIZE)
      n = PGSIZE;
    if (!data.load_bytes(buf, n))
      return -1;

    return devsw[major].pwrite(buf, off, n);
  }
  return ip->write_at(data, off, n, false);
}

ssize_t
file_inode::getdents(linux_dirent* out_dirents, size_t bytes)
{
  if (!readable)
    return -1;

  int i;
  lock_guard<sleeplock> l;
  if (!ip->is_directory()) {
    return -ENOTDIR;
  } else {
    l = off_lock.guard();

    for(i = 0; (i+1) * sizeof(linux_dirent) < bytes; i++) {
      const char* last = last_dirent ? last_dirent->ptr() : nullptr;
      if (!last_dirent) {
        last_dirent = std::unique_ptr<strbuf<FILENAME_MAX>>(new strbuf<FILENAME_MAX>(""));
      }

      if(!ip->next_dirent(last, last_dirent.get())) {
        break;
      }

      off += sizeof(linux_dirent);

      memset(&out_dirents[i], 0, sizeof(linux_dirent));
      out_dirents[i].d_ino = 1; // TODO (must be non-zero since 0=deleted)
      out_dirents[i].d_type = 0; // TODO
      out_dirents[i].d_off = off;
      out_dirents[i].d_reclen = sizeof(linux_dirent);
      strncpy(out_dirents[i].d_name, last_dirent->ptr(), sizeof(linux_dirent::d_name));
    }
  }

  return i * sizeof(linux_dirent);
}

int
file_pipe_reader::stat(struct kernel_stat *st, enum stat_flags flags)
{
  memset(st, 0, sizeof(struct kernel_stat));
  st->st_mode = (T_FIFO << __S_IFMT_SHIFT) | 0600;
  st->st_dev = 0;               // XXX ?
  st->st_ino = (uintptr_t)pipe;
  st->st_nlink = 1;
  st->st_size = 0;
  return 0;
}

ssize_t
file_pipe_reader::read(userptr<void> addr, size_t n)
{
  char b[PGSIZE];
  if (n > PGSIZE)
    n = PGSIZE;

  ssize_t ret = piperead(pipe, b, n);

  if (ret <= 0 || addr.store_bytes(b, ret))
    return ret;
  return -1;
}

void
file_pipe_reader::onzero(void)
{
  pipeclose(pipe, false);
  delete this;
}

void file_pipe_reader::on_ftable_insert(filetable* v) {
  pipemap(pipe, v->get_vmap());
}
void file_pipe_reader::on_ftable_remove(filetable* v) {
  pipeunmap(pipe, v->get_vmap());
}

int
file_pipe_writer::stat(struct kernel_stat *st, enum stat_flags flags)
{
  memset(st, 0, sizeof(struct kernel_stat));
  st->st_mode = (T_FIFO << __S_IFMT_SHIFT) | 0600;
  st->st_dev = 0;               // XXX ?
  st->st_ino = (uintptr_t)pipe;
  st->st_nlink = 1;
  st->st_size = 0;
  return 0;
}

ssize_t
file_pipe_writer::write(const userptr<void> data, size_t n)
{
  char buf[PGSIZE];
  if (n > PGSIZE)
    n = PGSIZE;
  if (!data.load_bytes(buf, n))
    return -1;

  return pipewrite(pipe, buf, n);
}

void
file_pipe_writer::onzero(void)
{
  pipeclose(pipe, true);
  delete this;
}

void file_pipe_writer::on_ftable_insert(filetable* v) {
  pipemap(pipe, v->get_vmap());
}
void file_pipe_writer::on_ftable_remove(filetable* v) {
  pipeunmap(pipe, v->get_vmap());
}
