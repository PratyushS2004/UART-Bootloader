#include <stdint.h>
#include "stm32f446xx.h"
#include <stdbool.h>
#include "main.h"
#include <string.h>

#define ACK 0x06U
#define NAK 0x15U
#define UART_RX_TIMEOUT_MS 4000

// Bootloader state machine states
typedef enum{
  IDLE, RX_TOTAL_LENGTH, RX_CHUNK_LENGTH, RX_CHUNK_PAYLOAD, RX_CHUNK_CRC, DONE, ERROR 
}Boot_state;

// Bootloader runtime context
typedef struct{
  Boot_state state;

  uint8_t word_buf[4]; // 4-byte assembler for program_word()
  uint8_t byte_count;  // Assembler index (0 to 3)

  uint32_t total_length;   // Upfront total image size in bytes
  uint32_t total_bytes_rx; // Cumulative bytes processed across all chunks

  uint32_t current_chunk_length; // Expected size of active chunk
  uint32_t chunk_bytes_rx;       // Bytes received for current active chunk

  uint8_t chunk_buffer[CHUNK_PAYLOAD_SIZE]; // 1KB buffer for CRC verification

  Boot_state error_target; // Where the next state should be after an ERROR

  uint32_t received_CRC;   // CRC read from the wire
  uint32_t calculated_CRC; // Calcualted CRC
} Boot_context;

static Boot_context context;

static void send_char (uint8_t);
static uint8_t receive_char (void);
static bool assemble_byte(Boot_context *context, uint8_t byte, uint32_t *word);
static uint32_t bytes_to_word(const uint8_t *buf, uint32_t remaining_bytes);
static bool verify_chunk_crc(Boot_context *context);
static bool commit_chunk_to_flash(Boot_context *context);

/*
 * Assembles 4 incoming stream bytes into a little-endian 32-bit word.
 * Returns true once 4 bytes are accumulated, false otherwise.
 */
static bool assemble_byte(Boot_context *context, uint8_t byte, uint32_t *word){
  context->word_buf[context->byte_count] = byte;
  context->byte_count++;

  if(context->byte_count == 4){ 
    // Little-Endian transfer
    *word = ((uint32_t)context->word_buf[0]) | ((uint32_t)context->word_buf[1] << 8) |
                ((uint32_t)context->word_buf[2] << 16) | ((uint32_t)context->word_buf[3] << 24);

    context->byte_count = 0;
    return true;
  }

  return false; 
}

