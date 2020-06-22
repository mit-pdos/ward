#pragma once

#include <atomic>
#include "percpu.hh"
#include "bitset.hh"
#include "bits.hh"

struct pgmap;

struct pgmap_pair {
  pgmap* user;
  pgmap* kernel;
};

class tlb_shootdown
{
public:
  constexpr tlb_shootdown() : cache_(nullptr), start_(~0), end_(0) {}
  void set_cache(page_map_cache* cache) {
    assert(cache_ == nullptr || cache_ == cache);
    cache_ = cache;
  }


  void add_range(uintptr_t start, uintptr_t end) {
    if (start < start_)
      start_ = start;
    if (end_ < end)
      end_ = end;
  }

  void perform() const;

  static void on_ipi();

private:
  void clear_tlb() const;
  page_map_cache* cache_;
  uintptr_t start_, end_;
};

// A page_map_cache controls the hardware cache of
// virtual-to-physical page mappings.
class page_map_cache
{
  pgmap_pair pml4s;
  vmap* const parent_;
  atomic<u64> tlb_generation_;
  mutable bitset<NCPU> active_cores_;

  // Switch to this page_map_cache on this CPU.
  void switch_to() const;

  friend void switchvm(struct vmap*, struct vmap*);
  friend class tlb_shootdown;

  page_map_cache(const page_map_cache&) = delete;
  page_map_cache(page_map_cache&&) = delete;
  page_map_cache &operator=(const page_map_cache&) = delete;
  page_map_cache &operator=(page_map_cache&&) = delete;

public:
  const u64 asid_;

  page_map_cache(vmap* parent);
  ~page_map_cache();

  // Load a mapping into the translation cache from the virtual
  // address va to the specified PTE. This should be called on page
  // faults. In general, the PTE is not guaranteed to persist.
  void insert(uintptr_t va, pme_t pte);

  // Invalidate all mappings from virtual address @c va to
  // <tt>start+len</tt>. This should be called whenever a page
  // mapping's permissions become more strict or the mapped page
  // changes. Any pages that need to be shot-down will have
  // their trackers accumulated in @c sd and cleared.
  void invalidate(uintptr_t start, uintptr_t len, tlb_shootdown *sd);
};
