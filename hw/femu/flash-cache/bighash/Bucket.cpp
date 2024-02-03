#include "Bucket.h"

#include <folly/Random.h>

namespace cache {
static_assert(sizeof(Bucket) == 24,
              "Bucket overhead. If this changes, you may have to adjust the "
              "sizes used in unit tests.");

namespace {
const details::BucketEntry* getIteratorEntry(BucketStorage::Allocation itr) {
  return reinterpret_cast<const details::BucketEntry*>(itr.view().data());
}
} // namespace

BufferView Bucket::Iterator::key() const {
  return getIteratorEntry(itr_)->key();
}

uint64_t Bucket::Iterator::keyHash() const {
  return getIteratorEntry(itr_)->keyHash();
}

BufferView Bucket::Iterator::value() const {
  return getIteratorEntry(itr_)->value();
}

bool Bucket::Iterator::keyEqualsTo(HashedKey hk) const {
  return getIteratorEntry(itr_)->keyEqualsTo(hk);
}

uint32_t Bucket::computeChecksum(BufferView view) {
  constexpr auto kChecksumStart = sizeof(checksum_);
  auto data = view.slice(kChecksumStart, view.size() - kChecksumStart);
  return cache::checksum(data);
}

BufferView Bucket::find(HashedKey hk) const {
  auto itr = storage_.getFirst();
  while (!itr.done()) {
    auto* entry = getIteratorEntry(itr);
    if (entry->keyEqualsTo(hk)) {
      return entry->value();
    }
    itr = storage_.getNext(itr);
  }
  return {};
}

uint32_t Bucket::remove(HashedKey hk, const DestructorCallback& destructorCb) {
  auto itr = storage_.getFirst();
  while (!itr.done()) {
    auto* entry = getIteratorEntry(itr);
    if (entry->keyEqualsTo(hk)) {
      if (destructorCb) {
        destructorCb(entry->hashedKey(), entry->value(),
                     DestructorEvent::Removed);
      }
      storage_.remove(itr);
      return 1;
    }
    itr = storage_.getNext(itr);
  }
  return 0;
}

Bucket::Iterator Bucket::getFirst() const {
  return Iterator{storage_.getFirst()};
}

Bucket::Iterator Bucket::getNext(Iterator itr) const {
  return Iterator{storage_.getNext(itr.itr_)};
}
} // namespace cache