#include <stdint.h>
#include "main.h"
#include "stm32f446xx.h"

#define APP_OFFSET_ADDRESS 0x8008000U

void delay(void);
void jump_to_application(void);

volatile uint32_t ms_ticks = 0;

int main(void)
{

  /* USER CODE BEGIN SysInit */
  RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  /* USER CODE BEGIN 2 */

  GPIOA->MODER &= ~GPIO_MODER_MODE5;
  GPIOA->MODER |=  GPIO_MODER_MODE5_0;

  SystemCoreClockUpdate();
  SysTick_Config(SystemCoreClock / 1000);

  int i = 0;
  while (i < 20)
  {

    GPIOA->BSRR = GPIO_BSRR_BS5;
    delay();

    GPIOA->BSRR = GPIO_BSRR_BR5;
    delay();

    i++;
  }
  jump_to_application();
}
  
void SysTick_Handler(void) {
    ms_ticks++;
}

void delay(void) {
    for (volatile int i = 0; i < 500000; i++) {
        __asm("nop"); // Forces the CPU to wait one instruction cycle
    }
}

typedef void (*ResetHandler_t)(void);

void jump_to_application(void){   

    SCB->VTOR = (uint32_t)APP_OFFSET_ADDRESS;
    uint32_t app_msp = *(uint32_t *)APP_OFFSET_ADDRESS;
    ResetHandler_t app_reset_handler;
    app_reset_handler = (ResetHandler_t)(*((uint32_t *)(APP_OFFSET_ADDRESS + 4)));

    __set_MSP(app_msp);
    app_reset_handler();

}

