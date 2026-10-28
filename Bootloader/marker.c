#include "marker.h"
#include "memory_map.h"

bool marker_header_valid(const Image_marker *marker) {
  if (marker->magic != MARKER_MAGIC) {
    return false; // Erased flash, torn record or garbage
  }

  // A torn record can have a good magic and a size that is still 0xFFFFFFFF
  if ((marker->size == 0) || ((marker->size % 4) != 0) || (marker->size > MAX_PAYLOAD_SIZE)) {
    return false;
  }
  return true;
}
