#include "stm32f446xx.h"

uint8_t erase_single_sector(uint8_t sector);
uint8_t clear_BSY(void);
uint8_t sector_erase(uint8_t last_sector);

/*
Erases sector.
Returns 1 for its successful or 0 if not.
*/
uint8_t erase_single_sector(uint8_t sector){
        FLASH->CR &= ~FLASH_CR_SNB; // Clear SNB
        FLASH->CR |= FLASH_CR_SER;  // Sector Erase Activated
        FLASH->CR |= sector << FLASH_CR_SNB_Pos; // Select sector to erase
        FLASH->CR |= FLASH_CR_STRT; // Trigger erase operation
        if(wait_BSY()){
            if(FLASH->SR & FLASH_SR_WRPERR){ // Write protection error
                return 0;
            }
        }else{ return 0; }
    return 1;
}

uint8_t wait_BSY(void) {
    uint32_t timer = 10000000;
    while(FLASH->SR & FLASH_SR_BSY){ // Wait for BSY to clear
        if(--timer == 0){
            return 0;
        }
    }
    return 1;
}

uint8_t sector_erase(uint8_t last_sector){
    uint8_t erase_ok = 1;

    if(FLASH->CR & FLASH_CR_LOCK){
    // Unlock Sequence
    FLASH->KEYR = 0x45670123; // KEY1
    FLASH->KEYR = 0xCDEF89AB; // KEY2
    }

    if(wait_BSY()){
        for( uint8_t sector = 2; sector <= last_sector; sector++){
            if(erase_single_sector(sector) == 0){ // erase helper threw an error
               erase_ok = 0;  
               break;
            }
        }   
    }else{
        erase_ok = 0;
    }
    
    FLASH->CR |= FLASH_CR_LOCK; // Lock
    return erase_ok;
}