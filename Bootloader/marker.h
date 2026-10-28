#ifndef MARKER_H
#define MARKER_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Torn image record. Programmed one word at a time in field order, so the CRC word is
 * the commit. A record is only trusted if the reader recomputes the CRC over size bytes
 * of the image in flash and it matches. The magic alone is not enough.
 */
typedef struct {
  uint32_t magic; // MARKER_MAGIC
  uint32_t size;  // Image size in bytes as programmed, including the host's 0xFF padding
  uint32_t crc;   // CRC over the image in flash, written last
} Image_marker;

_Static_assert(sizeof(Image_marker) == 12, "Image_marker must be three words with no padding");

// Pure check of the fields that are safe to trust before computing a CRC, no hardware access.
// The size must pass this before it is ever used as a length to read flash.
bool marker_header_valid(const Image_marker *marker);

#endif
