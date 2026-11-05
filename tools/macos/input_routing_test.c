/* Execute the production GetState/menu routing against deterministic SDL
 * reports. No SDL subsystem/device is opened by this test. */
#include <SDL.h>
#include "input/xinput_xbox.h"
#include "port_menu.h"
#include "controller_monitor.h"
#include "controller_axes.h"
#include "menu_input.h"
#include "input_profile.h"
#include "pad_mapping.h"
#include "flash_menu.h"
#include "keyboard_mouse.h"
#undef NDEBUG
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
static SDL_GameController *g_pads[4];
static int g_controller_connected[4], axes[6], instance=10, attached=1, stopped, no_keyboard_result;
static SDL_GameControllerType controller_type = SDL_CONTROLLER_TYPE_PS5;
static uint32_t ticks;
static XBOX_GAMEPAD physical, keyboard;
static DWORD black_SDLInputGetState(DWORD port,XBOX_INPUT_STATE *state)
{
    if(port>=4 || !state)return ERROR_DEVICE_NOT_CONNECTED;
    memset(state,0,sizeof *state);
    state->Gamepad=g_controller_connected[port]?physical:keyboard;
    return !g_controller_connected[port] && no_keyboard_result ? ERROR_DEVICE_NOT_CONNECTED : ERROR_SUCCESS;
}
static int xbox_KeyboardMergeState(XBOX_GAMEPAD *state){*state=keyboard;return 1;}
static int fake_attached(SDL_GameController *p){(void)p;return attached;}
static int16_t fake_axis(SDL_GameController *p,SDL_GameControllerAxis axis){(void)p;return axes[axis];}
static const char *fake_name(SDL_GameController *p){(void)p;return "Synthetic PlayStation";}
static SDL_Joystick *fake_joystick(SDL_GameController *p){return (SDL_Joystick *)p;}
static int fake_instance(SDL_Joystick *p){(void)p;return instance;}
static uint32_t fake_ticks(void){return ticks;}
static SDL_GameControllerType fake_type(SDL_GameController *p){(void)p;return controller_type;}
static int fake_rumble(SDL_GameController *p,Uint16 l,Uint16 r,Uint32 ms)
{(void)p;(void)ms;if(!l&&!r)++stopped;return 0;}
#define SDL_GameControllerGetAttached fake_attached
#define SDL_GameControllerGetAxis fake_axis
#define SDL_GameControllerName fake_name
#define SDL_GameControllerGetJoystick fake_joystick
#define SDL_JoystickInstanceID fake_instance
#define SDL_GetTicks fake_ticks
#define SDL_GameControllerGetType fake_type
#define SDL_GameControllerRumble fake_rumble
#include BLACK_INPUT_ROUTING_SOURCE
static XBOX_GAMEPAD poll(void)
{XBOX_INPUT_STATE result;assert(black_InputGetState(0,&result)==ERROR_SUCCESS);return result.Gamepad;}
int main(void)
{
    char folder[]="/tmp/black-routing-XXXXXX",path[512];assert(mkdtemp(folder));
    snprintf(path,sizeof path,"%s/settings.ini",folder);setenv("BLACK_SETTINGS_FILE",path,1);unsetenv("BLACK_INPUT_DEVICE");
    g_pads[0]=(SDL_GameController *)(uintptr_t)1;g_controller_connected[0]=1;
    poll();assert(!black_InputKeyboardActive());assert(black_InputPromptFamily()==BLACK_PROMPTS_PLAYSTATION);
    ticks=1000;keyboard.bAnalogButtons[0]=255;XBOX_GAMEPAD pad=poll();
    assert(black_InputKeyboardActive() && pad.bAnalogButtons[0]==255 && stopped==1);
    keyboard=(XBOX_GAMEPAD){0};physical.bAnalogButtons[1]=255;ticks=1010;pad=poll();
    assert(!black_InputKeyboardActive() && pad.bAnalogButtons[1]==255);
    black_SettingsSetInputDevice(BLACK_INPUT_KEYBOARD);pad=poll();assert(black_InputKeyboardActive() && !pad.bAnalogButtons[1]);
    /* A physical pad can still open/control settings in manual Keyboard. */
    physical.wButtons=XBOX_GAMEPAD_START|XBOX_GAMEPAD_BACK;pad=poll();
    assert(black_MenuTakeCommands()==BLACK_MENU_TOGGLE && !pad.wButtons && !pad.bAnalogButtons[1]);
    physical=(XBOX_GAMEPAD){0};black_MenuSetOpen(1);poll();
    black_SettingsSetPadBinding(0,7);black_SettingsSetInputDevice(BLACK_INPUT_GAMEPAD);
    physical.bAnalogButtons[0]=255;pad=poll();assert(!pad.bAnalogButtons[0]);
    assert(black_MenuTakeCommands()==BLACK_MENU_CHANGE); /* physical A, not remapped */
    physical=(XBOX_GAMEPAD){0};poll();black_MenuSetOpen(0);poll();poll();
    axes[SDL_CONTROLLER_AXIS_TRIGGERRIGHT]=32767;pad=poll();assert(pad.bAnalogButtons[0]==255);
    black_GameMenuState(1,0);pad=poll();assert(!pad.bAnalogButtons[0] && pad.bAnalogButtons[7]==255);
    black_GameMenuState(0,0);
    /* Capture consumes its opening key, result and release, no menu commands. */
    black_MenuSetOpen(1);axes[SDL_CONTROLLER_AXIS_TRIGGERRIGHT]=0;physical=(XBOX_GAMEPAD){0};poll();black_MenuTakeCommands();
    black_PadCaptureBegin();physical.bAnalogButtons[0]=255;poll();assert(black_PadCaptureTake()==-2);
    physical=(XBOX_GAMEPAD){0};poll();physical.bAnalogButtons[2]=255;poll();
    assert(black_PadCaptureTake()==2 && !black_MenuTakeCommands());poll();assert(!black_MenuTakeCommands());
    physical=(XBOX_GAMEPAD){0};poll();poll();black_MenuSetOpen(0);poll();poll();
    /* Disconnect: Auto recovers keyboard; manual Gamepad stays neutral. */
    g_pads[0]=NULL;g_controller_connected[0]=0;keyboard.bAnalogButtons[0]=255;
    pad=poll();assert(!black_InputKeyboardActive() && !pad.bAnalogButtons[0]);
    black_SettingsSetInputDevice(BLACK_INPUT_AUTO);pad=poll();assert(black_InputKeyboardActive() && pad.bAnalogButtons[0]==255);
    keyboard=(XBOX_GAMEPAD){0};g_pads[0]=(SDL_GameController *)(uintptr_t)1;g_controller_connected[0]=1;instance=11;
    poll();assert(!black_InputKeyboardActive());
    ticks=2000;black_InputNoteMenuKeyboard();poll();assert(black_InputKeyboardActive());
    /* Closed overlay with an old controller still deflected must not mute
     * keyboard fire after changing owners. */
    black_SettingsSetInputDevice(BLACK_INPUT_GAMEPAD);
    black_MenuSetOpen(1); physical.wButtons=XBOX_GAMEPAD_START; poll();
    black_MenuSetOpen(0); black_SettingsSetInputDevice(BLACK_INPUT_KEYBOARD);
    keyboard.bAnalogButtons[XBOX_BUTTON_RTRIGGER]=255; ticks=3000;
    pad=poll();assert(pad.bAnalogButtons[XBOX_BUTTON_RTRIGGER]==255);
    /* Foreground return also clears an old menu drain immediately. */
    black_SettingsSetInputDevice(BLACK_INPUT_GAMEPAD);poll();
    black_MenuSetOpen(1);poll();black_MenuSetOpen(0);
    black_InputFocusChanged(0);black_InputFocusChanged(1);
    physical.bAnalogButtons[1]=255;pad=poll();assert(pad.bAnalogButtons[1]==255);
    /* Hotplug while a settings chord/fire/aim/stick is held. A replacement's
     * held accept cannot confirm a binding, even if no empty poll intervened. */
    physical=(XBOX_GAMEPAD){0};keyboard=(XBOX_GAMEPAD){0};poll();
    for(int policy=BLACK_INPUT_AUTO;policy<=BLACK_INPUT_KEYBOARD;++policy) {
        black_SettingsSetInputDevice(policy);
        for(int cycle=0;cycle<3;++cycle) {
            black_MenuSetOpen(1);physical.wButtons=XBOX_GAMEPAD_START|XBOX_GAMEPAD_BACK;
            axes[SDL_CONTROLLER_AXIS_LEFTX]=24000;axes[SDL_CONTROLLER_AXIS_TRIGGERRIGHT]=32767;
            physical.wButtons|=XBOX_GAMEPAD_RIGHT_THUMB;poll();black_MenuTakeCommands();
            black_PadCaptureBegin();poll();
            g_pads[0]=NULL;g_controller_connected[0]=0;
            physical=(XBOX_GAMEPAD){0};memset(axes,0,sizeof axes);keyboard=(XBOX_GAMEPAD){0};
            pad=poll();assert(black_PadCaptureTake()==-1 && black_SettingsPadBinding(0)==7);
            assert(!memcmp(&pad,&(XBOX_GAMEPAD){0},sizeof pad));
            assert(black_InputKeyboardActive()==(policy!=BLACK_INPUT_GAMEPAD));
            if(policy!=BLACK_INPUT_GAMEPAD) assert(black_InputPromptFamily()==BLACK_PROMPTS_KEYBOARD);
            BlackControllerMonitor monitor=black_ControllerMonitor();assert(!monitor.connected);
            black_MenuSetOpen(0);keyboard.bAnalogButtons[XBOX_BUTTON_RTRIGGER]=255;
            pad=poll();assert(pad.bAnalogButtons[XBOX_BUTTON_RTRIGGER]==(policy==BLACK_INPUT_GAMEPAD?0:255));
            keyboard=(XBOX_GAMEPAD){0};poll();
            g_pads[0]=(SDL_GameController *)(uintptr_t)1;g_controller_connected[0]=1;++instance;
            controller_type=cycle==1?SDL_CONTROLLER_TYPE_XBOXONE:SDL_CONTROLLER_TYPE_PS5;
            black_MenuSetOpen(1);physical.bAnalogButtons[0]=255;poll();
            assert(!black_MenuTakeCommands()); /* reconnect already holding accept */
            assert(black_InputKeyboardActive()==(policy==BLACK_INPUT_KEYBOARD));
            assert(black_InputPromptFamily()==(policy==BLACK_INPUT_KEYBOARD?BLACK_PROMPTS_KEYBOARD:
                cycle==1?BLACK_PROMPTS_XBOX:BLACK_PROMPTS_PLAYSTATION));
            physical=(XBOX_GAMEPAD){0};poll();physical.bAnalogButtons[0]=255;poll();
            assert(black_MenuTakeCommands()==BLACK_MENU_CHANGE);
            physical=(XBOX_GAMEPAD){0};poll();black_MenuSetOpen(0);poll();poll();
            ticks+=1000;black_InputNoteMenuKeyboard();poll();assert(black_InputKeyboardActive()==(policy!=BLACK_INPUT_GAMEPAD));
            axes[SDL_CONTROLLER_AXIS_LEFTX]=24000;poll();assert(black_InputKeyboardActive()==(policy==BLACK_INPUT_KEYBOARD));
            memset(axes,0,sizeof axes);poll();
        }
    }
    /* An idle no-pad backend may return an error: output and owner must still
     * be reset, including ports other than zero and non-null invalid calls. */
    g_pads[0]=NULL;g_controller_connected[0]=0;no_keyboard_result=1;
    XBOX_INPUT_STATE cleared;memset(&cleared,0xFF,sizeof cleared);
    assert(black_InputGetState(0,&cleared)==ERROR_DEVICE_NOT_CONNECTED);
    assert(black_InputKeyboardActive() && !cleared.Gamepad.sThumbLX && !cleared.Gamepad.wButtons);
    assert(black_InputPromptFamily()==BLACK_PROMPTS_KEYBOARD);
    black_SettingsSetInputDevice(BLACK_INPUT_GAMEPAD);
    assert(black_InputGetState(0,&cleared)==ERROR_DEVICE_NOT_CONNECTED && !black_InputKeyboardActive());
    assert(black_InputGetState(0,NULL)==ERROR_DEVICE_NOT_CONNECTED);
    assert(black_InputGetState(5,NULL)==ERROR_DEVICE_NOT_CONNECTED);
    unlink(path);rmdir(folder);puts("Production input routing: selection/reconnect/prompts/remap/menu/capture: PASS (synthetic SDL)");
}
