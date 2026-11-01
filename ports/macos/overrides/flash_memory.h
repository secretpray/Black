#ifndef BLACK_FLASH_MEMORY_H
#define BLACK_FLASH_MEMORY_H
#include <stdint.h>
#include <stddef.h>
/* The pinned runtime maps a private 64 MiB contiguous window (kernel.h).
 * Preserve its high guest address; it is not an alias of the XBE/low heap. */
static inline int black_flash_memory_ok(uint32_t at,size_t size,size_t ram)
{
    uint64_t end=(uint64_t)at+size;
    return (at>=0x10000&&size<=ram&&end<=ram)||
        (at>=0x80000000u&&size<=0x4000000u&&end<=0x84000000ull);
}
#endif
