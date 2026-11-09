/* Preserve the pinned SDL implementation and add keyboard/mouse alongside
 * a connected controller. Upstream returns the controller state before ever
 * polling the keyboard in that case. The include retains upstream's license.
 * This translation unit replaces, rather than duplicates, xinput_device.c. */
#include <stdatomic.h>
#include <SDL.h>
#include "../runtime/src/input/xinput_xbox.h"
#include <stdio.h>
#include <string.h>
#include "port_menu.h"
#include "game_checkpoint.h"
#include "controller_monitor.h"
#include "flash_menu.h"
#include "input_profile.h"
static int black_rumble_needs_zero[XBOX_MAX_CONTROLLERS];
static int black_rumble_raw_active[XBOX_MAX_CONTROLLERS];
static SDL_bool black_InputSetHint(const char *name, const char *value)
{
    if (!strcmp(name, SDL_HINT_JOYSTICK_HIDAPI_PS5)) {
        value = black_SettingsDualSenseHid() ? "1" : "0";
        fprintf(stderr, "[INPUT] DualSense requested backend: %s (restart to change)\n",
            *value == '1' ? "SDL raw HID" : "platform driver");
        return SDL_SetHintWithPriority(name, value, SDL_HINT_OVERRIDE);
    }
    return SDL_SetHint(name, value);
}
#define SDL_SetHint black_InputSetHint
#define xbox_InputSetState black_PinnedInputSetState
#define xbox_InputGetState black_SDLInputGetState
#include BLACK_INPUT_BACKEND_SOURCE
#undef xbox_InputGetState
#undef xbox_InputSetState
#undef SDL_SetHint
#include "controller_axes.h"
#include "menu_input.h"
#include "input_owner.h"
#include "input_profile.h"
#include "pad_mapping.h"
#include "text_pad.h"
#include "menu_repeat.h"
#include <pthread.h>

/* Unlike the pinned backend, preserve SDL's failure result for diagnostics. */
static DWORD black_SDLInputSetState(DWORD port, const XBOX_VIBRATION *vibration)
{
    if (port >= XBOX_MAX_CONTROLLERS || !vibration || !g_pads[port] ||
        !SDL_GameControllerGetAttached(g_pads[port]))
        return ERROR_DEVICE_NOT_CONNECTED;
    Uint16 left = black_rumble_needs_zero[port] ? 0 : vibration->wLeftMotorSpeed;
    Uint16 right = black_rumble_needs_zero[port] ? 0 : vibration->wRightMotorSpeed;
    int result = SDL_GameControllerRumble(g_pads[port], left, right, 1000);
    if (getenv("RECOMP_INPUT_DEBUG")) {
        static XBOX_VIBRATION previous[XBOX_MAX_CONTROLLERS];
        static int previous_gate[XBOX_MAX_CONTROLLERS];
        if (previous[port].wLeftMotorSpeed != left || previous[port].wRightMotorSpeed != right ||
            previous_gate[port] != black_rumble_needs_zero[port]) {
            fprintf(stderr, "[RUMBLE-HOST] port=%u left=%u right=%u stale_gate=%d result=%d\n",
                (unsigned)port, left, right, black_rumble_needs_zero[port], result);
            previous[port] = (XBOX_VIBRATION){left, right};
            previous_gate[port] = black_rumble_needs_zero[port];
        }
    }
    if (result < 0) {
        static unsigned failures;
        if (failures++ < 8)
            fprintf(stderr, "[RUMBLE] SDL failed on port %u: %s\n",
                (unsigned)port, SDL_GetError());
        return ERROR_NOT_SUPPORTED;
    }
    return ERROR_SUCCESS;
}

