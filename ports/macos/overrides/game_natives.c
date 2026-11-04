/* Device apertures need scalar bus accesses. libc's SIMD loads cannot be
 * decoded by the ARM64 MMIO fault handler (observed at GP FE830BF8). */
#define sub_000A24E0 black_OriginalMemcpy
#define sub_0027DE6B black_OriginalRumbleNoop
#define sub_0027DDF8 black_OriginalInputState
#include "../game/src/recomp_natives.c"
#undef sub_000A24E0
#undef sub_0027DE6B
#undef sub_0027DDF8
#include "mcpx_mmio.h"

static int black_dsp_overlap(uint32_t address, uint32_t count)
{
    return address < 0xFE860000u && (uint64_t)address + count > 0xFE830000u;
}

void sub_000A24E0(void)
{
    uint32_t dst = MEM32(g_esp + 4), src = MEM32(g_esp + 8), n = MEM32(g_esp + 12);
    if (!getenv("RECOMP_REAL_DSP") ||
        (!black_dsp_overlap(dst, n) && !black_dsp_overlap(src, n))) {
        black_OriginalMemcpy();
        return;
    }
    int backwards = dst > src && (uint64_t)src + n > dst;
    uint32_t remaining = n;
    while (remaining) {
        unsigned size = remaining >= 4 && !((dst | src | (backwards ? remaining : 0)) & 3) ? 4 : 1;
        uint32_t offset = backwards ? remaining - size : n - remaining;
        uint64_t value;
        if (!xbox_mmio_read(src + offset, size, &value))
            value = size == 4 ? MEM32(src + offset) : MEM8(src + offset);
        if (size == 4) MEM32(dst + offset) = (uint32_t)value;
        else MEM8(dst + offset) = (uint8_t)value;
        remaining -= size;
    }
    g_eax = dst;
    g_esp += 4;
}
