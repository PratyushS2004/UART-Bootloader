/**
 * Architectural Verification Payload:
 * 1. Jump Proof: Initial fast blinking on PA5 (LD2) proves execution transferred 
 *    successfully to application Flash space (0x08008000).
 * 2. VTOR & Interrupt Relocation Proof: A SysTick exception fires ~1 second after
 *    boot, permanently slowing the blink rate. Successful dispatch of SysTick_Handler
 *    proves SCB->VTOR points to 0x08008000 and the CPU correctly serviced the vector table.
 */

#include <stdint.h>
#include "stm32f446xx.h"

void delay(void);

/* Volatile loop iteration count to prevent compiler optimization of software delay */
volatile uint32_t delay_loops = 50000;

int main(void) {
  /* Relocate Vector Table Offset Register to the application start address */
  SCB->VTOR = 0x08008000U;

  /* Enable peripheral clock for GPIOA */
  RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;

  /* Configure PA5 as general-purpose output */
  GPIOA->MODER &= ~GPIO_MODER_MODE5;
  GPIOA->MODER |=  GPIO_MODER_MODE5_0;

  /* Update SystemCoreClock variable and configure SysTick for ~1s interval */
  SystemCoreClockUpdate();
  SysTick_Config(SystemCoreClock);

  while (1) {
    GPIOA->BSRR = GPIO_BSRR_BS5;  /* Set PA5 HIGH */
    delay();

    GPIOA->BSRR = GPIO_BSRR_BR5;  /* Reset PA5 LOW */
    delay();
  }
}

void delay(void) {
  for (volatile uint32_t i = 0; i < delay_loops; i++) {
    __asm("nop");
  }
}

/**
 * Disables SysTick and increases software loop delay, permanently slowing the blink rate.
 */
void SysTick_Handler(void) {
  /* Disable SysTick timer to make this a one-shot state change */
  SysTick->CTRL &= ~SysTick_CTRL_ENABLE_Msk;

  /* Transition from fast blink to slow blink */
  delay_loops = 500000;
}