static int black_menu_input(DWORD port, XBOX_INPUT_STATE *state)
{
    if (port) {
        if (black_MenuIsOpen()) memset(&state->Gamepad, 0, sizeof state->Gamepad);
        return black_MenuIsOpen();
    }
    static BlackMenuPad navigation;
    static unsigned focus_epoch;
    static int previous_keyboard;
    static int navigation_instance = -1;
    XBOX_GAMEPAD *g = &state->Gamepad;
    unsigned down = 0;
    if ((g->wButtons & (XBOX_GAMEPAD_BACK | XBOX_GAMEPAD_START)) ==
        (XBOX_GAMEPAD_BACK | XBOX_GAMEPAD_START)) down |= BLACK_PAD_CHORD;
    if (g->bAnalogButtons[XBOX_BUTTON_A]) down |= BLACK_PAD_ACCEPT;
    if (g->bAnalogButtons[XBOX_BUTTON_B]) down |= BLACK_PAD_BACK;
    if ((g->wButtons & XBOX_GAMEPAD_DPAD_LEFT) || g->sThumbLX < -18000) down |= BLACK_PAD_LEFT;
    if ((g->wButtons & XBOX_GAMEPAD_DPAD_RIGHT) || g->sThumbLX > 18000) down |= BLACK_PAD_RIGHT;
    if ((g->wButtons & XBOX_GAMEPAD_DPAD_UP) || g->sThumbLY > 18000) down |= BLACK_PAD_UP;
    if ((g->wButtons & XBOX_GAMEPAD_DPAD_DOWN) || g->sThumbLY < -18000) down |= BLACK_PAD_DOWN;
    /* LB / RB (Xbox White / Black) step the settings tabs. */
    if (g->bAnalogButtons[XBOX_BUTTON_WHITE] > 32) down |= BLACK_PAD_PREV_TAB;
    if (g->bAnalogButtons[XBOX_BUTTON_BLACK] > 32) down |= BLACK_PAD_NEXT_TAB;
    /* Y: the selected setting back to its default. */
    if (g->bAnalogButtons[XBOX_BUTTON_Y] > 32) down |= BLACK_PAD_RESET;
    int analog = 0;
    for (int i = 0; i < 8; ++i) analog |= g->bAnalogButtons[i];
    if (g->wButtons || analog || abs(g->sThumbLX) > 8000 || abs(g->sThumbLY) > 8000 ||
        abs(g->sThumbRX) > 8000 || abs(g->sThumbRY) > 8000) down |= BLACK_PAD_HELD;
    /* Back + Y in a mission: restart from the on-disk checkpoint. The physical
     * layout, not the remap, so a rebound Y cannot fire it; the settings panel
     * uses Y alone and is excluded inside the request. */
    static int restart_combo;
    int restart = (g->wButtons & XBOX_GAMEPAD_BACK) && g->bAnalogButtons[XBOX_BUTTON_Y] > 32;
    if (restart && !restart_combo) black_CheckpointRequestRestart();
    restart_combo = restart;
    int instance = g_pads[0] && g_controller_connected[0] ?
        SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(g_pads[0])) : -1;
    if (instance != navigation_instance) {
        navigation_instance = instance;
        /* A replacement's held buttons must not confirm/toggle the menu.
         * The old device's drain must not block keyboard gameplay either. */
        navigation = (BlackMenuPad){.previous = down, .draining = instance >= 0 && down != 0};
    }
    unsigned epoch = black_InputFocusEpoch();
    int keyboard = black_InputKeyboardActive();
    if (!black_MenuIsOpen() && (epoch != focus_epoch || keyboard != previous_keyboard)) {
        /* A previous menu/device must not keep another device's gameplay
         * blocked until the old controller has returned to neutral. */
        navigation.previous = down;
        navigation.draining = 0;
    }
    if (!black_MenuIsOpen() && instance < 0) navigation.draining = 0;
    focus_epoch = epoch;
    previous_keyboard = keyboard;
    int capture;
    unsigned command = black_menu_pad(&navigation, down, black_MenuIsOpen(), &capture);
    if (command) black_MenuCommand(command);
    /* Home / Guide / PS: tap toggles the settings, a hold opens the quit
     * dialog (menu_input.h). The game never sees it. */
    static BlackHomeButton home;
    int guide = instance >= 0 && SDL_GameControllerGetButton(g_pads[0], SDL_CONTROLLER_BUTTON_GUIDE);
    unsigned home_command = black_home_button(&home, guide, black_InputFocused(), SDL_GetTicks());
    if (home_command) {
        black_MenuCommand(home_command);
        fprintf(stderr, "[HOME] %s\n", home_command == BLACK_MENU_QUIT ? "hold: quit dialog" : "tap: settings");
    }
    if (capture) memset(g, 0, sizeof *g);
    return capture;
}

