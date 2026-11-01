#include "../../ports/macos/overrides/texture_filter.h"
int main(void)
{
    /* Unset or 0 keeps the title's level; a setting only raises it. */
    if (black_anisotropy_for(1, NULL) != 1 || black_anisotropy_for(4, "0") != 4) return 1;
    if (black_anisotropy_for(1, "8") != 8 || black_anisotropy_for(16, "2") != 16) return 2;
    if (black_anisotropy_for(1, "64") != 16 || black_anisotropy_for(0, NULL) != 1) return 3;
    if (black_anisotropy_for(2, "-4") != 2) return 4;
    return 0;
}
