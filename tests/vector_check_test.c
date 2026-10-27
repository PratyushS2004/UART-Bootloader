// Host-side test for vector_table_valid. Build: gcc -IBootloader tests/vector_check_test.c <vector_check.c>
#include <stdio.h>
#include "vector_check.h"

typedef struct {
  const char *name;
  uint32_t msp;
  uint32_t reset;
  bool expected;
} Case;

static const Case cases[] = {
  {"valid app",                     0x20020000U, 0x08008101U, true },
  {"thumb bit clear",               0x20020000U, 0x08008100U, false},
  {"reset one past flash end",      0x20020000U, 0x08080001U, false},
  {"reset below app start",         0x20020000U, 0x08007FFFU, false},
  {"blank chip",                    0xFFFFFFFFU, 0xFFFFFFFFU, false},
  {"reset at exact app start",      0x20020000U, 0x08008001U, true },
  {"reset at last flash byte",      0x20020000U, 0x0807FFFFU, true },
  {"msp one past sram end",         0x20020001U, 0x08008101U, false},
  {"msp below sram start",          0x1FFFFFFFU, 0x08008101U, false},
};

int main(void) {
  int failures = 0;
  for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    bool got = vector_table_valid(cases[i].msp, cases[i].reset);
    if (got != cases[i].expected) {
      printf("FAIL: %s (msp=0x%08X reset=0x%08X expected=%d got=%d)\n",
             cases[i].name, (unsigned)cases[i].msp, (unsigned)cases[i].reset,
             cases[i].expected, got);
      failures++;
    }
  }
  printf("%d failure(s)\n", failures);
  return failures != 0;
}
