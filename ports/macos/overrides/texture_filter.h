#ifndef BLACK_TEXTURE_FILTER_H
#define BLACK_TEXTURE_FILTER_H
#include <stdint.h>
#include <stdlib.h>

/* Anisotropic filtering: RECOMP_AF raises every texture's level to at least
 * 2, 4, 8 or 16; unset or 0 keeps what the title asks NV2A for. Never lower
 * than the title's own level, never past the 16 GL guarantees. */
static inline uint32_t black_anisotropy_for(uint32_t game, const char *setting)
{
    long user = setting ? strtol(setting, NULL, 10) : 0;
    uint32_t level = game > (uint32_t)(user > 0 ? user : 0) ? game : (uint32_t)user;
    if (level < 1) level = 1;
    return level > 16 ? 16 : level;
}

static inline uint32_t black_anisotropy(uint32_t game)
{
    static const char *setting;
    static int read;
    if (!read) { setting = getenv("RECOMP_AF"); read = 1; }
    return black_anisotropy_for(game, setting);
}
#endif
