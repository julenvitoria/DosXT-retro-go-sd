/*
  vga_hle: EGA/VGA graphics for the XTulator Game & Watch port. GPLv2 or later.

  The CGA card stays for text and CGA modes. On top of it this module adds the EGA/VGA
  graphics hardware (sequencer, graphics controller, attribute controller, DAC, planar
  memory at A0000h) and the INT 10h services of an EGA/VGA BIOS, written in C because a
  free VGA BIOS that runs on an 8088/80186 is not available.

  Video memory is 4 planes of vga_plane_size bytes, interleaved (plane p, address a ->
  vga_vram[a * 4 + p]), so chain-4 (mode 13h) is plain linear memory and the four latches
  are one 32-bit word. With 16 KB planes (64 KB total) modes 0Dh (320x200x16), 0Eh
  (640x200x16), 13h (320x200x256) and unchained 320x200 "mode X" fit; 10h and 12h need
  larger planes and are refused otherwise.
*/
#ifndef _VGA_HLE_H_
#define _VGA_HLE_H_

#include <stdint.h>
#include "cpu/cpu.h"

extern int vga_active;               /* an EGA/VGA graphics mode is in effect */
extern uint8_t vga_mode;             /* its BIOS mode number */
extern volatile uint8_t vga_pal_dirty; /* DAC / attribute palette changed since last read */
extern uint32_t vga_plane_size;

/* allocate video memory from xt_pool and register ports / memory. 0 = ok */
int vga_hle_init(uint32_t plane_size);
/* INT 10h, called before the guest's vector: 1 = handled */
int vga_hle_int10(CPU_t* cpu);
/* a read of the input status register (3DAh) resets the attribute flip-flop */
void vga_hle_status_read(void);

/* 1 if the current mode uses 256 colours (else 16) */
int vga_hle_is256(void);
/* draw the screen into 200 rows of a 320-wide 8-bit framebuffer. 16-colour modes use
   indices base16..base16+15, 256-colour modes use the DAC index directly */
void vga_hle_render(uint8_t* fb, uint8_t base16);
/* current colours as 0x00RRGGBB: 16 entries (attribute palette through the DAC) or 256 (DAC) */
void vga_hle_palette16(uint32_t* out);
void vga_hle_palette256(uint32_t* out);

uint32_t vga_hle_frame_key(void);
#endif
