#ifndef MAIN_H
#define MAIN_H

#include <stdint.h>
#include <stdbool.h>
#include "stm32f446xx.h"

#define CHUNK_PAYLOAD_SIZE 1024 // 1KB chunk payload
#define MAX_PAYLOAD_SIZE 491520 // 480KB
#define SRAM_START  0x20000000U
#define SRAM_END    0x20020000U
#define FLASH_ADDR 0x08008000U

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
void jump_to_application(void);

#endif