/* Native-C adaptation of TGP src/game/mouse.cpp::StickLook at 0x1BD69F.
 * Apply after BLACK's stick curve, before zoom and world dt. No raw-axis
 * scaling, extra deadzone, register ABI bridge or X86Context dependency. */
#include "port_menu.h"
void black_StickLook(int aim_wanted, float zoom, float *yaw, float *pitch)
{
    if (black_InputKeyboardActive()) return;
    int aim = aim_wanted && zoom < 1.0f;
    *yaw *= black_SettingsLookSpeed(aim ? 2 : 0) / 50.0f;
    *pitch *= black_SettingsLookSpeed(aim ? 3 : 1) / 50.0f;
}

int black_ControllerAimAssist(void)
{
    return black_SettingsAimAssist() && !black_InputKeyboardActive();
}
