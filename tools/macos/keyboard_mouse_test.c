#include "../../ports/macos/overrides/keyboard_mouse.h"
#include "../../ports/macos/overrides/port_menu.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static uint16_t buttons;
static uint8_t analog[8];
static int16_t sticks[4];
static void state(uint64_t now)
{
    buttons=0; memset(analog,0,sizeof analog); memset(sticks,0,sizeof sticks);
    black_KeyboardState(now,&buttons,analog,sticks);
}
int main(void)
{
    char path[]="/tmp/black-keyboard-mouse-XXXXXX";
    int fd=mkstemp(path); if(fd<0)return 1; close(fd); setenv("BLACK_SETTINGS_FILE",path,1);
    for(int i=0;i<BLACK_ACTION_COUNT;++i) for(int slot=0;slot<2;++slot)
        if(black_SettingsBinding(i,slot)!=black_KeyActions()[i].keys[slot])return 2;
    if(black_KeyValid(-2)||black_KeyValid(263)||black_KeyValid(122)||black_KeyValid(109)||black_KeyValid(55))return 3;
    float x,y; black_InputSetKeyboardActive(1); black_MouseCapture(1); black_MouseTake(1000,0,&x,&y);
    black_KeyEvent(13,1); black_KeyEvent(2,1); state(1010);
    if(sticks[0]!=32767||sticks[1]!=32767)return 4;
    black_KeyEvent(1,1); black_KeyEvent(0,1); state(1020);
    if(sticks[0]||sticks[1])return 5;
    black_KeyReleaseAll(); black_MouseTake(1030,0,&x,&y);
    black_KeyEvent(BLACK_MOUSE_LEFT,1); black_KeyEvent(BLACK_MOUSE_RIGHT,1); state(1040);
    if(analog[7]!=255||buttons!=128||sticks[2]||sticks[3])return 6; /* RT fire, R3 aim, no mouse-as-stick */
    black_KeyReleaseAll(); black_KeyEvent(13,1); black_KeyEvent(61,1); state(1050);
    if(sticks[1]!=16383)return 7; /* right Alt canonicalizes to walk */
    black_KeyReleaseAll(); black_KeyEvent(36,1); black_KeyEvent(126,1); state(3000);
    if(analog[0]!=255||!(buttons&1)||sticks[3])return 8; /* fixed menu Enter/up */
    black_KeyReleaseAll(); black_MouseTake(3010,0,&x,&y); state(3020);
    black_KeyEvent(126,1); state(3030); if(sticks[3]!=32767)return 9;
    black_KeyReleaseAll(); black_MouseTake(4000,0,&x,&y); black_MouseWheel(1,4000); state(4001); if(!(buttons&8))return 10;
    state(4080); if(buttons)return 11;
    if(!black_SettingsSetBinding(0,1,35))return 12;
    black_KeyEvent(35,1); state(4100); if(sticks[1]!=32767)return 13;
    black_KeyEvent(13,1); black_KeyEvent(35,0); state(4110); if(sticks[1]!=32767)return 14;
    black_KeyEvent(13,0); state(4120); if(sticks[1])return 15; /* alternate release preserves other held key */
    black_KeyEvent(35,1); black_KeyReleaseAll(); state(4130); if(sticks[1])return 16;
    black_MouseCapture(1); black_MouseTake(5000,0,&x,&y);
    black_MouseMotion(.25,.5); black_MouseMotion(.75,1.5);
    if(!black_MouseTake(5016,0,&x,&y)||x!=1||y!=2)return 17;
    if(black_MouseTake(5017,0,&x,&y)||x||y)return 18; /* exactly once, not USB polling */
    black_SettingsSetMouseSpeed(2,25); black_SettingsSetMouseSpeed(3,100); black_SettingsSetMouseInvert(1,1);
    black_MouseMotion(4,3); if(!black_MouseTake(5032,1,&x,&y)||x!=2||y!=-6)return 19;
    black_MouseMotion(99,99); if(black_MouseTake(5400,0,&x,&y)||x||y)return 20; /* stale pause input dropped */
    black_MouseMotion(2,2); black_InputSetKeyboardActive(0);
    if(black_MouseTake(5416,0,&x,&y)||x||y)return 21;
    black_InputSetKeyboardActive(1); if(black_MouseTake(5432,0,&x,&y))return 22; /* no deferred jump after pad */
    black_MouseMotion(2,2); black_MenuSetOpen(1);
    if(black_MouseTake(5448,0,&x,&y))return 23;
    state(5448); if(buttons||analog[7]||sticks[0])return 24;
    black_MenuSetOpen(0); black_MouseCapture(0); black_MouseMotion(2,2);
    if(black_MouseTake(5464,0,&x,&y))return 25;
    black_MouseCapture(1); black_MouseTake(6000,0,&x,&y); black_MouseMotion(NAN,INFINITY);
    if(black_MouseTake(6016,0,&x,&y))return 26;
    if(!black_SettingsResetKeyboard()||black_SettingsMouseSpeed(2)!=50||black_SettingsMouseInvert(1)||
        black_SettingsBinding(0,1)!=-1||!black_SettingsMouseHoldAim())return 27;
    unlink(path); puts("Rebindable TGP actions, alternate holds, menu navigation, walk/wheel, precise mouse consumption and capture/stale/owner gates: PASS");
    return 0;
}
