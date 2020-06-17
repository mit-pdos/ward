#include "types.h"
#include "kernel.hh"
#include "fs.h"
#include "file.hh"
#include "major.h"
#include "kstats.hh"
#include "kstream.hh"
#include "linearhash.hh"

DEFINE_QPERCPU(struct kstats, mykstats, NO_CRITICAL);

static int
kstatsread(char *dst, u32 off, u32 n)
{
  kstats total{};
  if (off >= sizeof total)
    return 0;
  for (size_t i = 0; i < ncpu; ++i)
    total += mykstats[i];
  if (n > sizeof total - off)
    n = sizeof total - off;
  memmove(dst, (char*)&total + off, n);
  return n;
}

static int
qstatsread(char *dst, u32 off, u32 n)
{
  window_stream s(dst, off, n);

  extern linearhash<u64, u64> transparent_wb_rips;
  extern linearhash<u64, u64> intentional_wb_rips;

  s.println("exit_triggers = [");
  for(auto i = transparent_wb_rips.begin(); i != transparent_wb_rips.end(); i++) {
    u64 key, value;
    if(i.get(&key, &value)) {
      s.print("  { backtrace = [\"", shex(KTEXT | (key & 0x1fffff)));
      if ((key >> 21) != 0) {
        s.print("\", \"", shex(KTEXT | ((key>>21) & 0x1fffff)));
      }
      if ((key >> 42) != 0) {
        s.print("\", \"", shex(KTEXT | ((key>>42) & 0x1fffff)));
      }
      s.println("\"], count = ", value, ", intentional = false },");
    }
  }
  for(auto i = intentional_wb_rips.begin(); i != intentional_wb_rips.end(); i++) {
    u64 key, value;
    if(i.get(&key, &value)) {
      s.println("  { backtrace = [\"", shex(key), "\"], count = ", value, ", intentional = true },");
    }
  }

  s.println("]");
  return s.get_used();
}

static int nullread(char* dst, u32 off, u32 n) {
  return 0;
}
static int nullwrite(const char* src, u32 off, u32 n) {
  return n;
}

void
initdev(void)
{
  devsw[MAJ_KSTATS].pread = kstatsread;
  devsw[MAJ_QSTATS].pread = qstatsread;
  devsw[MAJ_NULL].pread = nullread;
  devsw[MAJ_NULL].pwrite = nullwrite;
}
