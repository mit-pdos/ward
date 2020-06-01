#include "types.h"
#include "kernel.hh"
#include "fs.h"
#include "file.hh"
#include "mnode.hh"
#include "linearhash.hh"
#include "mfs.hh"
#include "cmdline.hh"
#include "disk.hh"

static sref<mnode> load_inum(linearhash<u64, sref<mnode>> *inum_to_mnode, u32 dev, u64 inum);

static void
load_dir(linearhash<u64, sref<mnode>> *inum_to_mnode, sref<inode> i, sref<mnode> m)
{
  dirent de;
  for (size_t pos = 0; pos < i->size; pos += sizeof(de)) {
    assert(sizeof(de) == readi(i, (char*) &de, pos, sizeof(de)));
    if (!de.inum)
      continue;

    sref<mnode> mf = load_inum(inum_to_mnode, i->dev, de.inum);
    strbuf<DIRSIZ> name(de.name);
    if (name == ".")
      continue;

    mlinkref ilink(mf);
    ilink.acquire();
    m->as_dir()->insert(name, &ilink);
  }
}

static void
load_file(sref<inode> i, sref<mnode> m)
{
  for (size_t pos = 0; pos < i->size; pos += PGSIZE) {
    char* p = zalloc("load_file");
    assert(p);

    auto pi = sref<page_info>::transfer(new (page_info::of(p)) page_info());

    size_t nbytes = i->size - pos;
    if (nbytes > PGSIZE)
      nbytes = PGSIZE;

    assert(nbytes == readi(i, p, pos, nbytes));
    auto resize = m->as_file()->write_size();
    resize.resize_append(pos + nbytes, pi);
  }
}

static sref<mnode>
mnode_alloc(linearhash<u64, sref<mnode>> *inum_to_mnode, u64 inum, u8 mtype)
{
  auto m = root_fs->alloc(mtype);
  inum_to_mnode->insert(inum, m.mn());
  return m.mn();
}

static sref<mnode>
load_inum(linearhash<u64, sref<mnode>> *inum_to_mnode, u32 dev, u64 inum)
{
  sref<mnode> m;
  if (inum_to_mnode->lookup(inum, &m))
    return m;

  sref<inode> i = iget(dev, inum);
  switch (i->type.load()) {
  case T_DIR:
    m = mnode_alloc(inum_to_mnode, inum, mnode::types::dir);
    load_dir(inum_to_mnode, i, m);
    break;

  case T_FILE:
    m = mnode_alloc(inum_to_mnode, inum, mnode::types::file);
    load_file(i, m);
    break;

  default:
    panic("unhandled inode %ld type %d\n", inum, i->type.load());
  }

  return m;
}

void
mfsload()
{
  root_fs = new mfs();

  auto inum_to_mnode = new linearhash<u64, sref<mnode>>(4099);
  root_inum = load_inum(inum_to_mnode, disk_find_root(), 1)->inum_;
  /* the root inode gets an extra reference because of its own ".." */
  delete inum_to_mnode;
}


static void copy_directory(sref<mnode> dst_dir, sref<vnode> src_dir)
{
  strbuf<FILENAME_MAX> last("..");
  strbuf<FILENAME_MAX> name;
  while(src_dir->next_dirent(last.ptr(), &name)) {
    last = name;

    if(strcmp(name.ptr(), ".") == 0 || strcmp(name.ptr(), "..") == 0)
      continue;

    sref<vnode> f = src_dir->get_fs()->resolve(src_dir, name.ptr());
    sref<mnode> m;

    if(f->is_directory()) {
      cprintf("A:");
      m = root_fs->alloc(mnode::types::dir).mn();
      copy_directory(m, f);

      mlinkref ilink(dst_dir);
      ilink.acquire();
      m->as_dir()->insert("..", &ilink);
    } else if(f->is_regular_file()) {
      m = root_fs->alloc(mnode::types::file).mn();

      for (size_t pos = 0; pos < f->file_size(); pos += PGSIZE) {
        char* p = zalloc("load_file");
        assert(p);

        auto pi = sref<page_info>::transfer(new (page_info::of(p)) page_info());

        size_t nbytes = f->file_size() - pos;
        if (nbytes > PGSIZE)
          nbytes = PGSIZE;

        assert(nbytes == f->read_at(p, pos, nbytes));
        auto resize = m->as_file()->write_size();
        resize.resize_append(pos + nbytes, pi);
      }
    } else {
      panic("unknown file type");
    }

    mlinkref ilink(m);
    ilink.acquire();
    if(!strcmp(name.ptr(), "head"))
      dst_dir->as_dir()->insert("HEAD", &ilink);
    else
      dst_dir->as_dir()->insert(name.ptr(), &ilink);
  }
}

void mfsloadfat(sref<filesystem> fs)
{
  root_fs = new mfs();

  cprintf("A:");
  auto m = root_fs->alloc(mnode::types::dir).mn();
  root_inum = m->inum_;
  copy_directory(m, fs->root());
}
