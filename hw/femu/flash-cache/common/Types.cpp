#include "Types.h"

namespace cache {
const char* toString(Status status) {
  switch (status) {
  case Status::Ok:
    return "Ok";
  case Status::NotFound:
    return "NotFound";
  case Status::Rejected:
    return "Rejected";
  case Status::Retry:
    return "Retry";
  case Status::DeviceError:
    return "DeviceError";
  case Status::BadState:
    return "BadState";
  }
  return "Unknown";
}

/*
const char* toString(DestructorEvent e) {
  switch (e) {
  case DestructorEvent::Recycled:
    return "Recycled";
  case DestructorEvent::Removed:
    return "Removed";
  case DestructorEvent::PutFailed:
    return "PutFailed";
  }
  return "Unknown";
}
*/
} // namespace cache