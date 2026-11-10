/* Isolated native UI check. No global keyboard injection, guest, audio or save data. */
#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#include "../../ports/macos/overrides/port_menu.h"
#include "../../ports/macos/overrides/menu_pages.h"
#include "../../ports/macos/overrides/keyboard_mouse.h"
#include "../../ports/macos/overrides/input_profile.h"
#include "../../ports/macos/overrides/controller_monitor.h"
#include "../../ports/macos/overrides/game_mods.h"
#include "../../ports/macos/overrides/black_box.h"
#include <unistd.h>
static int quit_requests;
void black_HostQuit(void) { ++quit_requests; }

static void pump(double seconds)
{
    double end = CACurrentMediaTime() + seconds;
    do {
        NSEvent *event;
        while ((event = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:NSDate.distantPast
            inMode:NSDefaultRunLoopMode dequeue:YES])) [NSApp sendEvent:event];
        black_MenuPump();
        [NSApp.windows.firstObject displayIfNeeded];
        usleep(5000);
    } while (CACurrentMediaTime() < end);
}
static void key(NSWindow *window, unsigned short code, NSString *chars)
{
    for (NSNumber *down in @[@YES, @NO]) {
        NSEvent *event = [NSEvent keyEventWithType:down.boolValue ? NSEventTypeKeyDown : NSEventTypeKeyUp
            location:NSZeroPoint modifierFlags:0 timestamp:0 windowNumber:window.windowNumber
            context:nil characters:chars charactersIgnoringModifiers:chars isARepeat:NO keyCode:code];
        [NSApp postEvent:event atStart:NO];
    }
    pump(code == 122 || code == 53 ? .25 : .06);
}
static void snapshot(NSWindow *window, NSString *path)
{
    NSView *view = window.contentView;
    [view displayIfNeeded];
    NSBitmapImageRep *rep = [view bitmapImageRepForCachingDisplayInRect:view.bounds];
    [view cacheDisplayInRect:view.bounds toBitmapImageRep:rep];
    [[rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:path atomically:YES];
}
static NSButton *button(NSView *view, NSString *title)
{
    if ([view isKindOfClass:NSButton.class] && [((NSButton *)view).title isEqual:title]) return (NSButton *)view;
    for (NSView *child in view.subviews) {
        NSButton *found = button(child, title);
        if (found) return found;
    }
    return nil;
}
static void click(NSWindow *window, NSView *view, CGFloat x, CGFloat y)
{
    NSPoint point = [view convertPoint:NSMakePoint(x, y) toView:nil];
    for (NSNumber *down in @[@YES, @NO]) {
        NSEvent *event = [NSEvent mouseEventWithType:down.boolValue ? NSEventTypeLeftMouseDown : NSEventTypeLeftMouseUp
            location:point modifierFlags:0 timestamp:0 windowNumber:window.windowNumber
            context:nil eventNumber:0 clickCount:1 pressure:1];
        [NSApp postEvent:event atStart:NO];
    }
    pump(.25);
}
/* The page's Defaults row, confirmed: Down from `from`, then Yes on the question. */
static void page_defaults(NSWindow *window, int page, int from)
{
    int count = black_menu_count(page);
    int steps = (black_menu_index(page, ROW_DEFAULTS) - black_menu_index(page, from) + count) % count;
    while (steps-- > 0) key(window, 125, @"\uF701");
    key(window, 36, @"\r");
    key(window, 126, @"\uF700"); /* the question starts on No */
    key(window, 36, @"\r");
}
int main(int argc, char **argv)
{
    if (argc < 3) return 1;
    @autoreleasepool {
        setenv("BLACK_GAME_DIR", argv[1], 1);
        NSString *output = [NSString stringWithUTF8String:argv[2]];
        [NSFileManager.defaultManager createDirectoryAtPath:output withIntermediateDirectories:YES attributes:nil error:nil];
        NSString *settings = [output stringByAppendingPathComponent:@"preview-settings.ini"];
        [NSFileManager.defaultManager removeItemAtPath:settings error:nil];
        setenv("BLACK_SETTINGS_FILE", settings.fileSystemRepresentation, 1);
        /* A stub HD pack: the texture rows are checkable without the real one. */
        NSString *textures = [output stringByAppendingPathComponent:@"textures"];
        NSString *replacement = [textures stringByAppendingPathComponent:@"replacement"];
        [NSFileManager.defaultManager createDirectoryAtPath:replacement withIntermediateDirectories:YES attributes:nil error:nil];
        [[NSData data] writeToFile:[replacement stringByAppendingPathComponent:@"preview.dds"] atomically:YES];
        [@"1 4 4 0c preview.dds\n" writeToFile:[textures stringByAppendingPathComponent:@"black-hd-reimagined.map"]
            atomically:YES encoding:NSUTF8StringEncoding error:nil];
        setenv("BLACK_TEXTURE_DIR", textures.fileSystemRepresentation, 1);
        unsetenv("BLACK_SKIP_INTROS"); unsetenv("BLACK_OPEN_SETTINGS");
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
        NSWindow *window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,960,600)
            styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable backing:NSBackingStoreBuffered defer:NO];
        [window setReleasedWhenClosed:NO];
        if (argc > 3) {
            NSImageView *back = [[NSImageView alloc] initWithFrame:window.contentView.bounds];
            back.image = [[[NSImage alloc] initWithContentsOfFile:[NSString stringWithUTF8String:argv[3]]] autorelease];
            back.imageScaling = NSImageScaleAxesIndependently;
            window.contentView = back; [back release];
        }
        black_MenuAttach(window, NULL);
        if (!black_MenuUsingGameFonts()) return 11;
        key(window, 122, @"\uF704");
        if (!black_MenuIsOpen()) return 2;
        key(window, 36, @"\r"); /* Port Settings from the top menu */
        if (!black_SettingsPanelOpen()) return 2;
        snapshot(window, [output stringByAppendingPathComponent:@"recomp-display.png"]);
        key(window, 124, @"\uF703");
        if (black_SettingsRenderScale() != 3) return 20;
        key(window, 123, @"\uF702");
        if (black_SettingsRenderScale() != 2) return 21;
        [window makeKeyAndOrderFront:nil]; pump(.1);
        NSButton *resolution = button(window.contentView, @"Render Scale");
        if (!resolution || resolution.hidden) return 25;
        key(window, 123, @"\uF702");
        if (black_SettingsRenderScale() != 1) return 27;
        key(window, 124, @"\uF703");
        key(window, 125, @"\uF701"); key(window, 123, @"\uF702");
        if (black_SettingsUpscaler()) return 38;
        key(window, 124, @"\uF703");
        key(window, 125, @"\uF701"); key(window, 124, @"\uF703");
        if (black_SettingsFov() != 75) return 22;
        key(window, 123, @"\uF702");
        key(window, 125, @"\uF701"); key(window, 36, @"\r");
        if (black_SettingsPerformanceOverlay() != BLACK_PERF_FPS) return 23;
        key(window, 123, @"\uF702");
        if (black_SettingsPerformanceOverlay() != BLACK_PERF_OFF) return 24;
        /* Display brightness and gamma: neutral, one step each way, back. */
        if (black_SettingsDisplayBrightness() != 0 || black_SettingsDisplayGamma() != 100) return 40;
        key(window, 125, @"\uF701"); key(window, 124, @"\uF703");
        if (black_SettingsDisplayBrightness() != 1) return 41;
        key(window, 123, @"\uF702"); key(window, 123, @"\uF702");
        if (black_SettingsDisplayBrightness() != -1) return 42;
        key(window, 124, @"\uF703");
        key(window, 125, @"\uF701"); key(window, 124, @"\uF703");
        if (black_SettingsDisplayGamma() != 105) return 43;
        snapshot(window, [output stringByAppendingPathComponent:@"recomp-display-gamma.png"]);
        key(window, 123, @"\uF702");
        if (black_SettingsDisplayBrightness() != 0 || black_SettingsDisplayGamma() != 100) return 44;
        key(window, 125, @"\uF701"); key(window, 123, @"\uF702");
        if (black_SettingsAntiAliasing() != 1) return 30;
        key(window, 124, @"\uF703");
        key(window, 125, @"\uF701"); key(window, 124, @"\uF703");
        if (black_SettingsAniso() != 2) return 31;
        key(window, 123, @"\uF702");
        snapshot(window, [output stringByAppendingPathComponent:@"recomp-graphics.png"]);
        /* General: game language over the disc's banks, then a Russian panel. */
        key(window, 125, @"\uF701"); key(window, 124, @"\uF703");
        if (black_SettingsGameLanguage() != BLACK_LANG_FRENCH) return 80;
        key(window, 123, @"\uF702");
        if (black_SettingsGameLanguage() != BLACK_LANG_ENGLISH) return 81;
        key(window, 125, @"\uF701");
        for (int i = 0; i < 6; ++i) key(window, 124, @"\uF703");
        if (black_SettingsMenuLanguage() != 6) return 82;
        snapshot(window, [output stringByAppendingPathComponent:@"recomp-language-ru.png"]);
        /* General: the two texture rows, still in Russian. */
        key(window, 125, @"\uF701"); key(window, 125, @"\uF701"); /* HD Texture Size */
        if (black_SettingsTextureMaxSize() != 2048) return 87;
        key(window, 36, @"\r");
        if (black_SettingsTextureMaxSize() != 4096) return 88;
        key(window, 123, @"\uF702"); key(window, 123, @"\uF702");
        if (black_SettingsTextureMaxSize() != 1024) return 89;
        snapshot(window, [output stringByAppendingPathComponent:@"recomp-texture-size-ru.png"]);
        key(window, 124, @"\uF703");
        if (black_SettingsTextureMaxSize() != 2048) return 90;
        key(window, 126, @"\uF700"); /* Texture Replacement */
        if (black_SettingsTextureReplacement()) return 84;
        key(window, 36, @"\r");
        if (!black_SettingsTextureReplacement()) return 85;
        snapshot(window, [output stringByAppendingPathComponent:@"recomp-textures-ru.png"]);
        key(window, 123, @"\uF702");
        if (black_SettingsTextureReplacement()) return 86;
        key(window, 126, @"\uF700"); /* Menu Language */
        key(window, 123, @"\uF702"); key(window, 123, @"\uF702");
        if (black_SettingsMenuLanguage() != 4 || black_SettingsRenderScale() != 2) return 83;
        snapshot(window, [output stringByAppendingPathComponent:@"recomp-language-es.png"]);
        black_SettingsSetMenuLanguage(6);
        /* Back to the top menu, then Mods: films, then the mods. */
        key(window, 53, @"\e");
        key(window, 125, @"\uF701"); key(window, 125, @"\uF701");
        key(window, 36, @"\r");
        snapshot(window, [output stringByAppendingPathComponent:@"mods-ru.png"]);
        black_SettingsSetMenuLanguage(0); pump(.05);
        key(window, 36, @"\r"); /* Any Film: on by default, off, on again */
        if (black_SettingsSkipAnyFilm()) return 3;
        key(window, 36, @"\r");
        if (!black_SettingsSkipAnyFilm()) return 3;
        key(window, 125, @"\uF701"); key(window, 36, @"\r"); /* Start-up films */
        if (!black_SettingsSkipIntros()) return 3;
        key(window, 125, @"\uF701"); key(window, 36, @"\r"); /* Mission briefings */
        if (!black_SettingsSkipMissionIntros()) return 3;
        snapshot(window, [output stringByAppendingPathComponent:@"mods-films.png"]);
        key(window, 125, @"\uF701"); key(window, 124, @"\uF703"); /* God Mode */
        key(window, 125, @"\uF701"); key(window, 36, @"\r"); /* Medkits */
        if (!black_SettingsMod(BLACK_MOD_GOD_MODE) || !black_SettingsMod(BLACK_MOD_MEDKITS)) return 70;
        key(window, 123, @"\uF702");
        if (black_SettingsMod(BLACK_MOD_MEDKITS)) return 71;
        snapshot(window, [output stringByAppendingPathComponent:@"mods-player.png"]);
        for (int i = BLACK_MOD_MEDKITS; i < BLACK_MOD_UNLOCK_MISSIONS; ++i) key(window, 125, @"\uF701");
        key(window, 36, @"\r");
        if (!black_SettingsMod(BLACK_MOD_UNLOCK_MISSIONS)) return 72;
        snapshot(window, [output stringByAppendingPathComponent:@"mods-unlocks.png"]);
        page_defaults(window, PAGE_MODS, ROW_MOD_FIRST + BLACK_MOD_UNLOCK_MISSIONS);
        if (black_SettingsMod(BLACK_MOD_GOD_MODE) || black_SettingsMod(BLACK_MOD_UNLOCK_MISSIONS) ||
            black_SettingsSkipIntros() || black_SettingsRenderScale() != 2) return 74;
        key(window,53,@"\e"); /* the top menu */
        key(window,53,@"\e"); /* close */
        if(black_MenuIsOpen())return 4;
        key(window,122,@"\uF704");
        key(window,126,@"\uF700");key(window,36,@"\r"); /* Controls */
        int raw[6]={385,-128,-640,256,0,0},mapped[6]={385,128,-640,-256,0,0};
        black_ControllerPublish("Synthetic preview pad",1,raw,mapped,0);
        black_SettingsSetMouseSpeed(0,47);black_SettingsSetMouseSpeed(1,45);
        black_SettingsSetLookSpeed(0,63);black_SettingsSetLookSpeed(1,41);
        NSString *rootRows[] = {@"Input Device",@"Button Prompts",@"Button Bindings",@"Mouse and Aiming",@"Gamepad and Vibration"};
        for(int i=0;i<5;++i)if(button(window.contentView,rootRows[i]).hidden)return 101;
        if(!button(window.contentView,@"Horizontal Sensitivity").hidden || !button(window.contentView,@"Mouse Look").hidden)return 102;
        snapshot(window,[output stringByAppendingPathComponent:@"controls-home.png"]);
        [window setContentSize:NSMakeSize(640,480)];pump(.1);
        snapshot(window,[output stringByAppendingPathComponent:@"controls-home-small.png"]);
        [window setContentSize:NSMakeSize(960,600)];pump(.1);
        key(window,124,@"\uF703");if(black_SettingsInputDevice()!=BLACK_INPUT_GAMEPAD)return 103;
        key(window,124,@"\uF703");if(black_SettingsInputDevice()!=BLACK_INPUT_KEYBOARD)return 104;
        key(window,36,@"\r");if(black_SettingsInputDevice()!=BLACK_INPUT_AUTO)return 105;
        key(window,125,@"\uF701");key(window,36,@"\r");key(window,36,@"\r");
        if(black_SettingsButtonPrompts()!=BLACK_PROMPTS_PLAYSTATION)return 106;
        snapshot(window,[output stringByAppendingPathComponent:@"controls-device.png"]);
        key(window,125,@"\uF701");key(window,36,@"\r"); /* editor */
        key(window,124,@"\uF703"); /* edit gamepad, no gameplay device change */
        if(black_SettingsInputDevice()!=BLACK_INPUT_AUTO||button(window.contentView,@"Reload (A)").hidden)return 107;
        key(window,125,@"\uF701");key(window,36,@"\r");
        black_PadCapturePoll(1,1,1);pump(.03);if(black_SettingsPadBinding(0)!=0)return 108;
        black_PadCapturePoll(0,1,1);black_PadCapturePoll(4,1,1);pump(.05);
        if(black_SettingsPadBinding(0)!=2)return 109;
        black_PadCapturePoll(0,1,1);
        snapshot(window,[output stringByAppendingPathComponent:@"controller-bindings.png"]);
        black_InputSetKeyboardActive(0);pump(.05);black_InputSetKeyboardActive(1);pump(.05);
        if(button(window.contentView,@"Reload (A)").hidden)return 110; /* no automatic page switch */
        key(window,36,@"\r");key(window,53,@"\e");if(!black_MenuIsOpen())return 111;
        key(window,117,@"\uF728");if(black_SettingsPadBinding(0)!=-1)return 112;
        key(window,36,@"\r");black_PadCapturePoll(0,-1,0);pump(.05);
        if(black_SettingsPadBinding(0)!=-1)return 113;
        page_defaults(window,PAGE_PAD_BINDINGS,ROW_PAD_FIRST);
        if(black_SettingsPadBinding(0)!=0||black_SettingsButtonPrompts()!=BLACK_PROMPTS_PLAYSTATION||black_SettingsLookSpeed(0)!=63)return 114;
        key(window,53,@"\e"); /* root, bindings row restored */
        if(![[(NSButton *)window.firstResponder title] isEqual:@"Button Bindings"])return 115;
        key(window,36,@"\r"); /* editor selector */
        key(window,123,@"\uF702");key(window,125,@"\uF701"); /* keyboard forward */
        key(window,124,@"\uF703");key(window,36,@"\r");key(window,35,@"p");
        if(black_SettingsBinding(0,1)!=35||black_SettingsBinding(0,0)!=13)return 116;
        snapshot(window,[output stringByAppendingPathComponent:@"keyboard-bindings.png"]);
        key(window,36,@"\r");key(window,53,@"\e");if(black_SettingsBinding(0,1)!=35||!black_MenuIsOpen())return 117;
        key(window,36,@"\r");black_MenuCommand(BLACK_MENU_BACK);pump(.05);
        if(black_SettingsBinding(0,1)!=35||button(window.contentView,@"MOVE FORWARD").hidden)return 118;
        key(window,117,@"\uF728");if(black_SettingsBinding(0,1)!=-1)return 119;
        key(window,36,@"\r");
        NSEvent *bindMouse=[NSEvent mouseEventWithType:NSEventTypeRightMouseDown location:NSZeroPoint modifierFlags:0 timestamp:0 windowNumber:window.windowNumber context:nil eventNumber:0 clickCount:1 pressure:1];
        [NSApp postEvent:bindMouse atStart:NO];pump(.05);if(black_SettingsBinding(0,1)!=BLACK_MOUSE_RIGHT)return 120;
        page_defaults(window,PAGE_KEYS,ROW_BIND_FIRST);
        if(black_SettingsBinding(0,1)!=-1||black_SettingsMouseSpeed(0)!=47||black_SettingsLookSpeed(1)!=41)return 121;
        key(window,53,@"\e");key(window,125,@"\uF701");key(window,36,@"\r"); /* mouse */
        if(button(window.contentView,@"Mouse Look").hidden)return 122;
        key(window,124,@"\uF703"); /* select Y, no change */
        if(black_SettingsMouseSpeed(0)!=47||black_SettingsMouseSpeed(1)!=45)return 123;
        key(window,36,@"\r");key(window,124,@"\uF703");
        if(black_SettingsMouseSpeed(0)!=47||black_SettingsMouseSpeed(1)!=46||black_SettingsLookSpeed(1)!=41)return 124;
        key(window,53,@"\e");if(button(window.contentView,@"Mouse Look").hidden)return 125; /* finish axis editing */
        snapshot(window,[output stringByAppendingPathComponent:@"mouse-settings.png"]);
        [window setContentSize:NSMakeSize(640,480)];pump(.1);snapshot(window,[output stringByAppendingPathComponent:@"mouse-small.png"]);
        [window setContentSize:NSMakeSize(960,600)];pump(.1);
        page_defaults(window,PAGE_MOUSE,ROW_MOUSE_LOOK_PAIR);
        if(black_SettingsMouseSpeed(0)!=50||black_SettingsLookSpeed(0)!=63||black_SettingsButtonPrompts()!=BLACK_PROMPTS_PLAYSTATION)return 126;
        key(window,53,@"\e");key(window,125,@"\uF701");key(window,36,@"\r"); /* gamepad */
        key(window,36,@"\r");key(window,124,@"\uF703");key(window,36,@"\r");
        if(black_SettingsLookSpeed(0)!=64||black_SettingsLookSpeed(1)!=41||black_SettingsMouseSpeed(0)!=50)return 127;
        snapshot(window,[output stringByAppendingPathComponent:@"controller-look.png"]);
        page_defaults(window,PAGE_PAD,ROW_PAD_LOOK_PAIR);
        if(black_SettingsLookSpeed(0)!=50||black_SettingsButtonPrompts()!=BLACK_PROMPTS_PLAYSTATION)return 128;
        for(int i=0;i<5;++i)key(window,125,@"\uF701");key(window,36,@"\r"); /* advanced */
        key(window,125,@"\uF701");key(window,125,@"\uF701");key(window,124,@"\uF703");
        if(black_SettingsStickDeadzone(0)!=21)return 129;
        snapshot(window,[output stringByAppendingPathComponent:@"controller-zones.png"]);
        black_SettingsSetInputDevice(BLACK_INPUT_KEYBOARD);black_SettingsSetBinding(0,1,35);black_SettingsSetLookSpeed(0,61);
        page_defaults(window,PAGE_ADVANCED,ROW_LEFT_DZ);
        if(black_SettingsStickDeadzone(0)!=-1||black_SettingsLookSpeed(0)!=61||black_SettingsBinding(0,1)!=35||black_SettingsInputDevice()!=BLACK_INPUT_KEYBOARD)return 136;
        black_SettingsSetInputDevice(BLACK_INPUT_AUTO);
        for(int i=0;i<8;++i)key(window,125,@"\uF701");key(window,36,@"\r"); /* diagnostics */
        if(button(window.contentView,@"Measure Stick Drift").hidden)return 130;
        /* Black Box: two differing polls, then Enter on its row saves beside this preview's settings. */
        int16_t bb_zero[4]={0,0,0,0},bb_push[4]={-12000,4000,0,0};
        uint8_t bb_idle[8]={0},bb_press[8]={0};
        bb_press[2]=200;
        black_BoxRecord(0,bb_idle,bb_zero);usleep(20000);black_BoxRecord(1,bb_press,bb_push);
        if(button(window.contentView,@"Save Black Box").hidden)return 137;
        key(window,125,@"\uF701");key(window,36,@"\r");
        if(!black_BoxPath()[0]||access(black_BoxPath(),R_OK))return 138;
        snapshot(window,[output stringByAppendingPathComponent:@"controller-diagnostics.png"]);
        key(window,53,@"\e");if(![[(NSButton *)window.firstResponder title]isEqual:@"Controller Diagnostics"])return 131;
        key(window,53,@"\e");if(![[(NSButton *)window.firstResponder title]isEqual:@"Advanced Gamepad"])return 132;
        key(window,53,@"\e");if(![[(NSButton *)window.firstResponder title]isEqual:@"Gamepad and Vibration"])return 133;
        black_SettingsSetMenuLanguage(6);pump(.05);snapshot(window,[output stringByAppendingPathComponent:@"controls-home-ru.png"]);
        key(window,36,@"\r");snapshot(window,[output stringByAppendingPathComponent:@"controller-look-ru.png"]);
        black_SettingsSetMenuLanguage(0);pump(.05);
        key(window,53,@"\e");key(window,53,@"\e");key(window,53,@"\e");if(black_MenuIsOpen())return 134;
        key(window,122,@"\uF704");key(window,36,@"\r"); /* Controls */
        if(button(window.contentView,@"Input Device").hidden)return 135;
        key(window,53,@"\e");key(window,53,@"\e");
        for (int open = 0; open < 2; ++open) {
            if (open) { key(window, 122, @"\uF704"); }
            NSEvent *quit = [NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint
                modifierFlags:NSEventModifierFlagCommand timestamp:0 windowNumber:window.windowNumber
                context:nil characters:@"q" charactersIgnoringModifiers:@"q" isARepeat:NO keyCode:12];
            [NSApp postEvent:quit atStart:NO]; pump(.05);
            if (quit_requests != open + 1) return 17;
        }
        NSMenuItem *quit = [NSApp.mainMenu.itemArray.firstObject.submenu itemWithTitle:@"Quit BLACK"];
        [NSApp sendAction:quit.action to:quit.target from:quit];
        if (quit_requests != 3) return 18;
        [window performClose:nil];
        if (quit_requests != 4) return 19;
        black_MenuDetach(); [window close]; [window release];
        puts("Control hub: device/prompts first, editor scope, independent X/Y, scoped resets, capture/cancel/disconnect, back/focus, no auto page switch, RU/resize/quit: PASS");
    }
    return 0;
}
