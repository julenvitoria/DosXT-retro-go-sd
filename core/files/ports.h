#ifndef _PORTS_H_
#define _PORTS_H_

#include <stdint.h>

#define PORTS_COUNT 0x1000

/*
  G&W port: instead of five 4096-entry pointer tables (80 KB on a 32-bit MCU) each
  port stores a 1-byte index into a small table of handler records (4 KB + records).
*/
#define PORTS_MAX_HANDLERS 32

void ports_cbRegister(uint32_t start, uint32_t count, uint8_t(*readb)(void*, uint32_t), uint16_t(*readw)(void*, uint32_t), void (*writeb)(void*, uint32_t, uint8_t), void (*writew)(void*, uint32_t, uint16_t), void* udata);
void ports_init();

#endif
