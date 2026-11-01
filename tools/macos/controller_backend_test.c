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
static int null_request;
static DWORD black_SDLInputSetState(DWORD port, const XBOX_VIBRATION *vibration)
{
    null_request = vibration == NULL;
    if (vibration) observed = *vibration;
    return port == 0 && vibration ? ERROR_SUCCESS : ERROR_DEVICE_NOT_CONNECTED;
}
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
        observed.wLeftMotorSpeed || observed.wRightMotorSpeed || null_request) return 7;
    if (request.wLeftMotorSpeed != 1234 || request.wRightMotorSpeed != 5678) return 8;
    if (xbox_InputSetState(0, NULL) != ERROR_DEVICE_NOT_CONNECTED || !null_request) return 9;
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
    unlink(path); rmdir(directory);
    puts("Production SDL hint selection and rumble gating: PASS (synthetic device sink)");
    return 0;
}
