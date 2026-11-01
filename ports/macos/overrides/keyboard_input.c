/* Keep scripted/terminal diagnostics separate from rebindable Cocoa events. */
#define xbox_KeyboardMergeState black_OriginalKeyboardMergeState
#define xbox_KeyboardReleaseAll black_OriginalKeyboardReleaseAll
#define xbox_MouseReleaseAll black_OriginalMouseReleaseAll
#include "../runtime/src/input/keyboard_input.c"
#undef xbox_KeyboardMergeState
#undef xbox_KeyboardReleaseAll
#undef xbox_MouseReleaseAll
#include "keyboard_mouse.h"
int xbox_KeyboardMergeState(XBOX_GAMEPAD *pad)
{
    int active = black_OriginalKeyboardMergeState(pad);
    if (!pad || getenv("RECOMP_BACKGROUND")) return active;
    int16_t sticks[4] = {pad->sThumbLX,pad->sThumbLY,pad->sThumbRX,pad->sThumbRY};
    black_KeyboardState(black_InputNowMs(), &pad->wButtons, pad->bAnalogButtons, sticks);
    pad->sThumbLX=sticks[0]; pad->sThumbLY=sticks[1]; pad->sThumbRX=sticks[2]; pad->sThumbRY=sticks[3];
    return 1; /* Native window input can be port 0 even before its first key. */
}
void xbox_KeyboardReleaseAll(void) { black_OriginalKeyboardReleaseAll(); black_KeyReleaseAll(); }
void xbox_MouseReleaseAll(void) { black_OriginalMouseReleaseAll(); black_KeyReleaseAll(); }
