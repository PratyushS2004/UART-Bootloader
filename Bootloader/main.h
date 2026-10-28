#ifndef MAIN_H
#define MAIN_H

#include <stdint.h>
#include <stdbool.h>
#include "stm32f446xx.h"
#include "memory_map.h"

#define CHUNK_PAYLOAD_SIZE 1024 // 1KB chunk payload

// Flash Module Data Structures
typedef struct {
    uint8_t sector;
    uint32_t address;
} flash_sector;

extern const flash_sector sector_table[];

extern volatile uint32_t ms_ticks; 

// Flash Driver Public API (flash.c)
uint8_t program_word(uint32_t address, uint32_t data);
uint8_t erase_for_length(uint32_t payload_length);
uint8_t erase_marker(void); // 1 on success, 0 on failure
uint8_t is_length_valid(uint32_t payload_length); // 1 if erase_for_length would accept it, no side effects
uint8_t write_marker(uint32_t size, uint32_t crc); // 1 on success, 0 on failure
void flash_init(void);

// UART & Bootloader Public API (uart.c)
void state_machine(void);
void UART_Config(void);
bool app_is_valid(void);
bool image_marker_valid(void); // Needs the CRC clock enabled
void jump_to_application(void)__attribute__((noreturn));

#endif