static DWORD black_InputGetState(DWORD port, XBOX_INPUT_STATE *state)
{
    if (port >= XBOX_MAX_CONTROLLERS || !state) return ERROR_DEVICE_NOT_CONNECTED;
    memset(state, 0, sizeof *state);
    static BlackInputOwner owner;
    static int owner_instance = -1;
    static unsigned focus_epoch;
    unsigned epoch = black_InputFocusEpoch();
    if (epoch != focus_epoch) {
        focus_epoch = epoch;
        owner.previous_key_action = 0;
        owner.last_pad = SDL_GetTicks() - 750u;
    }
    DWORD result = black_SDLInputGetState(port, state);
    if (result == ERROR_SUCCESS && port < XBOX_MAX_CONTROLLERS &&
        g_controller_connected[port] && g_pads[port] && SDL_GameControllerGetAttached(g_pads[port])) {
        /* Replace upstream's startup averaging, personal RY=5011 seed and
         * +/-6000 right-stick cap. Preserve its buttons, SDL discovery and
         * rumble, while reading standard centered SDL axes at full range.
         * BLACK applies an axis deadzone itself; do not stack a radial one. */
        int raw[6] = {
            SDL_GameControllerGetAxis(g_pads[port], SDL_CONTROLLER_AXIS_LEFTX),
            SDL_GameControllerGetAxis(g_pads[port], SDL_CONTROLLER_AXIS_LEFTY),
            SDL_GameControllerGetAxis(g_pads[port], SDL_CONTROLLER_AXIS_RIGHTX),
            SDL_GameControllerGetAxis(g_pads[port], SDL_CONTROLLER_AXIS_RIGHTY),
            SDL_GameControllerGetAxis(g_pads[port], SDL_CONTROLLER_AXIS_TRIGGERLEFT),
            SDL_GameControllerGetAxis(g_pads[port], SDL_CONTROLLER_AXIS_TRIGGERRIGHT)
        };
        int left_dz = black_SettingsStickThreshold(0), right_dz = black_SettingsStickThreshold(1);
        state->Gamepad.sThumbLX = black_configured_axis(raw[0], left_dz);
        state->Gamepad.sThumbLY = black_configured_axis(-raw[1], left_dz);
        state->Gamepad.sThumbRX = black_configured_axis(black_invert_axis(raw[2], black_SettingsInvertX()), right_dz);
        state->Gamepad.sThumbRY = black_configured_axis(-raw[3], right_dz);
        int trigger_dz = black_SettingsTriggerDeadzone();
        state->Gamepad.bAnalogButtons[XBOX_BUTTON_LTRIGGER] = black_trigger(raw[4], trigger_dz);
        state->Gamepad.bAnalogButtons[XBOX_BUTTON_RTRIGGER] = black_trigger(raw[5], trigger_dz);
        if (!port) {
            int mapped[6] = {state->Gamepad.sThumbLX, state->Gamepad.sThumbLY,
                state->Gamepad.sThumbRX, state->Gamepad.sThumbRY,
                state->Gamepad.bAnalogButtons[XBOX_BUTTON_LTRIGGER], state->Gamepad.bAnalogButtons[XBOX_BUTTON_RTRIGGER]};
            black_ControllerPublish(SDL_GameControllerName(g_pads[0]),
                SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(g_pads[0])), raw, mapped, SDL_GetTicks());
        }
        static int last_vibration = 1;
        int vibration = black_SettingsVibration() && black_SettingsRumbleIntensity() > 0;
        if (last_vibration && !vibration)
            for (int i = 0; i < XBOX_MAX_CONTROLLERS; ++i)
                if (g_pads[i]) SDL_GameControllerRumble(g_pads[i], 0, 0, 0);
        last_vibration = vibration;
        if (getenv("RECOMP_INPUT_DEBUG")) {
            static uint32_t last_report;
            uint32_t now = SDL_GetTicks();
            if (now - last_report >= 1000) {
                last_report = now;
                fprintf(stderr, "[PAD-AXES] raw=(%d,%d,%d,%d) mapped=(%d,%d,%d,%d) dz=%d/%d triggers=%d,%d -> %u,%u trigger_dz=%d%%\n",
                    raw[0], raw[1], raw[2], raw[3], state->Gamepad.sThumbLX,
                    state->Gamepad.sThumbLY, state->Gamepad.sThumbRX,
                    state->Gamepad.sThumbRY, left_dz, right_dz, raw[4], raw[5],
                    state->Gamepad.bAnalogButtons[XBOX_BUTTON_LTRIGGER],
                    state->Gamepad.bAnalogButtons[XBOX_BUTTON_RTRIGGER], trigger_dz);
            }
        }
    }
    int connected = g_controller_connected[0] && g_pads[0] && SDL_GameControllerGetAttached(g_pads[0]);
    if (!port && !connected) {
        black_input_owner(&owner,black_SettingsInputDevice(),0,0,0,SDL_GetTicks());
        owner_instance = -1;
        black_InputTakeMenuKeyboard();
        black_ControllerPublish(NULL, -1, NULL, NULL, SDL_GetTicks());
        black_PadCapturePoll(0,-1,0);
        black_InputSetKeyboardActive(black_SettingsInputDevice() != BLACK_INPUT_GAMEPAD);
    }
    if (result != ERROR_SUCCESS) {
        black_menu_input(port, state);
        return result;
    }
    if (port != 0 || !connected) {
        if (!port) {
            int keyboard = black_SettingsInputDevice() != BLACK_INPUT_GAMEPAD;
            black_InputSetKeyboardActive(keyboard);
            if (!keyboard) memset(&state->Gamepad,0,sizeof state->Gamepad);
        }
        black_menu_input(port, state);
        return result;
    }

    XBOX_GAMEPAD keys = {0};
    xbox_KeyboardMergeState(&keys);
    int instance = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(g_pads[0]));
    if (instance != owner_instance) { memset(&owner,0,sizeof owner); owner_instance = instance; }
    int policy = black_SettingsInputDevice();
    SDL_GameControllerType type = SDL_GameControllerGetType(g_pads[0]);
    black_InputControllerFamily(type == SDL_CONTROLLER_TYPE_PS3 || type == SDL_CONTROLLER_TYPE_PS4 ||
        type == SDL_CONTROLLER_TYPE_PS5 ? BLACK_PROMPTS_PLAYSTATION : BLACK_PROMPTS_XBOX);
    XBOX_GAMEPAD *pad = &state->Gamepad;
    int pad_active = pad->wButtons || abs(pad->sThumbLX) > 8000 || abs(pad->sThumbLY) > 8000 ||
        abs(pad->sThumbRX) > 8000 || abs(pad->sThumbRY) > 8000;
    int key_action = keys.wButtons || keys.sThumbLX || keys.sThumbLY || keys.sThumbRX || keys.sThumbRY || black_GamePointerIntent();
    key_action |= black_InputTakeMenuKeyboard();
    for (unsigned i = 0; i < 8; ++i) {
        if (pad->bAnalogButtons[i] > 30) pad_active = 1;
        if (keys.bAnalogButtons[i]) key_action = 1;
    }
    unsigned selected = black_input_owner(&owner, policy, 1, pad_active, key_action, SDL_GetTicks());
    if (selected == BLACK_INPUT_KEYBOARD && !black_InputKeyboardActive())
        SDL_GameControllerRumble(g_pads[0],0,0,0);
    black_InputSetKeyboardActive(selected == BLACK_INPUT_KEYBOARD);
    XBOX_INPUT_STATE physical = *state;
    if (black_PadCapturePoll(black_pad_pressed(pad),
        SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(g_pads[0])),1)) {
        memset(&state->Gamepad,0,sizeof state->Gamepad);
        return result;
    }
    if (selected == BLACK_INPUT_KEYBOARD) *pad = keys;
    else if (!black_GameMenuActive()) black_pad_remap(pad);
    /* Mouse deltas are consumed only by the guest MouseLook hook; look keys
     * are deliberate actions, while mouse motion never steals pad ownership. */
    /* Settings always use the physical layout, even after unbinding A/B/Start.
     * The native menu never injects remapped commands into the game. */
    int capture = black_menu_input(port, &physical);
    if (capture || black_MenuIsOpen()) memset(&state->Gamepad,0,sizeof state->Gamepad);
    if (getenv("RECOMP_INPUT_DEBUG") && keys.bAnalogButtons[XBOX_BUTTON_RTRIGGER]) {
        static uint32_t last_fire;
        uint32_t now = SDL_GetTicks();
        if (now - last_fire >= 250) {
            last_fire = now;
            fprintf(stderr, "[INPUT-FIRE] keyboard=%u output=%u owner=%u capture=%d menu=%d\n",
                keys.bAnalogButtons[XBOX_BUTTON_RTRIGGER], state->Gamepad.bAnalogButtons[XBOX_BUTTON_RTRIGGER],
                selected, capture, black_MenuIsOpen());
        }
    }
    return result;
}

