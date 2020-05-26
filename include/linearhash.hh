#pragma once

/*
 * A linear-probing hash table.
 */

#include "spinlock.hh"
#include "seqlock.hh"
#include "lockwrap.hh"
#include "hash.hh"

template<class K, class V, class Allocator = new_delete_allocator<void>>
class linearhash {
private:
  struct slotdata {
    slotdata() : used(false), valid(false) {}

    K key;
    V val;
    bool used;
    bool valid;
  };

  struct slot {
    slotdata data;
    spinlock lock;
    seqcount<u32> seq;
  };

  u64 nslots_;
  slot* slots_;

  typename Allocator::template rebind<slot>::other slots_alloc_;

public:
  linearhash(u64 nslots) : nslots_(nslots) {
    slots_ = slots_alloc_.allocate(nslots_);
    assert(slots_);
  }

  ~linearhash() {
    slots_alloc_.free(slots_, nslots_);
  }

  NEW_DELETE_OPS(linearhash);

  class iterator  {
  private:
    slot* slot_;
    iterator(slot* s): slot_(s) {}

    friend class linearhash;
  public:
    iterator& operator++() {
      slot_++;
      return *this;
    }
    iterator operator++(int) {
      iterator retval = *this;
      ++(*this);
      return retval;
    }
    bool operator==(iterator other) const {return slot_ == other.slot_;}
    bool operator!=(iterator other) const {return !(*this == other);}
    bool get(K* key, V* val){
      auto copy = seq_reader<slotdata>(&slot_->data, &slot_->seq);
      if(!copy->valid)
        return false;
      *key = copy->key;
      *val = copy->val;
      return true;
    }
  };
  iterator begin() {
    return iterator(&slots_[0]);
  }
  iterator end() {
    return iterator(&slots_[nslots_]);
  }

  bool insert(const K& k, const V& v) {
    u64 h = hash(k);
    for (u64 i = 0; i < nslots_; i++) {
      slot* s = &slots_[(h + i) % nslots_];
      scoped_acquire l(&s->lock);
      if (s->data.valid)
        continue;

      auto w = s->seq.write_begin();
      s->data.used = true;
      s->data.valid = true;
      s->data.key = k;
      s->data.val = v;
      return true;
    }

    panic("insert: out of slots");
  }

  bool enumerate(const K* prev, K* out) const {
    bool prevbucket = prev != nullptr;

    for(slot* s = prev ? (slot*)prev : slots_; s < slots_ + nslots_; s++) {
      if(s->data.valid) {
        *out = s->data.key;
        return true;
      }
    }
    return false;
  }

  bool remove(const K& k) {
    u64 h = hash(k);
    for (u64 i = 0; i < nslots_; i++) {
      slot* s = &slots_[(h + i) % nslots_];
      scoped_acquire l(&s->lock);
      if (!s->data.used)
        break;
      if (s->data.key == k) {
        auto w = s->seq.write_begin();
        s->data.valid = false;
        return true;
      }
    }
    return false;
  }

  bool remove2(const K& k, const V& v) {
    u64 h = hash(k);
    for (u64 i = 0; i < nslots_; i++) {
      slot* s = &slots_[(h + i) % nslots_];
      scoped_acquire l(&s->lock);
      if (!s->data.used)
        break;
      if (s->data.key == k && s->data.val == v) {
        auto w = s->seq.write_begin();
        s->data.valid = false;
        return true;
      }
    }
    return false;
  }

  bool lookup(const K& k, V* vptr) const {
    u64 h = hash(k);
    for (u64 i = 0; i < nslots_; i++) {
      const slot* s = &slots_[(h + i) % nslots_];
      auto copy = seq_reader<slotdata>(&s->data, &s->seq);
      if (!copy->used)
        break;
      if (copy->valid && copy->key == k) {
        if (vptr)
          *vptr = copy->val;
        return true;
      }
    }
    return false;
  }

  void increment(const K& k) const {
    u64 h = hash(k);
    for (u64 i = 0; i < nslots_; i++) {
      slot* s = &slots_[(h + i) % nslots_];
      scoped_acquire l(&s->lock);
      auto w = s->seq.write_begin();
      if (!s->data.used) {
        s->data.used = true;
        s->data.valid = true;
        s->data.key = k;
        s->data.val = 1;
        return;
      } else if (s->data.valid && s->data.key == k) {
        ++s->data.val;
        return;
      }
    }
    panic("increment: out of slots");
  }
};