// Main bootloader control loop
void state_machine(void){ 
  while (1){
    switch(context.state){
      case IDLE:{
        // Wait for initial activity before starting transfer
        while((USART2->SR & USART_SR_RXNE) == 0){ }
        context.byte_count = 0;
        context.total_bytes_rx = 0;
        context.state = RX_TOTAL_LENGTH;  
        break;
      }
      // Receive 4-byte total image size and erase target Flash sectors
      case RX_TOTAL_LENGTH:{
        uint8_t rx_byte = receive_char();
        if (assemble_byte(&context, rx_byte, &context.total_length)) {
          if(context.total_length >= MAX_PAYLOAD_SIZE){
            context.error_target = IDLE;
            context.state = ERROR;
          }else{
            if(erase_for_length(context.total_length)){ // Erase for the payload image
              context.byte_count = 0;
              context.state = RX_CHUNK_LENGTH;
              send_char(ACK); // Erase completed, send next field
            }else{
              context.error_target = IDLE;
              context.state = ERROR;
            }
          }
        }
        break;
      }
      case RX_CHUNK_LENGTH:{
        // Accumulate 4 bytes for current chunk length
        uint8_t rx_byte = receive_char();
        if (assemble_byte(&context, rx_byte, &context.current_chunk_length)) {
          // Check running chunk length against running over given total length
          if(context.current_chunk_length == 0 || 
            context.current_chunk_length > CHUNK_PAYLOAD_SIZE || 
            context.current_chunk_length > (context.total_length - context.total_bytes_rx )){
            context.error_target = RX_CHUNK_LENGTH;
            context.state = ERROR;
          }else{
            context.chunk_bytes_rx = 0; // Reset counter
            context.state = RX_CHUNK_PAYLOAD;
          }
        }
        break;
      }
      case RX_CHUNK_PAYLOAD:{
        uint8_t rx_byte = receive_char();

        // Write raw bytes into buffer
        context.chunk_buffer[context.chunk_bytes_rx] = rx_byte;
        context.chunk_bytes_rx++;

        // Done receving bytes when counter reaches length
        if(context.chunk_bytes_rx == context.current_chunk_length){
          context.byte_count = 0;
          context.state = RX_CHUNK_CRC;
        }
        break;
      }
      case RX_CHUNK_CRC:{
        // Accumulate 4 bytes for current chunk CRC
        uint8_t rx_byte = receive_char();
        if (assemble_byte(&context, rx_byte, &context.received_CRC)) {
          // Verify Hardware CRC
          if(!verify_chunk_crc(&context)) {
            context.error_target = RX_CHUNK_LENGTH;
            context.state = ERROR;
            break;
          }
          
          // Commit to Flash (Only runs if CRC matched)
          if (!commit_chunk_to_flash(&context)) {
            context.error_target = RX_CHUNK_LENGTH;
            context.state = ERROR;
            break;
          }

          // Success: Update Progress & Transition
          context.total_bytes_rx += context.current_chunk_length;
          send_char(ACK);

          if (context.total_bytes_rx >= context.total_length) {
            context.state = DONE;
          }else {
            context.state = RX_CHUNK_LENGTH; // Ready for next chunk
          }
        } break;
      }
      case ERROR:{
        // Notify host of failure and return to recovery target
        send_char(NAK);
        context.byte_count = 0;
        context.state = context.error_target;  
        break;
      }
      case DONE:{
        send_char(ACK); // Final verification ACK sent 

        // Wait for Transmission Complete flag
        while((USART2->SR & USART_SR_TC) == 0){ }

        jump_to_application();
        break;
      }
    }
  }
}

// Converts up to 4 buffer bytes into a little-endian word, padding remaining bytes with 0xFF
static uint32_t bytes_to_word(const uint8_t *buf, uint32_t remaining_bytes) {
  uint8_t temp[4] = {0xFF, 0xFF, 0xFF, 0xFF};
  uint32_t count = (remaining_bytes < 4) ? remaining_bytes : 4;
  
  for (uint32_t i = 0; i < count; i++) {
    temp[i] = buf[i];
  }

  return ((uint32_t)temp[0])       |
         ((uint32_t)temp[1] << 8)  |
         ((uint32_t)temp[2] << 16) |
         ((uint32_t)temp[3] << 24);
}

// Computes hardware CRC over active chunk buffer and checks against received CRC
static bool verify_chunk_crc(Boot_context *context) {
  CRC->CR = CRC_CR_RESET; // Reset CRC peripheral state

  for(uint32_t i = 0; i < context->current_chunk_length; i += 4) {
    uint32_t remaining = context->current_chunk_length - i;
    CRC->DR = bytes_to_word(&context->chunk_buffer[i], remaining);
  }
 
 context->calculated_CRC = CRC->DR;
 return (context->calculated_CRC == context->received_CRC);
}

// Programs chunk buffer contents into target Flash word by word
static bool commit_chunk_to_flash(Boot_context *context) {
  uint32_t base_addr = FLASH_ADDR + context->total_bytes_rx;

  for(uint32_t i = 0; i < context->current_chunk_length; i += 4) {
    uint32_t remaining = context->current_chunk_length - i;
    uint32_t word = bytes_to_word(&context->chunk_buffer[i], remaining);
    if (program_word(base_addr + i, word) != 1) {
      return false; // Hardware write failure
    }
  }
  return true; // All words programmed successfully
}

