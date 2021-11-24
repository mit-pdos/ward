// Fake IDE disk; stores blocks in memory.
// Useful for running kernel without scratch disk.

#include "types.h"
#include <cstring>
#include "kernel.hh"
#include "cpputil.hh"
#include "disk.hh"
#include "vmalloc.hh"
#include "../third_party/zlib/zlib.h"

extern u8 _fs_img_start[];
extern u64 _fs_img_size;

class memdisk : public disk
{
public:
  memdisk(u8 *disk, size_t length, u32 diskindex);

  void readv(kiovec *iov, int iov_cnt, u64 off) override;
  void writev(kiovec *iov, int iov_cnt, u64 off) override;
  void flush() override;

  NEW_DELETE_OPS(memdisk);

private:
  u8 *disk;
  size_t length;
};

static void* zlib_alloc(void* opaque, unsigned int items, unsigned int size) {
  char* ptr = (char*)kmalloc(items * size + 16, "zlib");
  *(u64*)ptr = items * size + 16;
  return ptr + 16;
}
static void zlib_free(void* opaque, void* address) {
  u64 size = *(u64*)((char*)address - 16);
  kmfree((char*)address - 16, size);
}

void
initmemide(void)
{
  if (_fs_img_size > 0) {
    size_t len = 64 << 20;
    void* buf = kalloc("memide", len);
    if (!buf)
      buf = vmalloc_raw(len, PGSIZE, "memide");

    z_stream stream;
    memset(&stream, 0, sizeof(stream));
    stream.zalloc = zlib_alloc;
    stream.zfree = zlib_free;
    stream.avail_in = _fs_img_size;
    stream.next_in = _fs_img_start;
    stream.avail_out = len;
    stream.next_out = (unsigned char*)buf;
    if(inflateInit2(&stream, 16+MAX_WBITS) || inflate(&stream, Z_FINISH) != Z_STREAM_END) {
      cprintf("Failed to decompress in-memory disk\n");
      kfree(buf);
    } else {
      disk_register(new memdisk((u8*)buf, len, 0));
    }
    inflateEnd(&stream);
  }
}

memdisk::memdisk(u8 *disk, size_t length, u32 diskindex)
  : disk(disk), length(length)
{
  dk_nbytes = length;
  snprintf(dk_model, sizeof(dk_model), "SV6 MEMDISK");
  snprintf(dk_serial, sizeof(dk_serial), "%16p", disk);
  snprintf(dk_firmware, sizeof(dk_firmware), "n/a");
  snprintf(dk_busloc, sizeof(dk_busloc), "memide.%d", diskindex);
}

void
memdisk::readv(kiovec *iov, int iov_cnt, u64 offset)
{
  for (int i = 0; i < iov_cnt; i++) {
    kiovec v = iov[i];

    if (offset + v.iov_len > this->length || offset + v.iov_len < offset)
      panic("readv: sector out of range");

    u8 *p = this->disk + offset;
    memmove(iov->iov_base, p, iov->iov_len);

    offset += v.iov_len;
  }
}

void
memdisk::writev(kiovec *iov, int iov_cnt, u64 offset)
{
  for (int i = 0; i < iov_cnt; i++) {
    kiovec v = iov[i];

    if (offset + v.iov_len > this->length || offset + v.iov_len < offset)
      panic("writev: sector out of range");

    u8 *p = this->disk + offset;
    memmove(p, iov->iov_base, iov->iov_len);

    offset += v.iov_len;
  }
}

void
memdisk::flush()
{
  // nothing needed
}
