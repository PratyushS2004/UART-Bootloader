#ifndef MAIN_H
#define MAIN_H

#include <stdint.h>
#include <stdbool.h>
#include "stm32f446xx.h"
#include "memory_map.h"

#define CHUNK_PAYLOAD_SIZE 1024 // 1KB chunk payload
#define MAX_PAYLOAD_SIZE 491520 // 480KB

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
void flash_init(void);

// UART & Bootloader Public API (uart.c)
void state_machine(void);
void UART_Config(void);
bool app_is_valid(void);
void jump_to_application(void)__attribute__((noreturn));

#endif