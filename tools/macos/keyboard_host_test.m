/* Compile the production Cocoa input methods with a local event fixture.
 * No desktop event injection, pointer capture, game process or save data. */
#import <Cocoa/Cocoa.h>
#include "../../ports/macos/overrides/keyboard_mouse.h"
#include "../../ports/macos/overrides/port_menu.h"
#include "../../ports/macos/overrides/flash_menu.h"
#include "../../ports/macos/overrides/mouse_capture.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
static double display_aspect(void){return 4.0/3.0;}
static BOOL s_mouse_captured;
static NSWindow *s_window;
enum { K_START = 4 };
static int start_down;
static void xek_capture_mouse(BOOL on) { s_mouse_captured=on; black_MouseCapture(on); }
static void xbox_KeyboardReleaseAll(void) { black_KeyReleaseAll(); }
static void xbox_MouseReleaseAll(void) { black_KeyReleaseAll(); }
static void xbox_KeyboardPressSlot(int slot) { if(slot==K_START)start_down=1; }
static void xbox_KeyboardReleaseSlot(int slot) { if(slot==K_START)start_down=0; }
@interface BlackEventFixture : NSEvent {
@public unsigned short code; NSUInteger modifiers; double dx,dy,scroll; NSInteger button; NSString *typed;
}
@end
@implementation BlackEventFixture
- (unsigned short)keyCode { return code; }
- (NSEventModifierFlags)modifierFlags { return modifiers; }
- (CGFloat)deltaX { return dx; }
- (CGFloat)deltaY { return dy; }
- (CGFloat)scrollingDeltaY { return scroll; }
- (NSInteger)buttonNumber { return button; }
- (NSString *)characters { return typed ?: @""; }
- (NSPoint)locationInWindow { return NSZeroPoint; }
@end
@interface BlackHostFixture : NSView
@end
@implementation BlackHostFixture
#include "../../ports/macos/overrides/presenter_input.inc"
@end
static uint16_t buttons;
static uint8_t analog[8];
static int16_t sticks[4];
static void state(void)
{
    buttons=0; memset(analog,0,sizeof analog); memset(sticks,0,sizeof sticks);
    black_KeyboardState(black_InputNowMs(),&buttons,analog,sticks);
}
int main(void)
{
    @autoreleasepool {
        char path[]="/tmp/black-keyboard-host-XXXXXX";int fd=mkstemp(path);if(fd<0)return 1;close(fd);
        setenv("BLACK_SETTINGS_FILE",path,1);unsetenv("RECOMP_BACKGROUND");
        BlackHostFixture *view=[[[BlackHostFixture alloc] initWithFrame:NSZeroRect] autorelease];
        BlackEventFixture *e=[[[BlackEventFixture alloc] init] autorelease];
        black_InputSetKeyboardActive(1);
        e->code=13;[view keyDown:e];state();if(sticks[1]!=32767)return 2;
        [view keyUp:e];state();if(sticks[1])return 3;
        [view mouseDown:e];state();if(!s_mouse_captured||analog[7])return 4;
        [view mouseDown:e];state();if(analog[7]!=255)return 5;
        [view mouseUp:e];state();if(analog[7])return 6;
        float x,y;uint64_t now=black_InputNowMs();black_MouseTake(now,0,&x,&y);
        e->dx=.25;e->dy=.75;[view xek_motion:e];
        if(!black_MouseTake(now+1,0,&x,&y)||x!=.25f||y!=.75f)return 7;
        e->code=61;e->modifiers=NSEventModifierFlagOption;[view flagsChanged:e];
        e->code=13;e->modifiers=0;[view keyDown:e];state();if(sticks[1]!=16383)return 8;
        [view xek_focusLost:nil];state();if(sticks[1]||s_mouse_captured)return 9;
        [view mouseMoved:e];if(black_MouseTake(now+2,0,&x,&y))return 10;
        [view mouseDown:e];e->code=53;[view keyDown:e];if(s_mouse_captured||!start_down)return 11;
        [view keyUp:e];if(start_down)return 12;
        e->code=13;e->modifiers=NSEventModifierFlagCommand;[view keyDown:e];state();if(sticks[1])return 13;
        e->modifiers=0;setenv("RECOMP_BACKGROUND","1",1);[view keyDown:e];[view mouseDown:e];state();
        if(sticks[1]||analog[7]||s_mouse_captured)return 14;
        unsetenv("RECOMP_BACKGROUND");
        black_GameMenuState(1,1);e->code=15;e->typed=@"R";[view keyDown:e];state();
        if(analog[0]||black_GameTextTake()!='R')return 15;
        e->code=51;e->typed=@"\x7f";[view keyDown:e];if(black_GameTextTake()!='\b')return 16;
        e->code=36;e->typed=@"\r";[view keyDown:e];state();if(analog[0]||black_GameTextTake()!='\r')return 17;
        e->code=53;[view keyDown:e];state();if(analog[1]!=255||start_down)return 18;[view keyUp:e];
        [view mouseDown:e];if(s_mouse_captured||!black_GamePointerIntent())return 19;[view mouseUp:e];
        black_GameMenuState(0,0);unlink(path);
        /* Pad -> keyboard with no page transition must restore relative look. */
        BlackMouseCapture capture={0};
        if(black_mouse_capture(&capture,0,0,0,1,0)!=0)return 20;
        black_InputSetKeyboardActive(1);
        xek_capture_mouse(black_mouse_capture(&capture,1,0,0,1,0));
        now+=10;black_MouseTake(now,0,&x,&y);[view xek_motion:e];
        if(!s_mouse_captured||!black_MouseTake(now+1,0,&x,&y)||x!=.25f||y!=.75f)return 21;
        if(black_mouse_capture(&capture,1,0,0,1,0)!=-1)return 22;
        xek_capture_mouse(NO); /* Escape stays released until a page/device transition. */
        if(black_mouse_capture(&capture,1,0,0,1,0)!=-1)return 23;
        if(black_mouse_capture(&capture,0,0,1,1,0)!=0)return 24;
        if(black_mouse_capture(&capture,1,0,1,1,0)!=0)return 25;
        if(black_mouse_capture(&capture,1,0,0,1,0)!=1)return 26;
        if(black_mouse_capture(&capture,0,0,0,1,0)!=0)return 27;
        if(black_mouse_capture(&capture,1,0,0,0,0)!=0)return 28;
        if(black_mouse_capture(&capture,1,0,0,1,0)!=1)return 31;
        xek_capture_mouse(YES); [view mouseDown:e]; state();
        if(analog[7]!=255)return 32; /* First shot after return/device capture. */
        [view mouseUp:e]; state(); if(analog[7])return 33;
        if(black_mouse_capture(&capture,1,1,0,1,0)!=0)return 29;
        if(black_mouse_capture(&capture,1,0,0,1,1)!=0)return 30;
        puts("Production Cocoa input: physical key release, first click, fractional motion, modifier binding, focus/Escape/Command and scripted gates: PASS");
    }
    return 0;
}
