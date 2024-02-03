#pragma once

#include <folly/Range.h>

#include <cstdint>
#include <functional>
#include <ostream>

#include "Buffer.h"
#include "Hash.h"

namespace cache {
// Generic operation status
enum class Status {
  Ok,

  // Entry not found
  NotFound,

  // Operations were rejected (queue full, admission policy, etc.)
  Rejected,

  // Resource is temporary busy
  Retry,

  // Device IO error or out of memory
  DeviceError,

  // Internal invariant broken. Consistency may be violated.
  BadState,
};

enum class DestructorEvent {
  // space is recycled (item evicted)
  Recycled,
  // item is removed from NVM
  Removed,
  // item already in the queue but failed to put into NVM
  PutFailed,
};

// @key and @value are valid only during this callback invocation
using DestructorCallback =
    std::function<void(HashedKey hk, BufferView value, DestructorEvent event)>;

// Checking NvmItem expired
using ExpiredCheck = std::function<bool(BufferView value)>;

constexpr uint32_t kMaxKeySize{255};

// Convert status to string message. Return "Unknown" if invalid status.
const char* toString(Status status);

// Convert event to string message. Return "Unknown" if invalid event.
// const char* toString(DestructorEvent e);

inline std::ostream& operator<<(std::ostream& os, Status status) {
  return os << "Status::" << toString(status);
}

// inline std::ostream& operator<<(std::ostream& os, DestructorEvent e) {
//   return os << "DestructorEvent::" << toString(e);
// }
} // namespace cache