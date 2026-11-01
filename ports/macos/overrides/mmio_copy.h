/* REP MOVS over DSP memory is a bus transfer, not a libc SIMD copy.
 * Ordinary guest RAM keeps the original memcpy fast path. */
#pragma once
#include <string.h>
#include <stdlib.h>
#include "mcpx_mmio.h"
static inline void *black_device_memcpy(void *destination, const void *source, size_t count)
{
    uintptr_t base = (uintptr_t)g_xbox_mem_offset;
    uintptr_t src = (uintptr_t)source - base, dst = (uintptr_t)destination - base;
    int device = (src < 0xFE860000ull && src + count > 0xFE830000ull) ||
                 (dst < 0xFE860000ull && dst + count > 0xFE830000ull);
    if (!device || !getenv("RECOMP_REAL_DSP")) return memcpy(destination, source, count);
    for (size_t offset = 0; offset < count;) {
        unsigned size = count - offset >= 4 && !(((src + offset) | (dst + offset)) & 3) ? 4 : 1;
        uint64_t value;
        if (!xbox_mmio_read((uint32_t)(src + offset), size, &value))
            value = size == 4 ? *(volatile const uint32_t *)((const char *)source + offset)
                             : *((volatile const uint8_t *)source + offset);
        if (size == 4) *(volatile uint32_t *)((char *)destination + offset) = (uint32_t)value;
        else *((volatile uint8_t *)destination + offset) = (uint8_t)value;
        offset += size;
    }
    return destination;
}
#define memcpy black_device_memcpy
