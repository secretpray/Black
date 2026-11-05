/* Actual SDL hint API, production setting/rumble wrappers; no device I/O. */
#include <SDL.h>
#include "input/xinput_xbox.h"
#include "port_menu.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static pthread_mutex_t black_input_lock = PTHREAD_MUTEX_INITIALIZER;
static XBOX_VIBRATION observed;
static SDL_GameController *g_pads[XBOX_MAX_CONTROLLERS] = {(SDL_GameController *)1};
static int black_rumble_needs_zero[XBOX_MAX_CONTROLLERS], attached = 1;
static int black_rumble_raw_active[XBOX_MAX_CONTROLLERS];
static SDL_bool test_attached(SDL_GameController *pad) { (void)pad; return attached ? SDL_TRUE : SDL_FALSE; }
#define SDL_GameControllerGetAttached test_attached
static int rumble_failure;
static int black_test_rumble(SDL_GameController *pad, Uint16 left, Uint16 right, Uint32 duration)
{
    if (!pad || duration != 1000) abort();
    observed = (XBOX_VIBRATION){left, right};
    return rumble_failure ? -1 : 0;
}
#define SDL_GameControllerRumble black_test_rumble
#include BLACK_CONTROLLER_BACKEND_SOURCE
int main(void)
{
    char directory[] = "/tmp/black-input-backend-XXXXXX", path[512];
    if (!mkdtemp(directory)) return 1;
    snprintf(path, sizeof path, "%s/settings.ini", directory);
    setenv("BLACK_SETTINGS_FILE", path, 1);
    if (!black_SettingsSetDualSenseHid(0)) return 2;
    if (!black_InputSetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5, "1") ||
        strcmp(SDL_GetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5), "0")) return 3;
    if (!black_SettingsSetDualSenseHid(1) || !black_InputSetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5, "0") ||
        strcmp(SDL_GetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5), "1")) return 4;
    if (!black_InputSetHint("BLACK_TEST_HINT", "untouched") || strcmp(SDL_GetHint("BLACK_TEST_HINT"), "untouched")) return 5;
    XBOX_VIBRATION request = {1234, 5678};
    if (xbox_InputSetState(0, &request) || observed.wLeftMotorSpeed != 1234 || observed.wRightMotorSpeed != 5678) return 6;
    if (!black_SettingsSetVibration(0) || xbox_InputSetState(0, &request) ||
        observed.wLeftMotorSpeed || observed.wRightMotorSpeed) return 7;
    if (request.wLeftMotorSpeed != 1234 || request.wRightMotorSpeed != 5678) return 8;
    if (xbox_InputSetState(0, NULL) != ERROR_DEVICE_NOT_CONNECTED) return 9;
    if (!black_SettingsSetVibration(1) || xbox_InputSetState(0, &request) || observed.wRightMotorSpeed != 5678) return 10;
    if (xbox_InputSetState(3, &request) != ERROR_DEVICE_NOT_CONNECTED) return 11;
    request.wLeftMotorSpeed = 40000; request.wRightMotorSpeed = 12000;
    if (!black_SettingsSetRumbleIntensity(200) || xbox_InputSetState(0, &request) ||
        observed.wLeftMotorSpeed != 65535 || observed.wRightMotorSpeed != 24000) return 12;
    if (request.wLeftMotorSpeed != 40000 || request.wRightMotorSpeed != 12000) return 13;
    if (!black_SettingsSetRumbleIntensity(0) || xbox_InputSetState(0, &request) ||
        observed.wLeftMotorSpeed || observed.wRightMotorSpeed) return 14;
    if (black_SettingsSetRumbleIntensity(-1) || black_SettingsSetRumbleIntensity(201)) return 15;
    black_SettingsSetRumbleIntensity(100); black_InputSetKeyboardActive(1);
    if (xbox_InputSetState(0, &request) || observed.wLeftMotorSpeed || observed.wRightMotorSpeed) return 16;
    black_InputSetKeyboardActive(0);
    rumble_failure = 1;
    SDL_SetError("test rumble backend failure");
    if (xbox_InputSetState(0, &request) != ERROR_NOT_SUPPORTED) return 17;
    rumble_failure = 0;
    black_rumble_needs_zero[0] = 1;
    if (xbox_InputSetState(0, &request) || observed.wLeftMotorSpeed || observed.wRightMotorSpeed) return 18;
    black_InputSetKeyboardActive(1);
    xbox_InputSetState(0, &request); /* scaled zero must not rearm */
    black_InputSetKeyboardActive(0);
    if (!black_rumble_needs_zero[0]) return 19;
    XBOX_VIBRATION zero = {0};
    if (xbox_InputSetState(0, &zero) || black_rumble_needs_zero[0] ||
        xbox_InputSetState(0, &request) || observed.wRightMotorSpeed != 12000) return 20;
    attached = 0;
    if (xbox_InputSetState(0, &request) != ERROR_DEVICE_NOT_CONNECTED) return 21;
    unlink(path); rmdir(directory);
    puts("Production SDL hint selection and rumble gating: PASS (synthetic device sink)");
    return 0;
}
