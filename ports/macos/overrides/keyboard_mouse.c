/* Native-C input policy adapted from TGP common/mouse_keyboard.cpp.
 * Key IDs are physical macOS key codes, plus five mouse buttons and wheel.
 * Cocoa owns event delivery; no desktop-wide input or polling is used. */
#include "keyboard_mouse.h"
#include "port_menu.h"
#include "flash_menu.h"
#include <pthread.h>
#include <math.h>
#include <string.h>
#include <time.h>

static const BlackKeyAction actions[BLACK_ACTION_COUNT] = {
    {"move_forward", "MOVE FORWARD", 16, {13,-1}}, {"move_back", "MOVE BACK", 17, {1,-1}},
    {"strafe_left", "STRAFE LEFT", 18, {0,-1}}, {"strafe_right", "STRAFE RIGHT", 19, {2,-1}},
    {"walk", "WALK", 24, {58,-1}}, {"crouch", "CROUCH", 14, {8,-1}},
    {"fire", "FIRE", 7, {BLACK_MOUSE_LEFT,-1}}, {"aim", "AIM", 15, {BLACK_MOUSE_RIGHT,-1}},
    {"melee", "MELEE", 1, {9,-1}}, {"reload", "RELOAD", 0, {15,-1}},
    {"interact", "INTERACT", 2, {3,-1}}, {"grenade", "THROW GRENADE", 6, {5,-1}},
    {"weapon_up", "CHANGE WEAPON UP", 11, {14,BLACK_WHEEL_UP}},
    {"weapon_down", "CHANGE WEAPON DOWN", 10, {12,BLACK_WHEEL_DOWN}},
    {"suppressor", "TOGGLE SUPPRESSOR", 3, {17,-1}},
    {"firing_mode", "FIRING MODE / SCOPE ZOOM", 8, {7,-1}},
    {"health_pack", "HEALTH PACK", 9, {4,-1}}, {"next_track", "NEXT MUSIC TRACK", 5, {45,-1}},
    {"back", "OBJECTIVES", 13, {48,-1}}, {"pause", "PAUSE MENU", 12, {53,-1}},
    {"look_up", "LOOK UP", 20, {126,-1}}, {"look_down", "LOOK DOWN", 21, {125,-1}},
    {"look_left", "LOOK LEFT", 22, {123,-1}}, {"look_right", "LOOK RIGHT", 23, {124,-1}}
};
typedef struct { int code; const char *name; } KeyName;
static const KeyName names[] = {
    {0,"A"},{1,"S"},{2,"D"},{3,"F"},{4,"H"},{5,"G"},{6,"Z"},{7,"X"},{8,"C"},{9,"V"},
    {11,"B"},{12,"Q"},{13,"W"},{14,"E"},{15,"R"},{16,"Y"},{17,"T"},{18,"1"},{19,"2"},
    {20,"3"},{21,"4"},{22,"6"},{23,"5"},{24,"Equals"},{25,"9"},{26,"7"},{27,"Minus"},
    {28,"8"},{29,"0"},{30,"RBracket"},{31,"O"},{32,"U"},{33,"LBracket"},{34,"I"},{35,"P"},
    {36,"Enter"},{37,"L"},{38,"J"},{39,"Quote"},{40,"K"},{41,"Semicolon"},{42,"Backslash"},
    {43,"Comma"},{44,"Slash"},{45,"N"},{46,"M"},{47,"Period"},{48,"Tab"},{49,"Space"},
    {50,"Backtick"},{51,"Backspace"},{53,"Escape"},{56,"Shift"},{58,"Alt"},{59,"Ctrl"},
    {65,"NumpadPeriod"},{67,"NumpadStar"},{69,"NumpadPlus"},{75,"NumpadSlash"},
    {76,"NumpadEnter"},{78,"NumpadMinus"},{82,"Numpad0"},{83,"Numpad1"},{84,"Numpad2"},
    {85,"Numpad3"},{86,"Numpad4"},{87,"Numpad5"},{88,"Numpad6"},{89,"Numpad7"},
    {91,"Numpad8"},{92,"Numpad9"},{96,"F5"},{97,"F6"},{98,"F7"},{99,"F3"},{100,"F8"},
    {101,"F9"},{103,"F11"},{111,"F12"},{115,"Home"},{116,"PageUp"},{117,"Delete"},
    {118,"F4"},{119,"End"},{120,"F2"},{121,"PageDown"},{123,"Left"},{124,"Right"},
    {125,"Down"},{126,"Up"},{256,"LMB"},{257,"RMB"},{258,"MMB"},{259,"Mouse4"},
    {260,"Mouse5"},{261,"Wheel Up"},{262,"Wheel Down"}
};
const BlackKeyAction *black_KeyActions(void) { return actions; }
int black_KeyCanonical(int key) { return key == 60 ? 56 : key == 61 ? 58 : key == 62 ? 59 : key; }
const char *black_KeyName(int key)
{
    if (key < 0) return "UNBOUND";
    key = black_KeyCanonical(key);
    for (unsigned i = 0; i < sizeof names / sizeof names[0]; ++i) if (names[i].code == key) return names[i].name;
    return "UNKNOWN";
}
int black_KeyValid(int key) { return key == -1 || (key >= 0 && key < BLACK_KEY_LIMIT && strcmp(black_KeyName(key), "UNKNOWN") != 0); }
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static uint8_t held[BLACK_KEY_LIMIT];
static uint64_t wheel_until[2], last_take;
static double motion[2];
static int captured;
uint64_t black_InputNowMs(void)
{
    struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000 + 1;
}
void black_KeyEvent(int key, int down)
{
    key = black_KeyCanonical(key);
    if (key < 0 || key >= BLACK_KEY_LIMIT || !black_KeyValid(key)) return;
    pthread_mutex_lock(&lock); held[key] = !!down; pthread_mutex_unlock(&lock);
}
void black_KeyReleaseAll(void)
{
    pthread_mutex_lock(&lock);
    memset(held, 0, sizeof held); memset(wheel_until, 0, sizeof wheel_until);
    motion[0] = motion[1] = 0; last_take = 0;
    pthread_mutex_unlock(&lock);
    black_GamePointerRelease();
}
void black_MouseCapture(int on)
{
    pthread_mutex_lock(&lock); captured = !!on;
    motion[0] = motion[1] = 0; last_take = 0;
    pthread_mutex_unlock(&lock);
}
void black_MouseMotion(double dx, double dy)
{
    if (!isfinite(dx) || !isfinite(dy)) return;
    pthread_mutex_lock(&lock);
    if (captured) { motion[0] = fmax(-1e6, fmin(1e6, motion[0] + dx)); motion[1] = fmax(-1e6, fmin(1e6, motion[1] + dy)); }
    pthread_mutex_unlock(&lock);
}
void black_MouseWheel(int up, uint64_t now)
{
    pthread_mutex_lock(&lock); wheel_until[!up] = now + 80; pthread_mutex_unlock(&lock);
}
int black_MouseTake(uint64_t now, int aim, float *dx, float *dy)
{
    pthread_mutex_lock(&lock);
    int fresh = last_take && now >= last_take && now - last_take < 250;
    last_take = now;
    double x = motion[0], y = motion[1]; motion[0] = motion[1] = 0;
    int active = fresh && captured;
    pthread_mutex_unlock(&lock);
    *dx = *dy = 0;
    if (!active || black_MenuIsOpen() || !black_InputKeyboardActive()) return 0;
    *dx = (float)x * black_SettingsMouseSpeed(aim ? 2 : 0) / 50.0f * (black_SettingsMouseInvert(0) ? -1 : 1);
    *dy = (float)y * black_SettingsMouseSpeed(aim ? 3 : 1) / 50.0f * (black_SettingsMouseInvert(1) ? -1 : 1);
    return x != 0 || y != 0;
}
void black_KeyboardState(uint64_t now, uint16_t *buttons, uint8_t analog[8], int16_t sticks[4])
{
    uint8_t keys[BLACK_KEY_LIMIT]; int play;
    pthread_mutex_lock(&lock);
    memcpy(keys, held, sizeof keys);
    keys[BLACK_WHEEL_UP] = now < wheel_until[0]; keys[BLACK_WHEEL_DOWN] = now < wheel_until[1];
    play = last_take && now >= last_take && now - last_take < 250;
    pthread_mutex_unlock(&lock);
    if (black_MenuIsOpen()) return;
    int menu=black_GameMenuActive(),text=black_GameTextActive();
    if(menu)play=0;
    int targets[25] = {0};
    for (int i = 0; i < BLACK_ACTION_COUNT; ++i) {
        for (int slot = 0; slot < 2; ++slot) {
            int key = black_SettingsBinding(i, slot);
            if (key >= 0 && key < BLACK_KEY_LIMIT && keys[key] &&
                (play || (key != BLACK_WHEEL_UP && key != BLACK_WHEEL_DOWN)) && (!text || actions[i].target == 12))
                targets[actions[i].target] = 1;
        }
    }
    if (!play) {
        if (!text && (keys[36] || keys[76] || keys[49])) targets[0] = 1;
        if ((!text && keys[51]) || keys[BLACK_MOUSE_RIGHT]) targets[1] = 1;
        for (int i = 0; i < 4; ++i) targets[8+i] |= keys[(int[]){126,125,123,124}[i]];
        for (int i = 20; i < 24; ++i) targets[i] = 0;
    }
    if(menu && (targets[12] || keys[53])) { targets[12]=0; targets[1]=1; }
    if(black_GameConfirmPending())targets[0]=1;
    for (int i = 0; i < 8; ++i) if (targets[i]) analog[i] = 255;
    for (int i = 8; i < 16; ++i) if (targets[i]) *buttons |= 1u << (i-8);
    float speed = targets[24] ? black_SettingsWalkSpeed() / 100.0f : 1;
    int x = targets[19]-targets[18], y = targets[16]-targets[17];
    if (targets[18] || targets[19]) sticks[0] = (int16_t)(x * speed * 32767);
    if (targets[16] || targets[17]) sticks[1] = (int16_t)(y * speed * 32767);
    if (targets[22] || targets[23]) sticks[2] = (targets[23]-targets[22]) * 32767;
    if (targets[20] || targets[21]) sticks[3] = (targets[20]-targets[21]) * 32767;
}
