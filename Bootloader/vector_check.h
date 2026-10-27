#ifndef VECTOR_CHECK_H
#define VECTOR_CHECK_H

#include <stdint.h>
#include <stdbool.h>

// Pure check of an app's first two vector table words, no hardware access
bool vector_table_valid(uint32_t msp, uint32_t reset);

#endif
