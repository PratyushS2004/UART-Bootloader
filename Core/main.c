#include <stdint.h>
#include "stm32f446xx.h"

int main(void)
{

  // Enable clock
  RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
  RCC->APB1ENR |= RCC_APB1ENR_USART2EN;

  // Enable PA2/3 for TX/RX to alternate function mode
  GPIOA->MODER &= ~((3 << 4) | (3 << 6)); // Clear bits
  GPIOA->MODER |= (2 << 4) | (2 << 6);

  // Enable AF7 (USART2) in PA2/3
  GPIOA->AFR[0] |= (7 << 8) | (7 << 12);
  
  // USART Config
  USART2->CR1 &= ~USART_CR1_M;     // 8 data bits
  USART2->CR2 &= ~USART_CR2_STOP;  // 1 stop bit
  USART2->BRR = (8 << 4) | 11;     // 115200 baud & 16MHz
  USART2->CR1 |= USART_CR1_TE | USART_CR1_RE | USART_CR1_UE; // Enable TX, RX, USART

  /* Infinite loop */
  while (1)
  {


  }
}



