#include "../../ports/macos/overrides/port_menu.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
void black_StickLook(int, float, float *, float *);
static int check(int aim, float zoom, float want_x, float want_y)
{
    float x[2] = {12, 999}, y[2] = {-6, 888};
    black_StickLook(aim, zoom, x, y);
    return fabsf(x[0] - want_x) > .0001f || fabsf(y[0] - want_y) > .0001f || x[1] != 999 || y[1] != 888;
}
int main(void)
{
    char path[] = "/tmp/black-controller-look-XXXXXX";
    int fd = mkstemp(path); if (fd < 0) return 1; close(fd);
    setenv("BLACK_SETTINGS_FILE", path, 1);
    if (check(0, 1, 12, -6) || check(1, .5f, 12, -6)) return 2;
    for (int i = 0; i < 4; ++i)
        if (!black_SettingsSetLookSpeed(i, (i + 1) * 25)) return 3;
    if (check(0, 1, 6, -6) || check(1, 1, 6, -6) || check(1, .5f, 18, -12) ||
        check(0, .5f, 6, -6)) return 4; /* aim released while zoom eases out */
    if (!black_SettingsSetInvertX(1) || check(1, .5f, 18, -12)) return 5;
    black_InputSetKeyboardActive(1);
    if (check(1, .5f, 12, -6)) return 6;
    black_InputSetKeyboardActive(0);
    if (black_SettingsSetLookSpeed(4, 50) || black_SettingsSetLookSpeed(0, 0) ||
        black_SettingsSetLookSpeed(0, 201) || !black_SettingsResetInput() || check(1, .5f, 12, -6)) return 7;
    unlink(path);
    puts("TGP native-C look: original response, independent XY/ADS, aim release, no second inversion, keyboard bypass: PASS");
    return 0;
}
