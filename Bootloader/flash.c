#include "stm32f446xx.h"

uint8_t flash_erase_sector(uint8_t end_sector);
uint8_t flash_wait(void);

/*
Clears sector starting from sector 2 to end_sector.
Returns 1 for its success or 0 if not.
*/
uint8_t flash_erase_sector(uint8_t end_sector){
        FLASH->CR &= ~FLASH_CR_SNB;
        FLASH->CR |= FLASH_CR_SER;
        FLASH->CR |= end_sector << FLASH_CR_SNB_Pos;
        FLASH->CR |= FLASH_CR_STRT;
        if(flash_wait()){
            if(FLASH->SR & FLASH_SR_WRPERR){
                return 0;
            }
        }else{ return 0; }
}

uint8_t flash_wait(void) {
    uint32_t timer = 10000000;
    while(FLASH->SR & FLASH_SR_BSY){
        if(--timer == 0){
            return 0;
        }
    }
    return 1;
}