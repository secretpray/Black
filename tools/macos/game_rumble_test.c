/* Normal production XPP entry -> real input policy -> synthetic SDL sink. */
#include <SDL.h>
#include "input/xinput_xbox.h"
#include "port_menu.h"
#include "flash_memory.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static pthread_mutex_t black_input_lock = PTHREAD_MUTEX_INITIALIZER;
static SDL_GameController *g_pads[XBOX_MAX_CONTROLLERS] = {(SDL_GameController *)1};
static XBOX_VIBRATION observed;
static int calls, fail_host;
static int test_rumble(SDL_GameController *pad, Uint16 left, Uint16 right, Uint32 ms)
{
    if (!pad || ms != 1000) abort();
    observed = (XBOX_VIBRATION){left, right}; ++calls;
    return fail_host ? -1 : 0;
}
#define SDL_GameControllerRumble test_rumble
#include BLACK_CONTROLLER_BACKEND_SOURCE
static union { uint32_t words[0x400000 / 4]; uint8_t bytes[0x400000]; } ram;
static size_t g_xbox_total_ram = sizeof ram;
static uint32_t g_eax, g_esp, event_token;
#define MEM32(at) ram.words[(at) / 4]
#define MEM16(at) (*(uint16_t *)(ram.bytes + (at)))
#define MEM8(at) ram.bytes[(at)]
static int input_calls, menu_calls, pause_calls;
static void black_OriginalInputState(void) { ++input_calls; g_esp += 12; g_eax = 0; }
static void black_OriginalVibrationMenu(void)
{ ++menu_calls; MEM8(0x20000 + 0x20168) ^= 1; g_esp += 4; g_eax = 0x1234; }
static void black_OriginalVibrationPause(void)
{ ++pause_calls; MEM8(0x20000 + 0x20168) ^= 1; g_esp += 4; g_eax = 0x5678; }
static void black_InputFeedbackEvent(uint32_t token)
{
    if (MEM32(0x11000) != 0) abort(); /* Publish completion before signaling. */
    event_token = token;
}
#ifdef BLACK_RUMBLE_BASELINE
static uint32_t xin_arg(int i) { return MEM32(g_esp + 4 + i * 4); }
static void xin_ret(uint32_t n, uint32_t value) { g_eax = value; g_esp += 4 + n * 4; }
#include BLACK_RUMBLE_BASELINE
#else
#include "game_rumble.inc"
#endif
static void invoke(uint32_t handle, uint32_t feedback)
{
    g_esp = 0x10000; MEM32(g_esp) = 0xABCDEF;
    MEM32(g_esp + 4) = handle; MEM32(g_esp + 8) = feedback;
    sub_0027DE6B();
    if (g_esp != 0x1000C || MEM32(0x10000) != 0xABCDEF ||
        MEM32(0x10004) != handle || MEM32(0x10008) != feedback) abort();
}
#define REQUIRE(condition) do { if (!(condition)) { \
    fprintf(stderr, "Native rumble regression line %d: %s\n", __LINE__, #condition); return 1; \
} } while (0)
int main(void)
{
    char directory[] = "/tmp/black-game-rumble-XXXXXX", path[512];
    if (!mkdtemp(directory)) return 1;
    snprintf(path, sizeof path, "%s/settings.ini", directory);
    setenv("BLACK_SETTINGS_FILE", path, 1);
    black_SettingsSetVibration(1); black_SettingsSetRumbleIntensity(100);
    black_InputSetKeyboardActive(0);
    memset(ram.bytes + 0x11000, 0xA5, 72);
    MEM32(0x11004) = 0x48000003;
    MEM16(0x11042) = 0x1234; MEM16(0x11044) = 0xABCD;
    unsigned char before[72]; memcpy(before, ram.bytes + 0x11000, 72);
    invoke(0xD0DE0000, 0x11000);
    REQUIRE(calls == 1 && observed.wLeftMotorSpeed == 0x1234 && observed.wRightMotorSpeed == 0xABCD);
    REQUIRE(g_eax == 997 && MEM32(0x11000) == 0 && event_token == 0x48000003);
    REQUIRE(!memcmp(before + 4, ram.bytes + 0x11004, 68));
    black_SettingsSetRumbleIntensity(50);
    invoke(0xD0DE0000, 0x11000);
    REQUIRE(calls == 2 && observed.wLeftMotorSpeed == 0x1234 / 2 && observed.wRightMotorSpeed == 0xABCD / 2);
    black_SettingsSetVibration(0); invoke(0xD0DE0000, 0x11000);
    REQUIRE(calls == 3 && !observed.wLeftMotorSpeed && !observed.wRightMotorSpeed);
    black_SettingsSetVibration(1); black_InputSetKeyboardActive(1);
    invoke(0xD0DE0000, 0x11000);
    REQUIRE(calls == 4 && !observed.wLeftMotorSpeed && !observed.wRightMotorSpeed);
    black_InputSetKeyboardActive(0);
    MEM32(0x11004) = 0; event_token = 0;
    MEM16(0x11042) = MEM16(0x11044) = 0;
    invoke(0xD0DE0000, 0x11000);
    REQUIRE(calls == 5 && !observed.wLeftMotorSpeed && !observed.wRightMotorSpeed && !event_token);
    invoke(0, 0x11000); REQUIRE(g_eax == 1167 && calls == 5);
    invoke(0xD0DE0000, 0); REQUIRE(g_eax == 87 && calls == 5);
    invoke(0xD0DE0000, sizeof ram - 68); REQUIRE(g_eax == 87 && calls == 5);
    invoke(0xD0DE0000, 0xFFFFFFF0); REQUIRE(g_eax == 87 && calls == 5);
    fail_host = 1; invoke(0xD0DE0000, 0x11000);
    REQUIRE(calls == 6 && g_eax == 997 && MEM32(0x11000) == 0);
    g_pads[0] = NULL; invoke(0xD0DE0000, 0x11000);
    REQUIRE(calls == 6 && g_eax == 997 && MEM32(0x11000) == 0);
#ifndef BLACK_RUMBLE_BASELINE
    black_GameVibrationSync(); /* Objects not yet constructed. */
    MEM32(0x2D45F0) = 0x20000; MEM32(0x2D45E8) = 0x50000;
    black_SettingsSetVibration(1); g_esp = 0x10000;
    sub_0027DDF8();
    REQUIRE(input_calls == 1 && g_esp == 0x1000C && MEM8(0x40168) == 1 && MEM8(0x50ACC) == 1);
    MEM8(0x40168) = MEM8(0x50ACC) = 0; /* Profile load overrides guest flags. */
    black_GameVibrationSync();
    REQUIRE(MEM8(0x40168) == 1 && MEM8(0x50ACC) == 1);
    black_SettingsSetVibration(0); black_GameVibrationSync();
    REQUIRE(!MEM8(0x40168) && !MEM8(0x50ACC));
    g_esp = 0x10000; sub_00119C00();
    REQUIRE(menu_calls == 1 && g_esp == 0x10004 && g_eax == 0x1234 && black_SettingsVibration());
    black_GameVibrationSync(); REQUIRE(MEM8(0x50ACC) == 1);
    g_esp = 0x10000; sub_00119DD0();
    REQUIRE(pause_calls == 1 && g_esp == 0x10004 && g_eax == 0x5678 && !black_SettingsVibration());
    black_GameVibrationSync(); REQUIRE(!MEM8(0x40168) && !MEM8(0x50ACC));
#endif
    unlink(path); rmdir(directory);
    puts("Native XInputSetState -> settings -> SDL: motors/header, completion/event, stdcall, gates, bounds: PASS (synthetic)");
    return 0;
}
