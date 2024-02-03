#pragma once

#include <cstdint>

#include "../Hash.h"
#include "Buffer.h"

namespace cache {
// Default hash function
uint64_t hashBuffer(BufferView key, uint64_t seed = 0);

// Default checksumming function
uint32_t checksum(BufferView data, uint32_t startingChecksum = 0);

// Convenience utils to convert a piece of buffer to a hashed key
inline HashedKey makeHK(const void* ptr, size_t size) {
  return HashedKey{
      folly::StringPiece{reinterpret_cast<const char*>(ptr), size}};
}
inline HashedKey makeHK(BufferView key) {
  return makeHK(key.data(), key.size());
}
inline HashedKey makeHK(const Buffer& key) {
  BufferView view = key.view();
  return makeHK(view.data(), view.size());
}
inline HashedKey makeHK(const char* cstr) {
  return HashedKey{folly::StringPiece{cstr}};
}

} // namespace cache