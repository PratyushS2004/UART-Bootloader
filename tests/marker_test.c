// Host-side test for marker_header_valid. Build: gcc -IBootloader tests/marker_test.c <marker.c>
#include <stdio.h>
#include "marker.h"
#include "memory_map.h"

typedef struct {
  const char *name;
  Image_marker marker;
  bool expected;
} Case;

static const Case cases[] = {
  {"valid 1352 byte image",        {MARKER_MAGIC, 1352,                 0x1234}, true },
  {"largest allowed size",         {MARKER_MAGIC, MAX_PAYLOAD_SIZE,     0x1234}, true },
  {"one word past largest size",   {MARKER_MAGIC, MAX_PAYLOAD_SIZE + 4, 0x1234}, false},
  {"smallest allowed size",        {MARKER_MAGIC, 4,                    0x1234}, true },
  {"size zero",                    {MARKER_MAGIC, 0,                    0x1234}, false},
  {"size not a word multiple",     {MARKER_MAGIC, 1351,                 0x1234}, false},
  {"torn: magic only",             {MARKER_MAGIC, 0xFFFFFFFFU,     0xFFFFFFFFU}, false},
  {"wrong magic",                  {0x89ABCDEEU,  1352,                 0x1234}, false},
  {"erased marker sector",         {0xFFFFFFFFU,  0xFFFFFFFFU,     0xFFFFFFFFU}, false},
};

int main(void) {
  int failures = 0;
  for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    bool got = marker_header_valid(&cases[i].marker);
    if (got != cases[i].expected) {
      printf("FAIL: %s (expected=%d got=%d)\n", cases[i].name, cases[i].expected, got);
      failures++;
    }
  }
  printf("%d failure(s)\n", failures);
  return failures != 0;
}
