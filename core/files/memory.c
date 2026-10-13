/*
  XTulator: A portable, open-source 80186 PC emulator.
  Copyright (C)2020 Mike Chambers

  G&W port: page-granular memory map (see memory.h). Same external API.

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include <stdint.h>
#include <stddef.h>
#include "config.h"
#include "cpu/cpu.h"
#include "debuglog.h"
#include "memory.h"

typedef struct {
	uint8_t (*read)(void* udata, uint32_t addr);
	void (*write)(void* udata, uint32_t addr, uint8_t value);
	void* udata;
} MEMORY_HANDLER;

uint8_t* memory_pageRead[MEMORY_PAGES];
uint8_t* memory_pageWrite[MEMORY_PAGES];
static uint8_t memory_pageHandler[MEMORY_PAGES]; /* 0 = none, else index+1 */
static MEMORY_HANDLER memory_handlers[MEMORY_MAX_HANDLERS];
static uint8_t memory_handlerCount = 0;

void cpu_write(CPU_t* cpu, uint32_t addr32, uint8_t value) {
	uint32_t page;
	uint8_t h;
	(void)cpu;
	addr32 &= MEMORY_MASK;
	page = addr32 >> MEMORY_PAGE_SHIFT;

	if (memory_pageWrite[page] != NULL) {
		memory_pageWrite[page][addr32 & MEMORY_PAGE_MASK] = value;
		return;
	}
	h = memory_pageHandler[page];
	if (h && memory_handlers[h - 1].write != NULL) {
		memory_handlers[h - 1].write(memory_handlers[h - 1].udata, addr32, value);
	}
}

uint8_t cpu_read(CPU_t* cpu, uint32_t addr32) {
	uint32_t page;
	uint8_t h;
	(void)cpu;
	addr32 &= MEMORY_MASK;
	page = addr32 >> MEMORY_PAGE_SHIFT;

	if (memory_pageRead[page] != NULL) {
		return memory_pageRead[page][addr32 & MEMORY_PAGE_MASK];
	}
	h = memory_pageHandler[page];
	if (h && memory_handlers[h - 1].read != NULL) {
		return memory_handlers[h - 1].read(memory_handlers[h - 1].udata, addr32);
	}
	return 0xFF;
}

static int memory_checkRange(const char* who, uint32_t start, uint32_t len) {
	if ((start & MEMORY_PAGE_MASK) || (len & MEMORY_PAGE_MASK)) {
		debug_log(DEBUG_ERROR, "[MEMORY] %s: range %05X+%X not aligned to %u-byte pages\r\n",
			who, start, len, MEMORY_PAGE_SIZE);
		return -1;
	}
	return 0;
}

void memory_mapRegister(uint32_t start, uint32_t len, uint8_t* readb, uint8_t* writeb) {
	uint32_t off;
	if (memory_checkRange("mapRegister", start, len)) return;
	for (off = 0; off < len; off += MEMORY_PAGE_SIZE) {
		uint32_t page = (start + off) >> MEMORY_PAGE_SHIFT;
		if ((start + off) >= MEMORY_RANGE) break;
		memory_pageRead[page] = (readb == NULL) ? NULL : readb + off;
		memory_pageWrite[page] = (writeb == NULL) ? NULL : writeb + off;
	}
}

void memory_mapCallbackRegister(uint32_t start, uint32_t count, uint8_t(*readb)(void*, uint32_t), void (*writeb)(void*, uint32_t, uint8_t), void* udata) {
	uint32_t off;
	uint8_t h = 0, i;
	if (memory_checkRange("mapCallbackRegister", start, count)) return;

	/* reuse an identical handler record if there is one */
	for (i = 0; i < memory_handlerCount; i++) {
		if (memory_handlers[i].read == readb && memory_handlers[i].write == writeb && memory_handlers[i].udata == udata) {
			h = i + 1;
			break;
		}
	}
	if (!h) {
		if (memory_handlerCount >= MEMORY_MAX_HANDLERS) {
			debug_log(DEBUG_ERROR, "[MEMORY] too many memory handlers (max %d)\r\n", MEMORY_MAX_HANDLERS);
			return;
		}
		memory_handlers[memory_handlerCount].read = readb;
		memory_handlers[memory_handlerCount].write = writeb;
		memory_handlers[memory_handlerCount].udata = udata;
		h = ++memory_handlerCount;
	}
	for (off = 0; off < count; off += MEMORY_PAGE_SIZE) {
		if ((start + off) >= MEMORY_RANGE) break;
		memory_pageHandler[(start + off) >> MEMORY_PAGE_SHIFT] = h;
	}
}

int memory_init() {
	uint32_t i;
	for (i = 0; i < MEMORY_PAGES; i++) {
		memory_pageRead[i] = NULL;
		memory_pageWrite[i] = NULL;
		memory_pageHandler[i] = 0;
	}
	memory_handlerCount = 0;
	return 0;
}
