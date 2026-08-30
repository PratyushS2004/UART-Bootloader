#include <stdint.h>
#include "stm32f446xx.h"


void delay(void);


int main(void)
{

  /* USER CODE BEGIN SysInit */
  RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  /* USER CODE BEGIN 2 */

  GPIOA->MODER &= ~GPIO_MODER_MODE5;
  GPIOA->MODER |=  GPIO_MODER_MODE5_0;
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {

    /* USER CODE END WHILE */
    GPIOA->BSRR = GPIO_BSRR_BS5;
    delay();

    GPIOA->BSRR = GPIO_BSRR_BR5;
    delay();
    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}



void delay(void) {
    for (volatile int i = 0; i < 50000; i++) {
        __asm("nop"); // Forces the CPU to wait one instruction cycle
    }
}

