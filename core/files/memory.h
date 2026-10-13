#ifndef _MEMORY_H_
#define _MEMORY_H_

#include <stdint.h>

#define MEMORY_RANGE		0x100000
#define MEMORY_MASK			0x0FFFFF

/*
  G&W port: the address space is mapped in pages instead of per byte.
  Original XTulator kept five 1M-entry pointer tables (20 MB on a 32-bit MCU).
  Every mapping registered by the PC/XT machines and the CGA/VGA cards is aligned
  to at least 16 KB, so 2 KB pages are a safe granularity:
    - 2 x 512 pointers (direct read / write)   = 4 KB
    - 512 x uint8_t callback-handler index     = 0.5 KB
    - MEMORY_MAX_HANDLERS small handler records
  Ranges that are not page aligned are rejected with an error message.
*/
#define MEMORY_PAGE_SHIFT	11
#define MEMORY_PAGE_SIZE	(1u << MEMORY_PAGE_SHIFT)
#define MEMORY_PAGE_MASK	(MEMORY_PAGE_SIZE - 1u)
#define MEMORY_PAGES		(MEMORY_RANGE >> MEMORY_PAGE_SHIFT)
#define MEMORY_MAX_HANDLERS	8

void memory_mapRegister(uint32_t start, uint32_t len, uint8_t* readb, uint8_t* writeb);
void memory_mapCallbackRegister(uint32_t start, uint32_t count, uint8_t(*readb)(void*, uint32_t), void (*writeb)(void*, uint32_t, uint8_t), void* udata);
int memory_init();

/* Direct page pointers (NULL = not directly mapped). Exposed for fast paths. */
extern uint8_t* memory_pageRead[MEMORY_PAGES];
extern uint8_t* memory_pageWrite[MEMORY_PAGES];

#endif
