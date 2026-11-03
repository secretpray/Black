/* First-run setup check: every page drawn to PNG, keyboard selection, and a
 * real install of your own disc folder or image through the setup's own flow,
 * plus a stop and resume of the copy. Writes only under <output>.
 *
 *   black-setup-preview <disc folder or image> <output> [europe.patch]
 */
#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#import "../../ports/macos/overrides/app_setup.h"
#include <stdio.h>

static void snapshot(NSView *view, NSString *path)
{
    [view displayIfNeeded];
    NSBitmapImageRep *rep = [view bitmapImageRepForCachingDisplayInRect:view.bounds];
    [view cacheDisplayInRect:view.bounds toBitmapImageRep:rep];
    [[rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:path atomically:YES];
}
static void key(NSWindow *window, unsigned short code)
{
    NSEvent *event = [NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint modifierFlags:0 timestamp:0
        windowNumber:window.windowNumber context:nil characters:@"" charactersIgnoringModifiers:@"" isARepeat:NO keyCode:code];
    [window sendEvent:event];
}
static int fail(const char *what) { fprintf(stderr, "FAIL: %s\n", what); return 1; }

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: %s <disc folder or image> <output> [europe.patch]\n", argv[0]); return 2; }
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
        NSURL *source = [NSURL fileURLWithPath:@(argv[1])], *output = [NSURL fileURLWithPath:@(argv[2])];
        NSURL *patch = argc > 3 ? [NSURL fileURLWithPath:@(argv[3])] :
            [NSURL fileURLWithPath:[@(__FILE__) stringByAppendingPathComponent:@"../../../src/common/europe.patch"].stringByStandardizingPath];
        NSFileManager *fm = NSFileManager.defaultManager;
        NSURL *data = [output URLByAppendingPathComponent:@"data"], *game = [data URLByAppendingPathComponent:@"Game"];
        [fm removeItemAtURL:data error:nil];
        [fm createDirectoryAtURL:data withIntermediateDirectories:YES attributes:nil error:nil];
        NSWindow *window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 960, 600) styleMask:NSWindowStyleMaskTitled
            backing:NSBackingStoreBuffered defer:NO];
        window.releasedWhenClosed = NO;
        NSView *view = black_SetupMakeView(game, patch);
        window.contentView = view;
        NSString *(^png)(NSString *) = ^(NSString *name) { return [output URLByAppendingPathComponent:[name stringByAppendingPathExtension:@"png"]].path; };

        /* Before the disc is read: the stand-in font. */
        black_SetupShow(view, BLACK_SETUP_DISC, nil, nil, nil, 0, 5);
        snapshot(view, png(@"1-disc"));
        black_SetupShow(view, BLACK_SETUP_DISC, nil, nil, nil, 0, .35);
        snapshot(view, png(@"1-disc-typing"));
        black_SetupShow(view, BLACK_SETUP_CHECKING, nil, nil, source.lastPathComponent.uppercaseString, 0, 5);
        snapshot(view, png(@"2-checking"));
        NSError *error = nil;
        BlackDiscInfo *info = black_InspectDisc(source, &error);
        if (!info) { fprintf(stderr, "%s\n", error.localizedDescription.UTF8String); return fail("inspect"); }
        printf("Inspect: %s, %lu files, %llu bytes, languages %s, fonts %lu/%lu bytes\n", info.region.UTF8String,
            (unsigned long)info.files.count, info.totalBytes, [info.languages componentsJoinedByString:@","].UTF8String,
            (unsigned long)info.bigFont.length, (unsigned long)info.smallFont.length);
        black_SetupShow(view, BLACK_SETUP_REVIEW, info, nil, nil, 0, 5);
        snapshot(view, png(@"3-review"));
        BlackInstallProgress *progress = [[[BlackInstallProgress alloc] init] autorelease];
        progress.bytesTotal = info.totalBytes; progress.bytesDone = info.totalBytes * 62 / 100;
        progress.filesTotal = info.files.count; progress.filesDone = info.files.count * 6 / 10;
        progress.currentFile = info.files[progress.filesDone][@"path"];
        black_SetupShow(view, BLACK_SETUP_COPYING, info, progress, nil, 0, 5);
        snapshot(view, png(@"4-copying"));
        black_SetupShow(view, BLACK_SETUP_FAILED, info, nil,
            @"UNSUPPORTED GAME REVISION. SELECT YOUR OWN USA OR SUPPORTED EUROPEAN ORIGINAL XBOX BLACK DISC.", 0, 5);
        snapshot(view, png(@"6-failed"));
        black_SetupShow(view, BLACK_SETUP_DONE, info, nil, nil, 0, 5);
        snapshot(view, png(@"7-done"));

        /* Keyboard: down moves the selection and wraps; the review offers two rows. */
        [window makeKeyAndOrderFront:nil]; [window makeFirstResponder:view];
        black_SetupShow(view, BLACK_SETUP_REVIEW, info, nil, nil, 0, 5);
        key(window, 125); snapshot(view, png(@"3-review-another"));
        key(window, 125);
        snapshot(view, png(@"3-review-wrapped"));
        NSData *a = [NSData dataWithContentsOfFile:png(@"3-review")], *b = [NSData dataWithContentsOfFile:png(@"3-review-another")];
        if ([a isEqual:b]) return fail("down arrow did not move the selection");

        /* A real install through the setup's flow, stopped part way and resumed. */
        __block BOOL stopped = NO;
        BlackInstallProgress *watch = [[[BlackInstallProgress alloc] init] autorelease];
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
            while (watch.bytesDone < info.totalBytes / 3) usleep(2000);
            watch.cancelled = YES;
        });
        if (black_InstallDisc(info, game, patch, watch, &error) || error.code != BLACK_INSTALL_CANCELLED) return fail("stop");
        uint64_t kept = black_PartialBytes(info, game);
        stopped = kept > 0 && ![fm fileExistsAtPath:game.path];
        printf("Stop: %llu of %llu bytes kept, game folder absent: %s\n", kept, info.totalBytes, stopped ? "yes" : "no");
        if (!stopped) return fail("partial copy");
        black_SetupShow(view, BLACK_SETUP_STOPPED, info, nil, nil, 0, 5);
        snapshot(view, png(@"5-stopped"));
        double start = CACurrentMediaTime();
        BlackSetupPage last = black_SetupDrive(view, source, 900);
        printf("Resume through the setup: page %d after %.1f s\n", last, CACurrentMediaTime() - start);
        if (last != BLACK_SETUP_DONE) return fail("install did not finish");
        if (!black_ValidateInstalledGame(game, &error)) return fail("installed executable");
        if ([fm fileExistsAtPath:[data URLByAppendingPathComponent:@".install-partial"].path]) return fail("partial folder left behind");
        snapshot(view, png(@"7-done-live"));
        [window close];
        puts("First-run setup: pages, keyboard selection, inspect, stop/resume, install and check: PASS");
    }
    return 0;
}
