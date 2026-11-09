/* Developer diagnostic, from portingpete's RECOMP_BLACK_TEST_UNIT: start the
 * loaded mission at an explicit streaming unit so a checkpoint near that part
 * can be reached without playing the earlier ones. It writes only the unit
 * field the mission's own streaming table bounds; off unless the launcher sets
 * the variable. Not exposed in the menu and never used by ordinary runs. */
#include <fenv.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__x86_64__)
#include <xmmintrin.h>
#endif

extern ptrdiff_t g_xbox_mem_offset;

#ifndef BLACK_TEST_UNIT_RAM_SIZE
#define BLACK_TEST_UNIT_RAM_SIZE 0x04000000u /* the Xbox's 64 MB address space */
#endif
/* The mission core is allocated at MmAllocateContiguousMemory addresses such
 * as 0x804FD180; the runtime maps that window at the same offset as low RAM. */
#ifndef BLACK_TEST_UNIT_WINDOW_BASE
#define BLACK_TEST_UNIT_WINDOW_BASE 0x80000000u
#endif
#ifndef BLACK_TEST_UNIT_WINDOW_SIZE
#define BLACK_TEST_UNIT_WINDOW_SIZE 0x04000000u
#endif

static uint32_t test_unit_read(uint32_t address)
{
    uint32_t value;
    memcpy(&value, (const uint8_t *)(uintptr_t)g_xbox_mem_offset + address, 4);
    return value;
}

static void test_unit_write(uint32_t address, uint32_t value)
{
    memcpy((uint8_t *)(uintptr_t)g_xbox_mem_offset + address, &value, 4);
}

static int test_unit_span(uint32_t address, uint32_t bytes)
{
    const uint64_t end = (uint64_t)address + bytes;
    return ((uint64_t)address >= 0x10000u && end <= BLACK_TEST_UNIT_RAM_SIZE) ||
        ((uint64_t)address >= BLACK_TEST_UNIT_WINDOW_BASE &&
         end <= (uint64_t)BLACK_TEST_UNIT_WINDOW_BASE + BLACK_TEST_UNIT_WINDOW_SIZE);
}

static int test_unit_valid(uint32_t core, unsigned unit)
{
    const uint8_t *memory = (const uint8_t *)(uintptr_t)g_xbox_mem_offset;
    uint32_t header;
    unsigned count;
    if (!test_unit_span(0x2D45D4u, 4u) || core != test_unit_read(0x2D45D4u) ||
        !test_unit_span(core, 0x5AF8u)) return 0;
    /* The loaded mission sets its own level and stage; 2 is the title screen. */
    if (memory[core + 0x5AECu] > 8u || memory[core + 0x5AECu] == 2u ||
        memory[core + 0x5AEDu] != 1u) return 0;
    header = test_unit_read(core + 0x5AF4u);
    if (!test_unit_span(header, 0x13u)) return 0;
    count = memory[header + 0x11u];
    return count && count <= 127u && unit && unit <= count;
}

static unsigned test_unit_count(uint32_t core)
{
    const uint8_t *memory = (const uint8_t *)(uintptr_t)g_xbox_mem_offset;
    uint32_t header;
    if (!test_unit_span(core, 0x5AF8u)) return 0;
    header = test_unit_read(core + 0x5AF4u);
    if (!test_unit_span(header, 0x13u)) return 0;
    return memory[header + 0x11u];
}

void black_TestStreamUnit(uint32_t core)
{
    static int initialized;
    static unsigned unit, records;
    fenv_t environment;
#if defined(__x86_64__)
    unsigned csr = _mm_getcsr();
#endif
    fegetenv(&environment);
    if (!initialized) {
        const char *value = getenv("RECOMP_BLACK_TEST_UNIT");
        unsigned parsed = 0;
        initialized = 1;
        if (value && *value) {
            const char *p = value;
            while (*p >= '0' && *p <= '9' && parsed <= 127u)
                parsed = parsed * 10u + (unsigned)(*p++ - '0');
            if (!*p && parsed && parsed <= 127u) unit = parsed;
        }
        if (unit) fprintf(stderr, "[BLACK_TEST_UNIT] requested unit %u (developer start)\n", unit);
    }
    if (unit) {
        const int valid = test_unit_valid(core, unit);
        /* Native streaming names its file StUnit%02d.bin from this field. */
        if (valid) test_unit_write(core + 0x5AF0u, unit);
        if (records++ < 8u) {
            const uint8_t *memory = (const uint8_t *)(uintptr_t)g_xbox_mem_offset;
            unsigned level = 0, stage = 0, live = 0;
            if (test_unit_span(core + 0x5AECu, 2u)) {
                level = memory[core + 0x5AECu];
                stage = memory[core + 0x5AEDu];
            }
            if (test_unit_span(0x2F3BF8u, 1u)) live = memory[0x2F3BF8u];
            fprintf(stderr, "[BLACK_TEST_UNIT] core=%08X level=%u stage=%u unit=%u of=%u accepted=%d checkpoint_live=%u native_streaming=1\n",
                    core, level, stage, unit, test_unit_count(core), valid, live);
        }
    }
    fesetenv(&environment);
#if defined(__x86_64__)
    _mm_setcsr(csr);
#endif
}
