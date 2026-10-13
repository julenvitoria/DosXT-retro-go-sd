/*
  xt_pool: static, malloc-free allocator for the guest PC's RAM/ROM buffers.
  Part of the XTulator Game & Watch port. GPLv2 or later (same as XTulator).

  The pool is a list of up to XT_POOL_MAX_REGIONS memory regions. Allocation is
  bump-only (nothing is ever freed; a reset calls xt_pool_reset()). On the host the
  default region is a static array; on the Game & Watch the frontend registers the
  regions it owns (e.g. the end of RAM_EMU and a part of RAM_UC freed by an 8-bit
  framebuffer) before machine_init().
*/
#ifndef _XT_POOL_H_
#define _XT_POOL_H_

#include <stdint.h>
#include <stddef.h>

#ifndef XT_POOL_MAX_REGIONS
#define XT_POOL_MAX_REGIONS 4
#endif

/* Size of the built-in region. 0 = none (the frontend must call xt_pool_add). */
#ifndef XT_POOL_DEFAULT_SIZE
#define XT_POOL_DEFAULT_SIZE (640 * 1024 + 16 * 1024)
#endif

int   xt_pool_add(void* base, size_t size);   /* 0 ok, -1 table full */
void* xt_pool_alloc(size_t size);              /* 4-byte aligned, NULL if no region has room */
void  xt_pool_reset(void);                     /* forget all allocations (keeps regions) */
size_t xt_pool_free_bytes(void);

/* Optional hook: return a read-only image of a ROM file (e.g. a const array in flash,
   executed/read in place) or NULL to fall back to loading it into pool memory.
   Weak default returns NULL. */
const uint8_t* xt_rom_lookup(const char* filename, uint32_t size);

#endif
