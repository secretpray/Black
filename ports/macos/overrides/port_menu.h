#ifndef BLACK_PORT_MENU_H
#define BLACK_PORT_MENU_H
#include <stdint.h>
enum { BLACK_MENU_TOGGLE = 1, BLACK_MENU_CHANGE = 2, BLACK_MENU_BACK = 4,
       BLACK_MENU_OFF = 8, BLACK_MENU_ON = 16, BLACK_MENU_UP = 32, BLACK_MENU_DOWN = 64,
       BLACK_MENU_PREV_TAB = 128, BLACK_MENU_NEXT_TAB = 256 };
int black_SettingsSkipIntros(void);
int black_SettingsSetSkipIntros(int enabled);
int black_SettingsSkipMissionIntros(void);
int black_SettingsSetSkipMissionIntros(int enabled);
int black_SettingsRenderScale(void);
int black_SettingsSetRenderScale(int scale);
int black_SettingsFov(void);
int black_SettingsSetFov(int degrees);
int black_SettingsAntiAliasing(void);
int black_SettingsSetAntiAliasing(int mode);
int black_SettingsAniso(void);
int black_SettingsSetAniso(int level);
int black_SettingsValidAniso(int level);
int black_SettingsUpscaler(void);
int black_SettingsSetUpscaler(int mode);
int black_SettingsSaveFailed(void);
int black_MenuIsOpen(void);
void black_MenuSetOpen(int open);
void black_MenuCommand(unsigned commands);
unsigned black_MenuTakeCommands(void);
/* Input preferences apply live except driver selection, which needs restart. */
int black_SettingsStickDeadzone(int right);
int black_SettingsStickThreshold(int right);
int black_SettingsSetStickDeadzone(int right, int percent);
int black_SettingsTriggerDeadzone(void);
int black_SettingsSetTriggerDeadzone(int percent);
int black_SettingsVibration(void);
int black_SettingsSetVibration(int enabled);
int black_SettingsDualSenseHid(void);
int black_SettingsSetDualSenseHid(int enabled);
int black_SettingsResetInput(void);
/* TGP controller profile: 50 units = the original 1x look speed. */
int black_SettingsLookSpeed(int axis);
int black_SettingsSetLookSpeed(int axis, int units);
int black_SettingsInvertX(void);
int black_SettingsSetInvertX(int enabled);
int black_SettingsRumbleIntensity(void);
int black_SettingsSetRumbleIntensity(int percent);
int black_InputKeyboardActive(void);
void black_InputSetKeyboardActive(int active);
int black_SettingsHoldAim(void);
int black_SettingsSetHoldAim(int enabled);
int black_SettingsAimAssist(void);
int black_SettingsSetAimAssist(int enabled);
int black_ControllerAimAssist(void);
void black_MenuPollHost(void);
/* Cocoa entry points, called on the main thread only. */
void black_MenuAttach(void *window, void (*capture)(int));
void black_MenuDetach(void);
void black_MenuPump(void);
int black_MenuUsingGameFonts(void);
#endif
