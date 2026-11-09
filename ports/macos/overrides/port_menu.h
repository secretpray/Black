#ifndef BLACK_PORT_MENU_H
#define BLACK_PORT_MENU_H
#include <stdint.h>
enum { BLACK_MENU_TOGGLE = 1, BLACK_MENU_CHANGE = 2, BLACK_MENU_BACK = 4,
       BLACK_MENU_OFF = 8, BLACK_MENU_ON = 16, BLACK_MENU_UP = 32, BLACK_MENU_DOWN = 64,
       BLACK_MENU_PREV_TAB = 128, BLACK_MENU_NEXT_TAB = 256, BLACK_MENU_QUIT = 512,
       BLACK_MENU_RESET = 1024 /* the selected setting back to its default */ };
int black_SettingsSkipIntros(void);
int black_SettingsSetSkipIntros(int enabled);
int black_SettingsSkipMissionIntros(void);
int black_SettingsSetSkipMissionIntros(int enabled);
int black_SettingsSkipAnyFilm(void);
int black_SettingsSetSkipAnyFilm(int enabled);
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
/* Game text from the disc's string banks (next launch), and this panel's own. */
/* Russian is built in the installed copy from its English bank (game_translation.h). */
enum { BLACK_LANG_ENGLISH, BLACK_LANG_FRENCH, BLACK_LANG_GERMAN, BLACK_LANG_SPANISH, BLACK_LANG_ITALIAN, BLACK_LANG_RUSSIAN, BLACK_GAME_LANGUAGES };
int black_SettingsGameLanguage(void);
int black_SettingsSetGameLanguage(int language);
int black_SettingsMenuLanguage(void);
int black_SettingsSetMenuLanguage(int language);
/* Display brightness and gamma over the game's ramp (display_gamma.h); live. */
int black_SettingsDisplayBrightness(void);
int black_SettingsSetDisplayBrightness(int percent);
int black_SettingsDisplayGamma(void);
int black_SettingsSetDisplayGamma(int hundredths);
int black_SettingsFpsCounter(void);
int black_SettingsSetFpsCounter(int enabled);
/* HD texture replacement (texture_pack.h): off by default, live. */
int black_SettingsTextureReplacement(void);
int black_SettingsSetTextureReplacement(int enabled);
/* Largest replacement side: 1024, 2048 (default) or 4096; live. */
int black_SettingsTextureMaxSize(void);
int black_SettingsSetTextureMaxSize(int size);
int black_SettingsSaveFailed(void);
/* Game window state, saved by the window itself (window_state.m). */
int black_SettingsValidWindowFrame(const int frame[4]);
int black_SettingsWindowFrame(int frame[4]);
int black_SettingsWindowFullScreen(void);
int black_SettingsSetWindowState(const int frame[4], int full_screen);
int black_MenuIsOpen(void);          /* settings panel or quit dialog */
int black_SettingsPanelOpen(void);
void black_MenuSetOpen(int open);
int black_QuitDialogOpen(void);
void black_QuitDialogSetOpen(int open);
void black_QuitToMenuRequest(void);
int black_QuitToMenuTake(void);
int black_GameInMission(void);
int black_GamePaused(void);
void black_InputPulseStart(void);
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
void black_InputFocusChanged(int focused);
unsigned black_InputFocusEpoch(void);
int black_InputFocused(void);
void black_InputSetKeyboardActive(int active);
int black_SettingsHoldAim(void);
int black_SettingsSetHoldAim(int enabled);
int black_SettingsAimAssist(void);
int black_SettingsSetAimAssist(int enabled);
int black_ControllerAimAssist(void);
void black_MenuPollHost(void);
/* Mods page values (game_mods.h order), live and saved: 0 off, 1 on, or a
 * level up to black_mod_info[].levels. The game reports the XBE revision it
 * found; "DETECTING" until then. */
int black_SettingsMod(int mod);
int black_SettingsSetMod(int mod, int value);
int black_SettingsResetMods(void);
void black_ModsSetRevision(const char *name, int supported);
const char *black_ModsRevision(void);
int black_ModsSupported(void);
/* Cocoa entry points, called on the main thread only. */
void black_MenuAttach(void *window, void (*capture)(int));
void black_MenuDetach(void);
void black_MenuPump(void);
int black_MenuUsingGameFonts(void);
#endif
