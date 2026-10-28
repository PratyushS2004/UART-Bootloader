#ifndef MEMORY_MAP_H
#define MEMORY_MAP_H

#define SRAM_START  0x20000000U
#define SRAM_END    0x20020000U
#define FLASH_ADDR 0x08008000U
#define APP_FLASH_END 0x08080000U // One past the last flash byte (512KB part), exclusive

#define MAX_PAYLOAD_SIZE 491520U // 480KB, the app region FLASH_ADDR to APP_FLASH_END

#define MARKER_SECTOR 1U        // Flash sector number holding the image marker
#define MARKER_ADDR  0x08004000U // Start of sector 1, reserved for the image marker
#define MARKER_MAGIC 0x89ABCDEFU // Tells a written record apart from erased flash (0xFFFFFFFF)

#endif
