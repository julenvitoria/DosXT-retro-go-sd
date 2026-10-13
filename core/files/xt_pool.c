/*
  xt_pool: static, malloc-free allocator. See xt_pool.h.
  Part of the XTulator Game & Watch port. GPLv2 or later (same as XTulator).
*/
#include "xt_pool.h"

typedef struct {
	uint8_t* base;
	size_t size;
	size_t used;
} XT_POOL_REGION;

static XT_POOL_REGION regions[XT_POOL_MAX_REGIONS];
static int region_count = 0;

#if XT_POOL_DEFAULT_SIZE > 0
static uint8_t default_pool[XT_POOL_DEFAULT_SIZE] __attribute__((aligned(8)));
static int default_added = 0;
#endif

int xt_pool_add(void* base, size_t size) {
	if (region_count >= XT_POOL_MAX_REGIONS) return -1;
	/* align base up to 4 bytes */
	uintptr_t b = ((uintptr_t)base + 3u) & ~(uintptr_t)3u;
	size_t lost = (size_t)(b - (uintptr_t)base);
	if (size <= lost) return 0;
	regions[region_count].base = (uint8_t*)b;
	regions[region_count].size = size - lost;
	regions[region_count].used = 0;
	region_count++;
	return 0;
}

static void ensure_default(void) {
#if XT_POOL_DEFAULT_SIZE > 0
	if (!default_added) {
		default_added = 1;
		xt_pool_add(default_pool, sizeof(default_pool));
	}
#endif
}

void* xt_pool_alloc(size_t size) {
	ensure_default();
	size = (size + 3u) & ~(size_t)3u;
	for (int i = 0; i < region_count; i++) {
		if (regions[i].size - regions[i].used >= size) {
			void* p = regions[i].base + regions[i].used;
			regions[i].used += size;
			return p;
		}
	}
	return NULL;
}

void xt_pool_reset(void) {
	for (int i = 0; i < region_count; i++) regions[i].used = 0;
}

size_t xt_pool_free_bytes(void) {
	ensure_default();
	size_t f = 0;
	for (int i = 0; i < region_count; i++) f += regions[i].size - regions[i].used;
	return f;
}

__attribute__((weak)) const uint8_t* xt_rom_lookup(const char* filename, uint32_t size) {
	(void)filename; (void)size;
	return NULL;
}
