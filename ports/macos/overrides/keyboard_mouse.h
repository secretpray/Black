#ifndef BLACK_KEYBOARD_MOUSE_H
#define BLACK_KEYBOARD_MOUSE_H
#include <stdint.h>
enum { BLACK_KEY_LIMIT = 263, BLACK_MOUSE_LEFT = 256, BLACK_MOUSE_RIGHT,
       BLACK_MOUSE_MIDDLE, BLACK_MOUSE_4, BLACK_MOUSE_5, BLACK_WHEEL_UP, BLACK_WHEEL_DOWN };
enum { BLACK_ACTION_COUNT = 24 };
typedef struct { const char *id, *label; int target, keys[2]; } BlackKeyAction;
const BlackKeyAction *black_KeyActions(void);
int black_KeyValid(int key);
int black_KeyCanonical(int key);
const char *black_KeyName(int key);
int black_SettingsBinding(int action, int slot);
int black_SettingsSetBinding(int action, int slot, int key);
int black_SettingsResetKeyboard(void);
int black_SettingsMouseSpeed(int axis);
int black_SettingsSetMouseSpeed(int axis, int units);
int black_SettingsMouseInvert(int vertical);
int black_SettingsSetMouseInvert(int vertical, int enabled);
int black_SettingsMouseHoldAim(void);
int black_SettingsSetMouseHoldAim(int enabled);
int black_SettingsWalkSpeed(void);
int black_SettingsSetWalkSpeed(int percent);
void black_KeyEvent(int key, int down);
void black_KeyReleaseAll(void);
void black_MouseCapture(int captured);
void black_MouseMotion(double dx, double dy);
void black_MouseWheel(int up, uint64_t now_ms);
uint64_t black_InputNowMs(void);
/* The game heartbeat also decides whether fixed menu navigation is needed. */
int black_MouseTake(uint64_t now_ms, int aim, float *dx, float *dy);
void black_KeyboardState(uint64_t now_ms, uint16_t *buttons, uint8_t analog[8], int16_t sticks[4]);
#endif
