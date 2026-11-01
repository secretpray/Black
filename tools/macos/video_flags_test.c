#include "../../ports/macos/overrides/video_flags.h"
int main(void)
{
    /* XGetVideoFlags shifts the setting right by 16: bit 0 is widescreen. */
    if (((black_video_flags("16:9") >> 16) & 0x5F) != 1) return 1;
    if (black_video_flags(NULL) != BLACK_XC_VIDEO_WIDESCREEN) return 2;
    if (black_video_flags("16:10") != BLACK_XC_VIDEO_WIDESCREEN) return 3;
    if (black_video_flags("1.78") != BLACK_XC_VIDEO_WIDESCREEN) return 4;
    if (black_video_flags("4:3") != 0) return 5;
    if (black_video_flags("1.3333") != 0) return 6;
    if (black_video_flags("bogus") != BLACK_XC_VIDEO_WIDESCREEN) return 7;
    /* The pinned kernel's 0x03 reads as no flags at all through the XDK. */
    if (((0x03u >> 16) & 0x5F) != 0) return 8;
    return 0;
}
