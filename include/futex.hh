#pragma once

#include "kernel.hh"
#include "uk/futex.h"

class pageable;

enum futex_kind {
  FUT_PAGEABLE,
  FUT_VMAP,
};

struct futexkey {
  union {
    void* ptr;
    // Futex is in some shared region
    sref<pageable> pageable;
    // Futex is in anonymous memory (or has the private bit set)
    vmap* vmap;
  };
  unsigned long address;
  bool shared;

  futexkey() : ptr(nullptr) {}
  futexkey(uintptr_t useraddr, const sref<class vmap>& vmap, bool priv);
  ~futexkey();

  bool operator==(const futexkey& other);

  futexkey(futexkey&&);
  futexkey& operator=(futexkey&&) noexcept;
};
