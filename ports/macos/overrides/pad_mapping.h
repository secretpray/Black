#ifndef BLACK_PAD_MAPPING_H
#define BLACK_PAD_MAPPING_H
#include "input_profile.h"
#include "input/xinput_xbox.h"
#include <string.h>
static const uint16_t black_pad_bits[8] = {XBOX_GAMEPAD_DPAD_UP, XBOX_GAMEPAD_DPAD_DOWN,
    XBOX_GAMEPAD_DPAD_LEFT, XBOX_GAMEPAD_DPAD_RIGHT, XBOX_GAMEPAD_START, XBOX_GAMEPAD_BACK,
    XBOX_GAMEPAD_LEFT_THUMB, XBOX_GAMEPAD_RIGHT_THUMB};
static inline unsigned black_pad_value(const XBOX_GAMEPAD *pad, int button)
{ return button < 0 || button >= 16 ? 0 : button < 8 ? pad->bAnalogButtons[button] : (pad->wButtons & black_pad_bits[button - 8]) ? 255 : 0; }
static inline uint16_t black_pad_pressed(const XBOX_GAMEPAD *pad)
{ uint16_t mask = 0; for (int i = 0; i < 16; ++i) if (black_pad_value(pad,i) > 30) mask |= 1u << i; return mask; }
static inline void black_pad_remap(XBOX_GAMEPAD *pad)
{
    XBOX_GAMEPAD original = *pad;
    pad->wButtons = 0; memset(pad->bAnalogButtons,0,8);
    for (int target = 0; target < 16; ++target) {
        unsigned value = black_pad_value(&original,black_SettingsPadBinding(target));
        if (target < 8) pad->bAnalogButtons[target] = value;
        else if (value > 30) pad->wButtons |= black_pad_bits[target - 8];
    }
}
#endif
