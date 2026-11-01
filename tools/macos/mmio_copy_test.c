#include <stdint.h>
#include <stddef.h>
#include <sys/mman.h>
#include <unistd.h>
#undef NDEBUG
#include <assert.h>
static ptrdiff_t g_xbox_mem_offset;
#include "../../ports/macos/overrides/mmio_copy.h"
#undef memcpy

static unsigned reads;
int xbox_mmio_read(uint32_t address, unsigned size, uint64_t *value)
{
    if (address < 0xFE830000u || (uint64_t)address + size > 0xFE834000u) return 0;
    *value = 0;
    for (unsigned i = 0; i < size; ++i) *value |= (uint64_t)((address + i) & 255) << (i * 8);
    ++reads;
    return 1;
}
int main(void)
{
    const size_t bytes = 0x100000000ull;
    uint8_t *ram = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    assert(ram != MAP_FAILED);
    g_xbox_mem_offset = (ptrdiff_t)ram;
    assert(!mprotect(ram + 0xFE830000ull, 16384, PROT_NONE));
    setenv("RECOMP_REAL_DSP", "1", 1);
    /* Reproduce the exact faulting source; SIMD dereference would SIGBUS. */
    black_device_memcpy(ram + 0x10000, ram + 0xFE830BF8ull, 0x118);
    for (unsigned i = 0; i < 0x118; ++i) assert(ram[0x10000 + i] == ((0xBF8 + i) & 255));
    assert(reads == 0x118 / 4);
    reads = 0;
    black_device_memcpy(ram + 0x10003, ram + 0xFE830BF9ull, 19);
    for (unsigned i = 0; i < 19; ++i) assert(ram[0x10003 + i] == ((0xBF9 + i) & 255));
    assert(reads > 0);
    unsigned before = reads;
    black_device_memcpy(ram + 0x11000, ram + 0x10003, 19);
    assert(!memcmp(ram + 0x11000, ram + 0x10003, 19));
    assert(reads == before);
    black_device_memcpy(ram + 0x10000, ram + 0xFE830BF8ull, 0);
    assert(reads == before);
    munmap(ram, bytes);
}