/* Standard 115200 Baud Rate setting for USART2 running on 16MHz APB1 clock (OVER16=0)
 USARTDIV = 16,000,000 / (16 * 115200) = 8.6805
 DIV_Mantissa = 8 (0x08), DIV_Fraction = 0.6805 * 16 = 10.88 -> 11 (0x0B)
 BRR = (8 << 4) | 11 = 0x008B 
*/
#define USART2_BRR_115200_16MHZ   0x008BU

void UART_Config(void) {
  // Enable AHB1 and APB1 Peripheral Clocks for GPIOA and USART2 
  RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
  RCC->APB1ENR |= RCC_APB1ENR_USART2EN;

  // Configure PA2 (TX) and PA3 (RX) for Alternate Function Mode (AF07) 
  GPIOA->MODER &= ~(GPIO_MODER_MODER2_Msk | GPIO_MODER_MODER3_Msk);
  GPIOA->MODER |=  (GPIO_MODER_MODER2_1   | GPIO_MODER_MODER3_1);

  // Map PA2 and PA3 to Alternate Function 7 (USART2) using AFR High/Low registers 
  GPIOA->AFR[0] &= ~(GPIO_AFRL_AFSEL2_Msk | GPIO_AFRL_AFSEL3_Msk);
  GPIOA->AFR[0] |=  ((7U << GPIO_AFRL_AFSEL2_Pos) | (7U << GPIO_AFRL_AFSEL3_Pos));

  // Configure USART2 Control Registers 
  USART2->CR1 &= ~USART_CR1_M;     // 8 Data Bits (0 = 1 Start bit, 8 Data bits, n Stop bit)
  USART2->CR2 &= ~USART_CR2_STOP;  // 1 Stop Bit (00 = 1 Stop bit)
  USART2->BRR  = USART2_BRR_115200_16MHZ;

  // Enable Transmitter, Receiver, and USART Peripheral 
  USART2->CR1 |= (USART_CR1_TE | USART_CR1_RE | USART_CR1_UE);
}

// Blocking byte transmit
static void send_char (uint8_t c){
  while((USART2->SR & USART_SR_TXE) == 0){ }
  USART2->DR = c;
}

// Blocking byte receive with timeout reset
static uint8_t receive_char (void){
  uint32_t start = ms_ticks;

  while((USART2->SR & USART_SR_RXNE) == 0){ 
    if ((ms_ticks - start) >= UART_RX_TIMEOUT_MS) {
      NVIC_SystemReset(); // Timed out, Force a system reset 
    }
  }
  return (uint8_t) USART2->DR;
}

typedef void (*ResetHandler_t)(void);

void jump_to_application(void) {
  // Disable all global interrupts so no active vectors fire during the jump
  __disable_irq();

  // Disable USART2 and clear its configuration
  USART2->CR1 &= ~USART_CR1_UE;
  
  // Disable SysTick timer and clear pending state
  SysTick->CTRL = 0;
  SysTick->LOAD = 0;
  SysTick->VAL  = 0;

  // Clear peripheral interrupt pending bits in the NVIC
  for(uint8_t i = 0; i < 8; i++) {
    NVIC->ICER[i] = 0xFFFFFFFF; // Clear Enable
    NVIC->ICPR[i] = 0xFFFFFFFF; // Clear Pending
  }

  // Relocate the Vector Table offset to the application start
  SCB->VTOR = (uint32_t)FLASH_ADDR;

  // Fetch main stack pointer (MSP) and reset handler entry address
  uint32_t app_msp = *(volatile uint32_t *)FLASH_ADDR;
  ResetHandler_t app_reset_handler = (ResetHandler_t)(*(volatile uint32_t *)(FLASH_ADDR + 4));

 if((app_msp >= SRAM_START) && (app_msp <= SRAM_END)) {
   // Set the Main Stack Pointer
   __set_MSP(app_msp);

   // Re-enable interrupts before entering the application
   __enable_irq();

   // Branch to the application's Reset_Handler
   app_reset_handler();
  }else {
   // Invalid vector table: Force a system reset
   NVIC_SystemReset();
  }
}