/* The game can stop submitting USB transfers while paused at its world
 * timer. The menu must keep reading the host independently. Serialize SDL
 * and the existing keyboard/script state with normal USB-side polling. */
static pthread_mutex_t black_input_lock = PTHREAD_MUTEX_INITIALIZER;
/* A or START as the game sees port 0 (pad, keyboard or script, after the
 * remap and with the settings panel's capture), for the film skip. */
static atomic_int black_film_button;
int black_InputFilmButton(void) { return atomic_load(&black_film_button); }
/* A short START press for the game: the quit dialog opens the pause menu
 * this way before asking the game to quit to its main menu. */
static atomic_int black_start_pulse;
void black_InputPulseStart(void) { atomic_store(&black_start_pulse, 6); }

DWORD xbox_InputGetState(DWORD port, XBOX_INPUT_STATE *state)
{
    pthread_mutex_lock(&black_input_lock);
    DWORD result = black_InputGetState(port, state);
    if (!port && result == ERROR_SUCCESS && state && !black_MenuIsOpen()) {
        int pulse = atomic_load(&black_start_pulse);
        /* Released for the last polls, so the game sees one press. */
        if (pulse > 0) {
            if (pulse > 2) state->Gamepad.wButtons |= XBOX_GAMEPAD_START;
            else state->Gamepad.wButtons &= ~XBOX_GAMEPAD_START;
            atomic_store(&black_start_pulse, pulse - 1);
        }
    }
    if (!port && result == ERROR_SUCCESS && state) {
        /* Name keyboard: X deletes, Y types a space, Start is DONE
         * (text_pad.h); the game does not see these buttons while the page
         * takes typing. */
        static BlackTextPad text_pad;
        int typing = black_GameTextActive() && !black_MenuIsOpen();
        int c = black_text_pad(&text_pad, typing, state->Gamepad.bAnalogButtons[XBOX_BUTTON_X] > 30,
            state->Gamepad.bAnalogButtons[XBOX_BUTTON_Y] > 30, !!(state->Gamepad.wButtons & XBOX_GAMEPAD_START),
            SDL_GetTicks());
        if (c) black_GameTextCharacter((unsigned)c);
        if (typing) {
            state->Gamepad.bAnalogButtons[XBOX_BUTTON_X] = state->Gamepad.bAnalogButtons[XBOX_BUTTON_Y] = 0;
            state->Gamepad.wButtons &= (WORD)~XBOX_GAMEPAD_START;
        }
        /* Held d-pad in the game's menus: steady steps (menu_repeat.h). */
        static BlackMenuRepeat menu_repeat;
        static const WORD dirs[4] = {XBOX_GAMEPAD_DPAD_UP, XBOX_GAMEPAD_DPAD_DOWN,
                                     XBOX_GAMEPAD_DPAD_LEFT, XBOX_GAMEPAD_DPAD_RIGHT};
        uint32_t held = 0;
        for (int i = 0; i < 4; ++i) if (state->Gamepad.wButtons & dirs[i]) held |= 1u << i;
        uint32_t shown = black_menu_repeat(&menu_repeat, black_GameMenuActive() && !black_MenuIsOpen(),
            held, SDL_GetTicks());
        for (int i = 0; i < 4; ++i)
            if (!(shown & (1u << i))) state->Gamepad.wButtons &= (WORD)~dirs[i];
    }
    if (!port)
        atomic_store(&black_film_button, result == ERROR_SUCCESS && state &&
            (state->Gamepad.bAnalogButtons[XBOX_BUTTON_A] > 30 ||
             (state->Gamepad.wButtons & XBOX_GAMEPAD_START)));
    pthread_mutex_unlock(&black_input_lock);
    return result;
}

