#ifndef BLACK_VIDEO_FLAGS_H
#define BLACK_VIDEO_FLAGS_H
#include <stdint.h>
#include "display_aspect.h"

/* XC_VIDEO in the XDK layout. XGetVideoFlags (0x23291 in the USA XBE) returns
 * (value >> 16) & 0x5F, and BLACK's display setup (0x178350) stores bit 0 of
 * that as its widescreen flag, [[0x2D67F4] + 0xC]. The camera (0x15A960) then
 * uses aspect 16/9 and view width 1.2 instead of 4/3 and 1. The pinned kernel
 * answers 0x03, which the XDK reads as a 4:3 set, so the presenter stretched a
 * 4:3 picture. Only widescreen is reported: the HDTV bits (0x20000 720p,
 * 0x40000 1080i, 0x80000 480p) would let the title pick another mode. */
#define BLACK_XC_VIDEO_WIDESCREEN 0x00010000u

static inline uint32_t black_video_flags(const char *aspect)
{
    return black_display_wide(black_display_aspect(aspect)) ? BLACK_XC_VIDEO_WIDESCREEN : 0;
}
#endif
