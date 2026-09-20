#include <stdint.h>
#include "main.h"
#include "stm32f446xx.h"
#include <stdbool.h>

static bool is_button_pressed (void);

// Global millisecond tick counter updated by SysTick ISR
volatile uint32_t ms_ticks = 0;

int main(void)
{
    // Check boot pin condition before enabling bootloader peripherals.
    // If button is not held down (returns false), boot straight to application.
    if(!is_button_pressed()){
        jump_to_application();
    }
        
    // Setup System Clock & SysTick (1ms interrupt) 
    SystemCoreClockUpdate();
    SysTick_Config(SystemCoreClock / 1000);

    // Enable hardware CRC peripheral clock (required for chunk CRC verification)
    RCC->AHB1ENR |= RCC_AHB1ENR_CRCEN;

    // Initialize Flash parallelism, UART peripheral, and launch bootloader FSM
    flash_init();
    UART_Config();
    state_machine();
}

// SysTick interrupt service routine
void SysTick_Handler(void) {
    ms_ticks++;
}

/*
 * Checks if the user button (PC13 / Blue Button B1 on Nucleo) is pressed.
 * Logic: PC13 is Active-LOW (0 = Pressed, 1 = Released).
 */
static bool is_button_pressed (void){
    // Enable GPIOC clock
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;

    // Configure PC13 as Input Mode
    GPIOC->MODER &= ~GPIO_MODER_MODE13_Msk;

    // Enable Internal Pull-Up
    GPIOC->PUPDR &= ~GPIO_PUPDR_PUPD13_Msk;
    GPIOC->PUPDR |=  GPIO_PUPDR_PUPD13_0;

    // Returns true if pin reads 0 (button pressed)
    return ((GPIOC->IDR & GPIO_IDR_ID13) == 0);
}