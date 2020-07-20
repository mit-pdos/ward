#include "types.h"
#include "kernel.hh"
#include "spinlock.hh"
#include "cpputil.hh"
#include "ns.hh"
#include "errno.h"
#include "condvar.hh"
#include "proc.hh"
#include "cpu.hh"
#include "spercpu.hh"
#include "vm.hh"
#include "hash.hh"

#define FUTEX_HASH_BUCKETS 257

struct futex_list_bucket {
  ilist<proc, &proc::futex_link> items;
  spinlock lock;
};

struct futex_list {
  futex_list_bucket buckets[FUTEX_HASH_BUCKETS];
};

futex_list futex_waiters __attribute__((section (".qdata")));

futexkey::futexkey(uintptr_t useraddr, const sref<class vmap>& vmap_, bool priv) : ptr(nullptr) {
  if ((useraddr & 0x3) != 0)
    panic("misaligned futex address");

  u64 pageidx;
  if (!priv && (pageable = vmap_->lookup_pageable(useraddr, &pageidx))) {
    shared = true;
    address = pageidx * PGSIZE + useraddr % PGSIZE;
  } else {
    shared = false;
    vmap = vmap_.get();
    address = useraddr;
  }
}

futexkey::futexkey(futexkey&& other) : futexkey() { *this = std::move(other); }

futexkey& futexkey::operator=(futexkey&& other) noexcept {
  shared = other.shared;
  address = other.address;
  ptr = other.ptr;
  other.ptr = nullptr;
  return *this;
}

futexkey::~futexkey() {
  if (shared)
    pageable.reset();
}

bool futexkey::operator==(const futexkey& other) {
  return address == other.address && ptr == other.ptr && shared == other.shared;
}

u64 futex_bucket(futexkey* key) {
  return (hash(key->address) ^ hash(key->ptr)) % FUTEX_HASH_BUCKETS;
}

u32 futexval(futexkey* key)
{
  if (key->shared) {
    auto p = key->pageable->get_page_info(PGROUNDDOWN(key->address));
    return *(u32*)((char*)p->va() + (key->address % PGSIZE));
  }

  u32 val;
  if (key->vmap == myproc()->vmap.get() && !fetchmem_ncli(&val, (const void*)(key->address), 4))
    return val;

  u32* kva = (u32*)key->vmap->pagelookup(key->address);
  return kva ? *kva : 0;
}

long futexwait(futexkey&& key, u32 val, u64 timer)
{
  futex_list_bucket* bucket = &futex_waiters.buckets[futex_bucket(&key)];
  scoped_acquire l(&bucket->lock);

  if (futexval(&key) != val)
    return -EWOULDBLOCK;

  myproc()->futex_key = std::move(key);
  bucket->items.push_back(myproc());

  u64 nsecto = timer == 0 ? 0 : timer+nsectime();
  myproc()->cv->sleep_to(&bucket->lock, nsecto);

  bucket->items.erase(iiterator<proc, &proc::futex_link>(myproc()));
  return 0;
}

long futexwake(futexkey&& key, u64 nwake)
{
  if (nwake == 0) {
    return -EINVAL;
  }

  futex_list_bucket* bucket = &futex_waiters.buckets[futex_bucket(&key)];
  scoped_acquire l(&bucket->lock);

  u64 nwoke = 0;
  for(auto i = bucket->items.begin(); i != bucket->items.end() && nwoke < nwake; i++) {
    if (i->futex_key == key) {
      i->cv->wake_all();
      nwoke++;
    }
  }

  return nwoke;
}
