/* Isolated native UI check. No global keyboard injection, guest, audio or save data. */
#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#include "../../ports/macos/overrides/port_menu.h"
#include "../../ports/macos/overrides/keyboard_mouse.h"
#include "../../ports/macos/overrides/controller_monitor.h"
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
        snapshot(window, [output stringByAppendingPathComponent:@"recomp-display.png"]);
        key(window, 124, @"\uF703");
        if (black_SettingsRenderScale() != 3) return 20;
        key(window, 123, @"\uF702");
        if (black_SettingsRenderScale() != 2) return 21;
        [window makeKeyAndOrderFront:nil]; pump(.1);
        NSButton *resolution = button(window.contentView, @"Render Scale");
        if (!resolution || resolution.hidden) return 25;
        click(window, resolution, (765.0 - 84) / 792 * resolution.bounds.size.width, resolution.bounds.size.height / 2);
        if (black_SettingsRenderScale() != 1) return 27;
        key(window, 124, @"\uF703");
        key(window, 125, @"\uF701"); key(window, 123, @"\uF702");
        if (black_SettingsUpscaler()) return 38;
        key(window, 124, @"\uF703");
        key(window, 125, @"\uF701"); key(window, 124, @"\uF703");
        if (black_SettingsFov() != 75) return 22;
        key(window, 123, @"\uF702");
        key(window, 125, @"\uF701"); key(window, 123, @"\uF702");
        if (black_SettingsAntiAliasing() != 1) return 30;
        key(window, 124, @"\uF703");
        key(window, 125, @"\uF701"); key(window, 124, @"\uF703");
        if (black_SettingsAniso() != 2) return 31;
        key(window, 123, @"\uF702");
        key(window, 125, @"\uF701"); key(window, 36, @"\r");
        key(window, 125, @"\uF701"); key(window, 36, @"\r");
        if (!black_SettingsSkipIntros() || !black_SettingsSkipMissionIntros()) return 3;
        snapshot(window, [output stringByAppendingPathComponent:@"recomp-general.png"]);
        key(window, 53, @"\e"); if (black_MenuIsOpen()) return 4;
        key(window, 122, @"\uF704");
        if (!black_MenuIsOpen() || !black_SettingsSkipIntros()) return 5;
        key(window, 14, @"e"); key(window, 14, @"e");
        int raw[6] = {385, -128, -640, 256, 0, 0}, mapped[6] = {385,128,-640,-256,0,0};
        black_ControllerPublish("Synthetic preview pad", 1, raw, mapped, 0);
        snapshot(window, [output stringByAppendingPathComponent:@"controller-look.png"]);
        for (int i = 0; i < 4; ++i) {
            key(window, 124, @"\uF703");
            if (black_SettingsLookSpeed(i) != 51) return 50;
            key(window, 123, @"\uF702"); key(window, 125, @"\uF701");
        }
        key(window, 124, @"\uF703");
        if (!black_SettingsInvertX()) return 51;
        key(window, 123, @"\uF702"); key(window, 125, @"\uF701");
        key(window, 124, @"\uF703");
        if (!black_SettingsHoldAim()) return 59;
        key(window, 123, @"\uF702"); key(window, 125, @"\uF701");
        key(window, 123, @"\uF702");
        if (black_SettingsAimAssist()) return 60;
        snapshot(window, [output stringByAppendingPathComponent:@"controller-aim.png"]);
        key(window, 124, @"\uF703"); key(window, 125, @"\uF701");
        for (int i = 0; i < 12; ++i) key(window, 123, @"\uF702");
        if (black_SettingsStickDeadzone(0) != 8) return 41;
        key(window, 125, @"\uF701");
        for (int i = 0; i < 16; ++i) key(window, 123, @"\uF702");
        if (black_SettingsStickDeadzone(1) != 4) return 42;
        key(window, 125, @"\uF701");
        key(window, 124, @"\uF703"); key(window, 124, @"\uF703");
        if (black_SettingsTriggerDeadzone() != 2) return 43;
        snapshot(window, [output stringByAppendingPathComponent:@"controller-deadzones.png"]);
        [window setContentSize:NSMakeSize(640,480)]; pump(.1);
        snapshot(window, [output stringByAppendingPathComponent:@"controller-small.png"]);
        [window setContentSize:NSMakeSize(960,600)]; pump(.1);
        key(window, 125, @"\uF701"); key(window, 123, @"\uF702");
        if (black_SettingsVibration()) return 44;
        key(window, 125, @"\uF701"); key(window, 123, @"\uF702");
        if (black_SettingsRumbleIntensity() != 90) return 52;
        key(window, 125, @"\uF701"); key(window, 123, @"\uF702");
        if (black_SettingsDualSenseHid()) return 45;
        snapshot(window, [output stringByAppendingPathComponent:@"controller-driver.png"]);
        key(window, 125, @"\uF701"); key(window, 36, @"\r");
        if (black_ControllerMonitor().calibration != BLACK_CAL_MEASURING) return 46;
        snapshot(window, [output stringByAppendingPathComponent:@"controller-measuring.png"]);
        for (unsigned t = 0; t <= 2100; t += 10) black_ControllerPublish("Synthetic preview pad", 1, raw, mapped, t);
        pump(.1);
        if (black_SettingsStickDeadzone(0) != 4 || black_SettingsStickDeadzone(1) != 4) return 47;
        snapshot(window, [output stringByAppendingPathComponent:@"controller-measured.png"]);
        black_ControllerPublish(NULL, -1, NULL, NULL, 2200);
        key(window, 36, @"\r");
        if (black_ControllerMonitor().calibration != BLACK_CAL_DISCONNECTED || black_SettingsStickDeadzone(0) != 4) return 48;
        key(window, 125, @"\uF701"); key(window, 36, @"\r");
        if (black_SettingsStickDeadzone(0) != -1 || black_SettingsStickDeadzone(1) != -1 ||
            black_SettingsTriggerDeadzone() || !black_SettingsVibration() || !black_SettingsDualSenseHid() ||
            black_SettingsRumbleIntensity() != 100 || black_SettingsInvertX() ||
            black_SettingsHoldAim() || !black_SettingsAimAssist() || black_SettingsLookSpeed(0) != 50 ||
            black_SettingsRenderScale() != 2 || black_SettingsFov() != 70 ||
            !black_SettingsSkipIntros() || !black_SettingsSkipMissionIntros()) return 49;
        NSButton *defaults = button(window.contentView, @"Defaults");
        if (!defaults) return 53;
        black_SettingsSetStickDeadzone(0, 7);
        click(window, defaults, defaults.bounds.size.width / 2, defaults.bounds.size.height / 2);
        if (black_SettingsStickDeadzone(0) != -1 || !black_SettingsSkipIntros()) return 54;
        key(window, 12, @"q");
        if (!black_MenuIsOpen() || quit_requests) return 33;
        NSButton *controller = button(window.contentView, @"Controller");
        if (!controller) return 55;
        click(window, controller, controller.bounds.size.width / 2, controller.bounds.size.height / 2);
        if (button(window.contentView, @"Horizontal Sensitivity").hidden) return 56;
        NSButton *back = button(window.contentView, @"Back");
        if (!back) return 57;
        click(window, back, back.bounds.size.width / 2, back.bounds.size.height / 2);
        if (black_MenuIsOpen()) return 58;
        key(window,122,@"\uF704"); key(window,12,@"q"); /* Controller -> Mouse and Keyboard */
        snapshot(window,[output stringByAppendingPathComponent:@"mouse-settings.png"]);
        key(window,124,@"\uF703");
        if (black_SettingsMouseSpeed(0)!=51) return 61;
        key(window,123,@"\uF702");
        for(int i=0;i<8;++i) key(window,125,@"\uF701");
        key(window,124,@"\uF703"); key(window,36,@"\r"); key(window,35,@"p");
        if (black_SettingsBinding(0,1)!=35 || black_SettingsBinding(0,0)!=13) return 62;
        snapshot(window,[output stringByAppendingPathComponent:@"keyboard-bindings.png"]);
        key(window,36,@"\r"); key(window,53,@"\e"); /* cancel, keep binding */
        if (!black_MenuIsOpen() || black_SettingsBinding(0,1)!=35) return 63;
        key(window,36,@"\r"); black_MenuCommand(BLACK_MENU_BACK); pump(.1);
        if (!black_MenuIsOpen() || black_SettingsBinding(0,1)!=35) return 67;
        key(window,117,@"\uF728");
        if (black_SettingsBinding(0,1)!=-1) return 64;
        key(window,36,@"\r");
        NSEvent *bindMouse=[NSEvent mouseEventWithType:NSEventTypeRightMouseDown location:NSZeroPoint modifierFlags:0
            timestamp:0 windowNumber:window.windowNumber context:nil eventNumber:0 clickCount:1 pressure:1];
        [NSApp postEvent:bindMouse atStart:NO]; pump(.1);
        if (black_SettingsBinding(0,1)!=BLACK_MOUSE_RIGHT) return 65;
        snapshot(window,[output stringByAppendingPathComponent:@"keyboard-bound-mouse.png"]);
        [window setContentSize:NSMakeSize(640,480)]; pump(.1);
        snapshot(window,[output stringByAppendingPathComponent:@"keyboard-small.png"]);
        [window setContentSize:NSMakeSize(960,600)]; pump(.1);
        defaults=button(window.contentView,@"Defaults");
        click(window,defaults,defaults.bounds.size.width/2,defaults.bounds.size.height/2);
        if(black_SettingsBinding(0,1)!=-1 || black_SettingsBinding(0,0)!=13 || black_SettingsMouseSpeed(0)!=50 || !black_SettingsSkipIntros()) return 66;
        key(window,53,@"\e");
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
        puts("TGP native UI: recomp/controller/mouse pages, dual key slots, capture/cancel/clear/mouse bind/defaults, resize/quit: PASS");
    }
    return 0;
}
