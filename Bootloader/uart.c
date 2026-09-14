#include <stdint.h>
#include "stm32f446xx.h"
#include <stdbool.h>
#include "main.h"

#define CHUNK_PAYLOAD_SIZE 1024 // 1KB chunk payload
#define MAX_PAYLOAD_SIZE 491520 // 480KB
#define ACK 0x06U
#define NAK 0x15U
#define FLASH_ADDR 0x08008000U

typedef enum{
  IDLE, RX_TOTAL_LENGTH, RX_CHUNK_LENGTH, RX_CHUNK_PAYLOAD, RX_CHUNK_CRC, DONE, ERROR 
}Boot_state;

typedef struct{
  Boot_state state;

  uint8_t word_buf[4]; // 4-byte assembler for program_word()
  uint8_t byte_count;  // Assembler index (0 to 3)

  uint32_t total_length;   // Upfront total image size in bytes
  uint32_t total_bytes_rx; // Cumulative bytes processed across all chunks

  uint32_t current_chunk_length; // Expected size of active chunk
  uint32_t chunk_bytes_rx;        // Bytes received for current active chunk

  uint8_t chunk_buffer[CHUNK_PAYLOAD_SIZE]; // 1KB buffer for CRC verification

  uint32_t received_CRC;   // CRC read from the wire
  uint32_t calculated_CRC; // Calcualted CRC
} Boot_context;

static Boot_context context;

void sendChar (uint8_t);
uint8_t receiveChar (void);
static inline uint32_t bytes_to_word(const uint8_t *buf);
static bool verify_chunk_crc(Boot_context *context);
static bool commit_chunk_to_flash(Boot_context *context);


/**
 * @brief Assembles incoming bytes into a 4-byte buffer.
 * @param context Pointer to the bootloader context.
 * @param byte The incoming single byte from UART.
 * @param word Pointer to a uint32_t to store the assembled result.
 * @return true (1) if 4 bytes have been fully assembled; false (0) if still assembling.
 */
bool assemble_byte(Boot_context *context, uint8_t byte, uint32_t *word){
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

void state_machine(void){ 
  while (1){
    switch(context.state){
      case IDLE:{
        while((USART2->SR & USART_SR_RXNE) == 0){ }
        context.byte_count = 0;
        context.state = RX_TOTAL_LENGTH;  
        break;
      }
      case RX_TOTAL_LENGTH:{
        // Accumulate 4 bytes for total image length
        uint8_t rx_byte = receiveChar();
        if (assemble_byte(&context, rx_byte, &context.total_length)) {
            if(context.total_length >= MAX_PAYLOAD_SIZE){
              context.state = ERROR;
            }else{
              erase_for_length(context.total_length);
              context.state = RX_CHUNK_LENGTH;
            }
        }
        break;
      }
      case RX_CHUNK_LENGTH:{
        context.chunk_bytes_rx = 0; // Reset counter
        uint8_t rx_byte = receiveChar();
        if (assemble_byte(&context, rx_byte, &context.current_chunk_length)) {
            if(context.current_chunk_length > CHUNK_PAYLOAD_SIZE || 
              context.current_chunk_length > (context.total_length - context.total_bytes_rx )){
              context.state = ERROR;
            }else{
              context.state = RX_CHUNK_PAYLOAD;
            }
        }
        break;
      }
      case RX_CHUNK_PAYLOAD:{
        uint8_t rx_byte = receiveChar();

        context.chunk_buffer[context.chunk_bytes_rx] = rx_byte;
        context.chunk_bytes_rx++;

        if(context.chunk_bytes_rx == context.current_chunk_length){
          context.state = RX_CHUNK_CRC;
        }
        break;
      }
      case RX_CHUNK_CRC:{
        uint8_t rx_byte = receiveChar();
        if (assemble_byte(&context, rx_byte, &context.received_CRC)) {

          // Verify Hardware CRC
          if(!verify_chunk_crc(&context)) {
            context.state = ERROR;
            break;
          }
          
          // Commit to Flash (Only runs if CRC matched)
          if (!commit_chunk_to_flash(&context)) {
            context.state = ERROR;
            break;
          }

          // Success: Update Progress & Transition
          context.total_bytes_rx += context.current_chunk_length;

          if (context.total_bytes_rx >= context.total_length) {
            context.state = DONE;
          }else {
            context.state = RX_CHUNK_LENGTH; // Ready for next chunk
          }
        } break;
      }
    }
  }
}

//Little-endian helper
static inline uint32_t bytes_to_word(const uint8_t *buf) {
    return ((uint32_t)buf[0])        |
           ((uint32_t)buf[1] << 8)  |
           ((uint32_t)buf[2] << 16) |
           ((uint32_t)buf[3] << 24);
}

//CRC Verification (Returns true if match, false if fail)
static bool verify_chunk_crc(Boot_context *context) {
    CRC->CR = 1; // Reset CRC peripheral state

    for (uint32_t i = 0; i < context->current_chunk_length; i += 4) {
        CRC->DR = bytes_to_word(&context->chunk_buffer[i]);
    }

    context->calculated_CRC = CRC->DR;
    return (context->calculated_CRC == context->received_CRC);
}

// Flash Programming (Returns ttrue if all words written OK, false on hardware error)
static bool commit_chunk_to_flash(Boot_context *context) {
    uint32_t base_addr = FLASH_ADDR + context->total_bytes_rx;

    for (uint32_t i = 0; i < context->current_chunk_length; i += 4) {
        uint32_t word = bytes_to_word(&context->chunk_buffer[i]);
        if (program_word(base_addr + i, word) != 1) {
            return false; // Hardware write failure
        }
    }
    return true; // All words programmed successfully
}

void UART_Config(void){
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

}

void sendChar (uint8_t c){
  while((USART2->SR & USART_SR_TXE) == 0){ }
  USART2->DR = c;
}

uint8_t receiveChar (void){
  while((USART2->SR & USART_SR_RXNE) == 0){ }
  return (uint8_t) USART2->DR;
}