DWORD xbox_InputSetState(DWORD port, const XBOX_VIBRATION *vibration)
{
    pthread_mutex_lock(&black_input_lock);
    XBOX_VIBRATION scaled = {0};
    /* Rearm from the game's envelope, not a zero produced by mute/owner
     * scaling; otherwise an old effect could resume after reconnect. */
    if (port < XBOX_MAX_CONTROLLERS && vibration) {
        black_rumble_raw_active[port] = vibration->wLeftMotorSpeed || vibration->wRightMotorSpeed;
        if (!black_rumble_raw_active[port]) black_rumble_needs_zero[port] = 0;
    }
    if (vibration && black_SettingsVibration() && (port || !black_InputKeyboardActive())) {
        unsigned percent = black_SettingsRumbleIntensity();
        unsigned left = (unsigned)vibration->wLeftMotorSpeed * percent / 100;
        unsigned right = (unsigned)vibration->wRightMotorSpeed * percent / 100;
        scaled.wLeftMotorSpeed = left > 65535 ? 65535 : left;
        scaled.wRightMotorSpeed = right > 65535 ? 65535 : right;
    }
    DWORD result = black_SDLInputSetState(port, vibration ? &scaled : NULL);
    pthread_mutex_unlock(&black_input_lock);
    return result;
}

void black_MenuPollHost(void)
{
    if (!black_MenuIsOpen()) return;
    static uint32_t previous;
    uint32_t now = SDL_GetTicks();
    if (now - previous < 8) return;
    previous = now;
    XBOX_INPUT_STATE ignored;
    /* Keep Cocoa's event pump responsive if a USB-side HID rescan is busy. */
    if (pthread_mutex_trylock(&black_input_lock) != 0) return;
    black_InputGetState(0, &ignored);
    pthread_mutex_unlock(&black_input_lock);
}
