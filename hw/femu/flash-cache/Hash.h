#pragma once

#include <folly/hash/Hash.h>

#include <folly/Range.h>

#include "BytesEqual.h"

// combines two 64 bit hash into one
inline uint64_t combineHashes(uint64_t h1, uint64_t h2) {
  return folly::hash::hash_128_to_64(h1, h2);
}

inline uint64_t hashInt(uint64_t key) { return folly::hash::twang_mix64(key); }

// Pairs up key and hash together, reducing the cost of computing hash multiple
// times, and eliminating possibility to modify one of them independently.
class HashedKey {
 public:
  static HashedKey precomputed(folly::StringPiece key, uint64_t keyHash) {
    return HashedKey{key, keyHash};
  }

  explicit HashedKey(folly::StringPiece key)
      : HashedKey{key, hashBuffer(key)} {}

  HashedKey(const char* key, size_t size)
      : HashedKey{folly::StringPiece{key, size}} {}

  folly::StringPiece key() const { return key_; }

  uint64_t keyHash() const { return keyHash_; }

  bool operator==(HashedKey other) const {
    return keyHash_ == other.keyHash_ && key_.size() == other.key().size() &&
           bytesEqual(key_.data(), other.key().data(), key_.size());
  }

  bool operator!=(HashedKey other) const { return !(*this == other); }

 private:
  HashedKey(folly::StringPiece key, uint64_t keyHash)
      : key_{key}, keyHash_{keyHash} {}

  // copy from navy/common/Hash.h to keep consistent hash behavior for keys
  // TODO: may use MurmurHash2 later
  static uint64_t hashBuffer(folly::StringPiece key, uint64_t seed = 0) {
    return folly::hash::SpookyHashV2::Hash64(key.data(), key.size(), seed);
  }

  folly::StringPiece key_;
  uint64_t keyHash_{};
};