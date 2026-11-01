#include "../../ports/macos/overrides/keyboard_mouse.h"
#include "../../ports/macos/overrides/port_menu.h"
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static union { uint32_t w[1024]; uint8_t b[4096]; } guest;
static uint64_t test_now=1000;
static uint64_t test_time(void) { return test_now; }
static uint8_t *address(unsigned at,unsigned size)
{ if(at<0x100||at+size>sizeof guest)abort(); return guest.b+at; }
#define MEM8(at) (*address(at,1))
#define MEMF(at) (*(float*)address(at,4))
#define black_InputNowMs test_time
#include "../../ports/macos/overrides/game_mouse.inc"
#undef black_InputNowMs
static void reset(void)
{
    memset(&guest,0,sizeof guest); MEMF(0x100+0xB8)=90; MEMF(0x100+0xBC)=90;
    MEMF(0x100+0xC8)=2; MEMF(0x800+0x1C)=1.0f/30;
    black_KeyReleaseAll(); black_MouseCapture(1); black_MouseLook(0x100,0x800,1);
}
int main(void)
{
    char path[]="/tmp/black-game-mouse-XXXXXX";int fd=mkstemp(path);if(fd<0)return 1;close(fd);
    setenv("BLACK_SETTINGS_FILE",path,1);black_InputSetKeyboardActive(1);reset();
    black_MouseMotion(10,20);test_now+=16; black_MouseLook(0x100,0x800,1);
    if(fabsf(MEMF(0x118)+.5f)>.00001||fabsf(MEMF(0x11c)-1)>.00001)return 2;
    if(fabsf(MEMF(0x1D8)+sqrtf(.5f/3))>.00001||fabsf(MEMF(0x1DC)+sqrtf(1.0f/3))>.00001)return 3;
    test_now+=16;black_MouseLook(0x100,0x800,1);if(MEMF(0x118)!=-.5f)return 4;
    reset();for(int i=0;i<10;++i){black_MouseMotion(1,2);test_now+=16;black_MouseLook(0x100,0x800,1);}
    if(fabsf(MEMF(0x118)+.5f)>.00001||fabsf(MEMF(0x11c)-1)>.00001)return 5; /* same total motion, independent frame partition */
    reset();MEM8(0x145)=1;black_SettingsSetMouseSpeed(2,25);black_SettingsSetMouseSpeed(3,100);
    black_MouseMotion(10,20);test_now+=16;black_MouseLook(0x100,0x800,.5f);
    if(MEMF(0x118)!=-.25f||MEMF(0x11c)!=2)return 6; /* aim settings replace normal; no extra zoom multiplier */
    reset();MEMF(0x1D8)=.3f;MEMF(0x1DC)=-.2f;
    black_MouseMotion(10,20);test_now+=16;black_MouseLook(0x100,0x800,1);
    if(MEMF(0x1D8)!=.3f||MEMF(0x1DC)!=-.2f)return 7; /* existing stick sway stays intact */
    reset();black_SettingsSetMouseInvert(0,1);black_SettingsSetMouseInvert(1,1);
    black_MouseMotion(8000,2);test_now+=16;black_MouseLook(0x100,0x800,1);
    if(fabsf(MEMF(0x118)-40)>.0001||fabsf(MEMF(0x11c)+.1f)>.0001)return 8; /* yaw remainder, inverse axes */
    if(MEMF(0x1D8)!=1)return 9; /* sway saturation does not cap direct angle */
    unlink(path);puts("Production native-C MouseLook: exact angles, frame partition, independent aim/invert, sway oracle and yaw wrap: PASS");return 0;
}
