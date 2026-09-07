#include <stdint.h>
#include "main.h"
#include "stm32f446xx.h"

uint8_t erase_single_sector(uint8_t sector);
uint8_t wait_BSY(void);
uint8_t sector_erase(uint8_t last_sector);
uint8_t last_sector(uint32_t length);

typedef struct 
{
    uint8_t sector;   // Sector number
    uint32_t address; // Sector start address
}flash_sector;

static const flash_sector sector_table[] = {
    {2, 0x08008000}, // Sector 2, 16K
    {3, 0x0800C000}, // Sector 3, 16K
    {4, 0x08010000}, // Sector 4, 64K
    {5, 0x08020000}, // Sector 5, 128K
    {6, 0x08040000}, // Sector 6, 128K
    {7, 0x08060000}  // Sector 7, 128K
};

/* 
Given the lengh, find the last sector the payload covers
*/
uint8_t last_sector(uint32_t length){
    if (length == 0) {
        return 100; // Invalid Sector
    }

    uint8_t sector = 100; //Invalid Sector
    uint32_t end_address = sector_table[0].address + length - 1;
    const uint8_t total_sectors = 6;

    for(uint8_t i = 0; i < total_sectors; i++){
        if(end_address >= sector_table[i].address){
            sector = sector_table[i].sector;
        }
    }
    return sector;
}

/*
Erases sector.
Returns 1 for its successful or 0 if not.
*/
uint8_t erase_single_sector(uint8_t sector){
    FLASH->CR &= ~FLASH_CR_SNB; // Clear SNB
    FLASH->CR |= FLASH_CR_SER;  // Sector Erase Activated
    FLASH->CR |= sector << FLASH_CR_SNB_Pos; // Select sector to erase
    FLASH->CR |= FLASH_CR_STRT; // Trigger erase operation

    uint8_t bsy_ok = wait_BSY();
    FLASH->CR &= ~FLASH_CR_SER; // Clear SER

    if(!bsy_ok){ // Check BSY status
        return 0;
    }
    
    if(FLASH->SR & FLASH_SR_WRPERR){ // Write protection error
        FLASH->SR |= FLASH_SR_WRPERR; // Clear flag
        return 0; 
    }

    return 1;
}

uint8_t wait_BSY(void) {
    uint32_t timer = 3000;
    uint32_t start = ms_ticks;

    while(FLASH->SR & FLASH_SR_BSY){ // Wait for BSY to clear
        if ((ms_ticks - start) >= timer) {
            return 0; // Timed out
        }
    }
    return 1;
}

uint8_t sector_erase(uint8_t last_sector){
    if(last_sector > 7 || last_sector < 2){ // Invalid sector
        return 0;
    }
    uint8_t erase_ok = 1;

    if(FLASH->CR & FLASH_CR_LOCK){
    // Unlock Sequence
    FLASH->KEYR = 0x45670123; // KEY1
    FLASH->KEYR = 0xCDEF89AB; // KEY2
    }

    if(wait_BSY()){
        for( uint8_t sector = 2; sector <= last_sector; sector++){
            if(erase_single_sector(sector) == 0){ // Erase helper threw an error
               erase_ok = 0;  
               break;
            }
        }   
    }else{
        erase_ok = 0;
    }
    
    if(wait_BSY()){
        FLASH->CR |= FLASH_CR_LOCK; // Lock
    }

    return erase_ok;
}

void flash_init(void) {
    // Lock in 32-bit parallelism for 3.3V board rail
    FLASH->CR &= ~FLASH_CR_PSIZE;
    FLASH->CR |= FLASH_CR_PSIZE_1;
}

uint8_t program_word(uint32_t address, uint32_t data){
    if (address % 4 != 0) { // Checks 32-bit (4-byte) alignment
        return 0;
    }

    if(!wait_BSY()){ // Check BSY status
        return 0;
    }

    FLASH->CR |= FLASH_CR_PG; // Set Programming
    *(uint32_t*) address = data; // Push data into address
    
    uint8_t bsy_ok = wait_BSY(); 
    FLASH->CR &= ~FLASH_CR_PG; // Clear PG

    if(!bsy_ok){ // Check BSY status
        return 0;
    }

    if (FLASH->SR & (FLASH_SR_PGAERR | FLASH_SR_PGPERR | FLASH_SR_PGSERR)){ // Check status flags
        FLASH->SR |= FLASH_SR_PGAERR | FLASH_SR_PGPERR | FLASH_SR_PGSERR; // Clear status flags
        return 0;
    }

    return 1;
}