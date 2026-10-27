#include "vector_check.h"
#include "memory_map.h"

bool vector_table_valid(uint32_t msp, uint32_t reset) {
  // Initial SP is one past the end of RAM on a full descending stack, so SRAM_END is allowed
  if ((msp < SRAM_START) || (msp > SRAM_END)) {
    return false;
  }

  // Thumb bit must be set, and the real address must land inside the app's flash
  if ((reset & 1U) == 0) {
    return false;
  }
  uint32_t reset_addr = reset & ~1U;
  return (reset_addr >= FLASH_ADDR) && (reset_addr < APP_FLASH_END);
}
