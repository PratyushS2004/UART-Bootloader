#include <stdint.h>
#include "main.h"
#include "stm32f446xx.h"
#include <stdbool.h>

static bool is_button_pressed (void);

// Global millisecond tick counter updated by SysTick ISR
volatile uint32_t ms_ticks = 0;

int main(void)
{
    // Enable hardware CRC peripheral clock. Needed for chunk CRC verification, and now also for
    // the boot-time marker check, so it has to come before the jump decision. Like GPIOC below,
    // the jump leaves it on, which is harmless since the app sets its own clocks with |=.
    RCC->AHB1ENR |= RCC_AHB1ENR_CRCEN;

    // Check boot pin condition before enabling the other bootloader peripherals.
    // If button is not held down and the app is valid, boot straight to application. That means
    // a sane vector table and a marker whose CRC matches the image. Otherwise fall through and
    // run the bootloader so a blank or torn chip can be programmed.
    if(!is_button_pressed() && app_is_valid() && image_marker_valid()){
        jump_to_application();
    }

    // Setup System Clock & SysTick (1ms interrupt)
    SystemCoreClockUpdate();
    SysTick_Config(SystemCoreClock / 1000);

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