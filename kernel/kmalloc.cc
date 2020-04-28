//
// Allocate objects smaller than a page.
//

#include "types.h"
#include "mmu.h"
#include "kernel.hh"
#include "spinlock.hh"
#include "kalloc.hh"
#include "mtrace.h"
#include "cpu.hh"
#include "kstream.hh"
#include "log2.hh"
#include "rnd.hh"
#include "amd64.h"
#include "page_info.hh"
#include "heapprof.hh"

#include <type_traits>

// allocate in power-of-two sizes up to 2^KMMAX (PGSIZE)
#define KMMAX 12

struct header {
  struct header *next;
};

template<typename Allocator>
class freelist {
  versioned<vptr48<header*>> buckets[KMMAX+1];
  char name[MAXNAME];
  Allocator allocator;

  int bucket(u64 nbytes) {
    int b = ceil_log2(nbytes);
    if (b < 6)
      b = 6;
    assert((1<<b) >= nbytes);
    assert(b <= KMMAX);
    return b;
  }

  // get more space for the given bucket
  int morecore(int bucket) {
    char *p = allocator.allocate(PGSIZE);
    if(p == 0)
      return -1;

    if (ALLOC_MEMSET)
      memset(p, 3, PGSIZE);

#if RANDOMIZE_KMALLOC
#if CODEX
    u8 r = rnd() % 11;
#else
    u8 r = rdtsc() % 11;
#endif
#else
    u8 r = 0;
#endif

    int sz = 1 << bucket;
    assert(sz >= sizeof(header));
    for(char *q = p + CACHELINE * r; q + sz <= p + PGSIZE; q += sz){
      struct header *h = (struct header *) q;
      for (;;) {
        auto headptr = buckets[bucket].load();
        h->next = headptr.ptr();
        if (buckets[bucket].compare_exchange(headptr, h))
          break;
      }
    }

    return 0;
  }

public:
  void* alloc(size_t nbytes, const char *name) {
    void *p;
    uint64_t mbytes = alloc_debug_info::expand_size(nbytes);

    if (mbytes > PGSIZE / 2) {
      // Full page allocation
      p = allocator.allocate(round_up_to_pow2(mbytes));
    } else {
      // Sub-page allocation
      header* h;

      size_t b = bucket(nbytes);
      for (;;) {
        auto headptr = buckets[b].load();
        h = headptr.ptr();
        if (!h) {
          if (morecore(b) < 0) {
            cprintf("kmalloc(%d) failed\n", 1 << b);
            return 0;
          }
        } else {
          header *nxt = h->next;
          if (buckets[b].compare_exchange(headptr, nxt)) {
            if (h->next != nxt)
              panic("kmalloc: aba race");
            break;
          }
        }
      }

      if (ALLOC_MEMSET) {
        char* chk = (char*)h + sizeof(struct header);
        for (int i = 0; i < (1<<b)-sizeof(struct header); i++) {
          if (chk[i] != 3) {
            console.print(shexdump(chk, 1<<b));
            panic("kmalloc: free memory was overwritten %p+%x", chk, i);
          }
        }
        memset(h, 4, (1<<b));
      }
      p = h;
    }

    if (p) {
      // Update debug_info
      alloc_debug_info *adi = alloc_debug_info::of(p, nbytes);
      if (KERNEL_HEAP_PROFILE) {
        auto alloc_rip = __builtin_return_address(0);
        if (heap_profile_update(HEAP_PROFILE_KMALLOC, alloc_rip, nbytes))
          adi->set_alloc_rip(HEAP_PROFILE_KMALLOC, alloc_rip);
        else
          adi->set_alloc_rip(HEAP_PROFILE_KMALLOC, nullptr);
      }
      mtlabel(mtrace_label_heap, (void*) p, nbytes, name, strlen(name));
    }

    return p;
  }

  void free(void *p, u64 nbytes) {
    // Update debug_info
    alloc_debug_info *adi = alloc_debug_info::of(p, nbytes);
    if (KERNEL_HEAP_PROFILE) {
      auto alloc_rip = adi->alloc_rip(HEAP_PROFILE_KMALLOC);
      if (alloc_rip)
        heap_profile_update(HEAP_PROFILE_KMALLOC, alloc_rip, -nbytes);
    }

    if (nbytes > PGSIZE / 2) {
      // Free full page allocation
      allocator.deallocate((char*)p, round_up_to_pow2(nbytes));
    } else {
      header* h = (header*)p;
      int b = bucket(nbytes);

      if (ALLOC_MEMSET)
        memset(p, 3, (1<<b));

      for (;;) {
        auto headptr = buckets[b].load();
        h->next = headptr.ptr();
        if (buckets[b].compare_exchange(headptr, h))
          break;
      }
    }
  }

  friend void kminit();
};

DEFINE_PERCPU(freelist<kalloc_allocator<char>>, freelists);
DEFINE_QPERCPU(freelist<palloc_allocator<char>>, pfreelists);

void
kminit(void)
{
  for (int c = 0; c < ncpu; c++) {
    freelists[c].name[0] = (char) c + '0';
    safestrcpy(freelists[c].name+1, "freelist", MAXNAME-1);
  }
}

void* kmalloc(u64 nbytes, const char *name) {
  void* p = freelists->alloc(nbytes, name);
  if (p) {
    mtlabel(mtrace_label_heap, (void*) h, nbytes, name, strlen(name));
  }
  return p;
}
void kmfree(void *p, u64 nbytes) {
  mtunlabel(mtrace_label_heap, p);
  freelists->free(p, nbytes);
}

char* pmalloc(u64 nbytes, const char *name) {
  return (char*)pfreelists->alloc(nbytes, name);
}
void pmfree(void *p, u64 nbytes) {
  pfreelists->free(p, nbytes);
}

// Expand an allocation size to include its alloc_debug_info
size_t
alloc_debug_info::expand_size(size_t size)
{
  if (std::is_empty<alloc_debug_info>::value)
    return size;

  static_assert(round_up_to_pow2_const(alignof(alloc_debug_info)) ==
                alignof(alloc_debug_info),
                "alignof(alloc_debug_info) isn't a power of two");
  size_t aligned = (size + (alignof(alloc_debug_info) - 1)) &
    ~(alignof(alloc_debug_info) - 1);
  size_t want = aligned + sizeof(alloc_debug_info);
  if (want > PGSIZE / 2) {
    // Store alloc_debug_info in page_info.  Round the size up
    // enough to make sure it allocates a whole page (we can't just
    // return size, because that may be <= PGSIZE / 2)
    if (size <= PGSIZE / 2)
      // We can't just return size because that would cause a
      // sub-page allocation, so make it just big enough to force a
      // full page allocation.
      return PGSIZE / 2;
    return size;
  }
  // Sub-page allocations store the alloc_debug_info at the end
  return want;
}

// Given an allocated pointer and the allocation's original size (not
// its expanded size), return the alloc_debug_info for the allocation.
alloc_debug_info *
alloc_debug_info::of(void *p, size_t size)
{
  if (std::is_empty<alloc_debug_info>::value)
    return nullptr;

  size_t aligned = (size + (alignof(alloc_debug_info) - 1)) &
    ~(alignof(alloc_debug_info) - 1);
  size_t want = aligned + sizeof(alloc_debug_info);

  if (want > PGSIZE / 2)
    return page_info::of(p);
  return (alloc_debug_info*)((char*)p + aligned);
}
