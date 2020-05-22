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

class core_tracking_shootdown
{
public:
  constexpr core_tracking_shootdown() : cache_(nullptr), start_(~0), end_(0) {}
  void set_cache(mmu_shared_page_table::page_map_cache* cache) {
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
  mmu_shared_page_table::page_map_cache* cache_;
  uintptr_t start_, end_;
};

// An MMU implementation based on shared page tables, where each vmap
// is supported by a single hardware page table.  In this case,
// page_map_cache maintains a hardware page table, which acts like a
// large and perfect cache in front of the hardware TLB.
namespace mmu_shared_page_table {
  // A page tracker maintains the per-page metadata necessary to compute
  // TLB shootdowns.  This may be an empty struct, so the caller may
  // want to use "empty base optimization" to prevent this from
  // consuming space.
  typedef struct { } page_tracker;

  // A shootdown gathers invalidations that need to be performed on
  // other cores.
  typedef TLB_SCHEME shootdown;

  // A page_map_cache controls the hardware cache of
  // virtual-to-physical page mappings.
  class page_map_cache
  {
    pgmap_pair pml4s;
    vmap* parent_;

    const u64 asid_;
    atomic<u64> tlb_generation_;
    mutable bitset<NCPU> active_cores_;

    void __insert(uintptr_t va, pme_t pte);
    void __invalidate(uintptr_t start, uintptr_t len, shootdown *sd);

    friend class ::core_tracking_shootdown;

  public:
    page_map_cache(vmap* parent);
    ~page_map_cache();

    void init();

    page_map_cache(const page_map_cache&) = delete;
    page_map_cache(page_map_cache&&) = delete;
    page_map_cache &operator=(const page_map_cache&) = delete;
    page_map_cache &operator=(page_map_cache&&) = delete;

    // Load a mapping into the translation cache from the virtual
    // address va to the specified PTE. This should be called on page
    // faults. In general, the PTE is not guaranteed to persist.
    void insert(uintptr_t va, pme_t pte);

    // Invalidate all mappings from virtual address @c va to
    // <tt>start+len</tt>. This should be called whenever a page
    // mapping's permissions become more strict or the mapped page
    // changes. Any pages that need to be shot-down will have
    // their trackers accumulated in @c sd and cleared.
    void invalidate(uintptr_t start, uintptr_t len, shootdown *sd);

    void qinsert(uintptr_t va, pme_t pte);

    // Switch to this page_map_cache on this CPU.
    void switch_to() const;

    // Switch out of this page_map_cache on this CPU.
    void switch_from() const;

    // Count the number of pages used by this page_map_cache.
    u64 internal_pages() const;
  };
}

namespace mmu = MMU_SCHEME;
