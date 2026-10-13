/*
  XTulator: A portable, open-source 80186 PC emulator.
  Copyright (C)2020 Mike Chambers

  G&W port: 1-byte handler index per port (see ports.h). Same external API.

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
#include "debuglog.h"
#include "cpu/cpu.h"
#include "ports.h"

typedef struct {
	uint8_t (*readb)(void* udata, uint32_t portnum);
	uint16_t (*readw)(void* udata, uint32_t portnum);
	void (*writeb)(void* udata, uint32_t portnum, uint8_t value);
	void (*writew)(void* udata, uint32_t portnum, uint16_t value);
	void* udata;
} PORT_HANDLER;

static uint8_t ports_index[PORTS_COUNT]; /* 0 = unassigned, else handler+1 */
static PORT_HANDLER ports_handlers[PORTS_MAX_HANDLERS];
static uint8_t ports_handlerCount = 0;

#define PORT_H(p) (ports_index[(p)] ? &ports_handlers[ports_index[(p)] - 1] : NULL)

void port_write(CPU_t* cpu, uint16_t portnum, uint8_t value) {
	PORT_HANDLER* h;
	(void)cpu;
	portnum &= 0x0FFF;
#ifdef DEBUG_PORTS
	if (portnum == 0x80) {
		debug_log(DEBUG_DETAIL, "Diagnostic port out: %02X\r\n", value);
	}
#endif
	h = PORT_H(portnum);
	if (h && h->writeb != NULL) {
		h->writeb(h->udata, portnum, value);
	}
}

void port_writew(CPU_t* cpu, uint16_t portnum, uint16_t value) {
	PORT_HANDLER* h;
	portnum &= 0x0FFF;
	h = PORT_H(portnum);
	if (h && h->writew != NULL) {
		h->writew(h->udata, portnum, value);
		return;
	}
	port_write(cpu, portnum, (uint8_t)value);
	port_write(cpu, portnum + 1, (uint8_t)(value >> 8));
}

uint8_t port_read(CPU_t* cpu, uint16_t portnum) {
	PORT_HANDLER* h;
	(void)cpu;
	portnum &= 0x0FFF;
	h = PORT_H(portnum);
	if (h && h->readb != NULL) {
		return h->readb(h->udata, portnum);
	}
	return 0xFF;
}

uint16_t port_readw(CPU_t* cpu, uint16_t portnum) {
	PORT_HANDLER* h;
	uint16_t ret;
	portnum &= 0x0FFF;
	h = PORT_H(portnum);
	if (h && h->readw != NULL) {
		return h->readw(h->udata, portnum);
	}
	ret = port_read(cpu, portnum);
	ret |= (uint16_t)port_read(cpu, portnum + 1) << 8;
	return ret;
}

void ports_cbRegister(uint32_t start, uint32_t count, uint8_t (*readb)(void*, uint32_t), uint16_t (*readw)(void*, uint32_t), void (*writeb)(void*, uint32_t, uint8_t), void (*writew)(void*, uint32_t, uint16_t), void* udata) {
	uint32_t i;
	uint8_t h = 0;
	for (i = 0; i < ports_handlerCount; i++) {
		PORT_HANDLER* p = &ports_handlers[i];
		if (p->readb == readb && p->readw == readw && p->writeb == writeb && p->writew == writew && p->udata == udata) {
			h = (uint8_t)(i + 1);
			break;
		}
	}
	if (!h) {
		if (ports_handlerCount >= PORTS_MAX_HANDLERS) {
			debug_log(DEBUG_ERROR, "[PORTS] too many port handlers (max %d)\r\n", PORTS_MAX_HANDLERS);
			return;
		}
		ports_handlers[ports_handlerCount].readb = readb;
		ports_handlers[ports_handlerCount].readw = readw;
		ports_handlers[ports_handlerCount].writeb = writeb;
		ports_handlers[ports_handlerCount].writew = writew;
		ports_handlers[ports_handlerCount].udata = udata;
		h = ++ports_handlerCount;
	}
	for (i = 0; i < count; i++) {
		if ((start + i) >= PORTS_COUNT) {
			break;
		}
		ports_index[start + i] = h;
	}
}

void ports_init() {
	uint32_t i;
	for (i = 0; i < PORTS_COUNT; i++) {
		ports_index[i] = 0;
	}
	ports_handlerCount = 0;
}
