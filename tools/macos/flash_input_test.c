#include "../../ports/macos/overrides/flash_menu.h"
#include "../../ports/macos/overrides/keyboard_mouse.h"
#include "../../ports/macos/overrides/port_menu.h"
#include "../../ports/macos/overrides/flash_memory.h"
#include <math.h>
#include <stdio.h>
int main(void)
{
    if(!black_flash_memory_ok(0x800f4000u,16,0x4000000)||
        black_flash_memory_ok(0x83fffffcu,8,0x4000000)||black_flash_memory_ok(0xfffffffcu,8,0x4000000)||
        black_flash_memory_ok(0,16,0x4000000)||!black_flash_memory_ok(0x2d199c,4,0x4000000))return 15;
    float x,y;
    if(!black_FramePointer(480,300,960,600,16.0f/9,&x,&y)||fabsf(x-.5f)>.0001||fabsf(y-.5f)>.0001)return 1;
    if(black_FramePointer(10,10,960,600,16.0f/9,&x,&y)||black_FramePointer(0,0,0,600,1,&x,&y))return 2;
    if(!black_FramePointer(320,240,640,480,4.0f/3,&x,&y)||x!=.5f||y!=.5f)return 3;
    black_GameMenuState(1,0);black_InputSetKeyboardActive(1);black_GamePointerPosition(.5f,.5f,1);
    black_GamePointerButton(1);black_GamePointerButton(0);BlackFlashPointer p=black_GamePointerTake();
    if(!p.valid||!p.click||p.down||!black_GamePointerIntent())return 4;
    if(black_GamePointerTake().click)return 5;
    black_GamePointerWheel(1);if(black_GamePointerTake().wheel!=1||black_GamePointerTake().wheel)return 6;
    black_GameMenuState(-1,1);for(int i=0;i<70;++i)black_GameTextCharacter('a');
    int count=0;while(black_GameTextTake()>=0)++count;if(count!=64)return 7;
    black_GameTextCharacter(0x410);if(black_GameTextTake()!=-1)return 8;
    black_GameTextCharacter('x');black_GamePointerRelease();if(black_GameTextTake()!=-1||black_GamePointerTake().valid)return 9;
    black_GamePointerPosition(.5f,.5f,1);black_GamePointerButton(1);black_InputSetKeyboardActive(0);
    if(black_GamePointerTake().valid)return 10;
    black_InputSetKeyboardActive(1);if(!black_GamePointerTake().click)return 11;
    black_MenuSetOpen(1);if(black_GamePointerTake().valid)return 12;black_MenuSetOpen(0);
    black_GameMenuConfirm();if(!black_GameConfirmPending())return 13;
    black_GameMenuState(0,0);if(black_GameMenuActive()||black_GameTextActive()||black_GameConfirmPending())return 14;
    puts("Flash host input: viewport/letterbox, click edge retention, wheel, bounded text queue, owner/overlay/focus/page gates: PASS");return 0;
}
