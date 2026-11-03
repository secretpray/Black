/* First-run setup drawn like BLACK's menu pages (black_ui.h): rails, a heading
 * typed in with a block cursor, green-grey rows and the selector bar. Layout and
 * flow follow portingpete's BLACK launcher (MIT) and the Burnout Revenge setup;
 * the code is native AppKit. Until the disc is read, DIN Condensed stands in
 * for the game's fonts; then its own Big/Small are read from the player's copy. */
#import "app_setup.h"
#import "black_ui.h"
#include "port_menu.h"
#include "game_language.h"
#import "game_translation.h"
#include <sys/sysctl.h>
#import <GameController/GameController.h>
#import <QuartzCore/QuartzCore.h>

static NSString *const stand_in = @"DINCondensed-Bold";
static BlackUIFont big_font, small_font;
static void text(NSString *s, CGFloat x, CGFloat y, CGFloat size, unsigned rgb, CGFloat alpha, int large)
{ black_ui_text(large ? &big_font : &small_font, stand_in, s, x, y, size, rgb, alpha); }
static CGFloat width_of(NSString *s, CGFloat size, int large)
{ return black_ui_text_width(large ? &big_font : &small_font, stand_in, s, size); }
static void over(NSRect r, unsigned rgb, CGFloat alpha)
{ [black_ui_color(rgb, alpha) setFill]; NSRectFillUsingOperation(r, NSCompositingOperationSourceOver); }
static NSArray<NSString *> *wrap(NSString *s, CGFloat width, CGFloat size)
{
    NSMutableArray *lines = [NSMutableArray array];
    for (NSString *paragraph in [s componentsSeparatedByString:@"\n"]) {
        NSMutableString *line = [NSMutableString string];
        for (NSString *word in [paragraph componentsSeparatedByString:@" "]) {
            NSString *next = line.length ? [line stringByAppendingFormat:@" %@", word] : word;
            if (line.length && width_of(next, size, 0) > width) { [lines addObject:[[line copy] autorelease]]; [line setString:word]; }
            else [line setString:next];
        }
        [lines addObject:line];
    }
    return lines;
}
static NSString *bytes(uint64_t n)
{ return [NSByteCountFormatter stringFromByteCount:(long long)n countStyle:NSByteCountFormatterCountStyleFile].uppercaseString; }
static NSString *language_name(NSString *suffix)
{
    NSDictionary *names = @{@"uk":@"ENGLISH", @"us":@"ENGLISH", @"fr":@"FRANÇAIS", @"ge":@"DEUTSCH", @"sp":@"ESPAÑOL",
                            @"it":@"ITALIANO", @"ja":@"JAPANESE", @"ko":@"KOREAN", @"du":@"NEDERLANDS"};
    return names[suffix.lowercaseString] ?: suffix.uppercaseString;
}
static NSString *languages(BlackDiscInfo *info)
{
    NSMutableOrderedSet *names = [NSMutableOrderedSet orderedSetWithObject:@"ENGLISH"];
    for (NSString *suffix in info.languages) [names addObject:language_name(suffix)];
    return [names.array componentsJoinedByString:@", "];
}
static double ease(double t) { t = t < 0 ? 0 : t > 1 ? 1 : t; return 1 - pow(1 - t, 3); }
static BOOL still(void) { return NSWorkspace.sharedWorkspace.accessibilityDisplayShouldReduceMotion; }
static const char *const page_names[] = {"Disc", "Checking", "Review", "Copying", "Stopped", "Failed", "Language", "Graphics", "Done"};
/* Game languages as port_settings numbers them, with each one's string bank suffixes. */
static NSString *const game_language_names[BLACK_GAME_LANGUAGES] = {@"ENGLISH", @"FRANÇAIS", @"DEUTSCH", @"ESPAÑOL", @"ITALIANO", @"РУССКИЙ"};
static NSString *const menu_language_names[] = {@"AUTO", @"ENGLISH", @"FRANÇAIS", @"DEUTSCH", @"ESPAÑOL", @"ITALIANO", @"РУССКИЙ"};
/* A language the game can show: its bank on the disc, or Russian built from the
 * disc's English text and the translation the app carries. */
static BOOL on_disc(BlackDiscInfo *info, int language)
{
    if (language == BLACK_LANG_RUSSIAN) return black_RussianStrings() != nil;
    NSArray *suffixes = @[@[@"UK", @"US"], @[@"Fr"], @[@"Ge"], @[@"Sp"], @[@"It"]][language];
    for (NSString *have in info.languages)
        for (NSString *want in suffixes) if ([have caseInsensitiveCompare:want] == NSOrderedSame) return YES;
    return language == BLACK_LANG_ENGLISH;
}
static int languages_count(BlackDiscInfo *info)
{ int n = 0; for (int i = 0; i < BLACK_GAME_LANGUAGES; ++i) n += on_disc(info, i); return n; }
static NSString *this_mac(void)
{
    char brand[128] = ""; size_t length = sizeof brand; uint64_t memory = 0; size_t size = sizeof memory;
    sysctlbyname("machdep.cpu.brand_string", brand, &length, NULL, 0);
    sysctlbyname("hw.memsize", &memory, &size, NULL, 0);
    return [NSString stringWithFormat:@"%@, %llu GB", @(brand).uppercaseString, memory >> 30];
}
/* The defaults: measured on an M1 Pro at the game's 30 frames a second (GRAPHICS). */
enum { REC_SCALE = 2, REC_AA = 2, REC_UPSCALER = 1, REC_AF = 0, REC_FOV = 70 };
static const int af_levels[] = {0, 2, 4, 8, 16};

typedef struct { NSString *label, *help, *tag; SEL action; BOOL enabled; NSString *value; } SetupRow;

@interface BlackSetupView : NSView {
@public
    BlackSetupPage page;
    int selected, finished; /* finished: 1 play, -1 quit */
    double started, frozen_age, last_tick, sample_time, speed, pad_next;
    CGFloat selector_y;
    BOOL dragging, pad_held;
    unsigned generation;
    uint64_t partial, free_space, sample_bytes;
    BlackDiscInfo *info;
    BlackInstallProgress *progress;
    NSString *message;
    NSURL *game, *patch;
}
- (void)tick;
- (void)inspect:(NSURL *)source;
- (void)install:(id)sender;
@end

@implementation BlackSetupView
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (instancetype)initWithFrame:(NSRect)frame
{
    if ((self = [super initWithFrame:frame])) {
        frozen_age = -1; started = CACurrentMediaTime(); selector_y = -1;
        [self registerForDraggedTypes:@[NSPasteboardTypeFileURL]];
        [self addTrackingArea:[[[NSTrackingArea alloc] initWithRect:NSZeroRect options:NSTrackingMouseMoved |
            NSTrackingActiveAlways | NSTrackingInVisibleRect owner:self userInfo:nil] autorelease]];
    }
    return self;
}
- (void)dealloc { [info release]; [progress release]; [message release]; [game release]; [patch release]; [super dealloc]; }
- (double)age { return frozen_age >= 0 ? frozen_age : still() ? 60 : CACurrentMediaTime() - started; }
- (void)go:(BlackSetupPage)next
{
    fprintf(stderr, "[SETUP] page %s -> %s\n", page_names[page], page_names[next]);
    page = next; selected = 0; started = CACurrentMediaTime(); frozen_age = -1;
    [self setNeedsDisplay:YES];
}
- (void)useFonts
{
    black_ui_free_font(&big_font); black_ui_free_font(&small_font);
    black_ui_load_font(&big_font, info.bigFont); black_ui_load_font(&small_font, info.smallFont);
    fprintf(stderr, "[SETUP] game fonts from the disc: %s\n", big_font.cap > 0 && small_font.cap > 0 ? "yes" : "no, stand-in");
}

/* ---- the pages' rows ---- */
- (int)rows:(SetupRow *)r
{
    int n = 0;
    switch (page) {
    case BLACK_SETUP_DISC:
        r[n++] = (SetupRow){@"CHOOSE A DISC IMAGE", @"AN ISO OF YOUR OWN ORIGINAL XBOX DISC: A FULL REDUMP IMAGE OR AN XISO.", @"ISO", @selector(chooseImage:), YES};
        r[n++] = (SetupRow){@"CHOOSE A FOLDER", @"THE DISC'S FILES IN A FOLDER, WITH DEFAULT.XBE AT ITS TOP.", @"FOLDER", @selector(chooseFolder:), YES};
        r[n++] = (SetupRow){@"QUIT", @"CLOSES SETUP. NOTHING IS COPIED OR CHANGED.", nil, @selector(quit:), YES};
        break;
    case BLACK_SETUP_CHECKING:
        r[n++] = (SetupRow){@"BACK", @"STOPS WAITING FOR THIS COPY AND CHOOSES ANOTHER.", nil, @selector(back:), YES};
        break;
    case BLACK_SETUP_REVIEW: {
        BOOL room = free_space + partial >= info.totalBytes + 64ull * 1024 * 1024;
        r[n++] = (SetupRow){partial ? @"CONTINUE COPYING" : @"INSTALL",
            room ? (partial ? @"CARRIES ON FROM THE FILES ALREADY COPIED. EACH FILE IS COPIED WHOLE OR AGAIN." :
                              @"COPIES THE GAME INTO YOUR LIBRARY. A FEW MINUTES; YOU CAN STOP AT ANY TIME.") :
                   @"NOT ENOUGH FREE SPACE ON THIS MAC. FREE SOME SPACE, THEN CHOOSE THIS COPY AGAIN.",
            nil, @selector(install:), room};
        r[n++] = (SetupRow){@"CHOOSE ANOTHER COPY", @"BACK TO THE CHOICE OF DISC IMAGE OR FOLDER.", nil, @selector(back:), YES};
        break;
    }
    case BLACK_SETUP_COPYING:
        r[n++] = (SetupRow){@"STOP", @"STOPS AFTER THE CURRENT PIECE. COPIED FILES ARE KEPT, AND THE NEXT INSTALL CARRIES ON FROM THEM.", nil, @selector(stop:), YES};
        break;
    case BLACK_SETUP_STOPPED:
        r[n++] = (SetupRow){@"CONTINUE COPYING", @"CARRIES ON FROM THE FILES ALREADY COPIED.", nil, @selector(install:), YES};
        r[n++] = (SetupRow){@"CHOOSE ANOTHER COPY", @"THE PARTIAL COPY STAYS UNTIL ANOTHER COPY IS INSTALLED.", nil, @selector(back:), YES};
        r[n++] = (SetupRow){@"QUIT", @"THE PARTIAL COPY IS KEPT FOR THE NEXT TIME YOU OPEN BLACK.", nil, @selector(quit:), YES};
        break;
    case BLACK_SETUP_FAILED:
        r[n++] = (SetupRow){@"CHOOSE ANOTHER COPY", @"BACK TO THE CHOICE OF DISC IMAGE OR FOLDER. YOUR COPY WAS NOT CHANGED.", nil, @selector(back:), YES};
        r[n++] = (SetupRow){@"QUIT", @"CLOSES SETUP.", nil, @selector(quit:), YES};
        break;
    case BLACK_SETUP_LANGUAGE:
        r[n++] = (SetupRow){@"GAME TEXT", black_SettingsGameLanguage() == BLACK_LANG_RUSSIAN ?
            @"RUSSIAN TEXT AND CYRILLIC LETTERS ARE ADDED TO YOUR COPY WHEN THE GAME STARTS. NOTHING TO DO BY HAND." :
            languages_count(info) > 1 ?
            @"TEXT AND MENUS OF THE GAME ITSELF, FROM YOUR DISC'S STRING BANKS. LEFT / RIGHT CHANGES IT." :
            @"THIS DISC HAS ENGLISH TEXT ONLY. THE EUROPEAN DISC ADDS FRENCH, GERMAN, SPANISH AND ITALIAN.",
            nil, @selector(forward:), YES, game_language_names[black_SettingsGameLanguage()]};
        r[n++] = (SetupRow){@"SETTINGS MENU", @"LANGUAGE OF THE RECOMP SETTINGS PANEL. AUTO FOLLOWS THE GAME TEXT; RUSSIAN IS FOR THE PANEL ONLY.",
            nil, @selector(forward:), YES, menu_language_names[black_SettingsMenuLanguage()]};
        r[n++] = (SetupRow){@"CONTINUE", @"ON TO GRAPHICS. BOTH CAN BE CHANGED LATER WITH CMD+, OR F1.", nil, @selector(next:), YES};
        break;
    case BLACK_SETUP_GRAPHICS: {
        int scale = black_SettingsRenderScale(), aa = black_SettingsAntiAliasing(), af = black_SettingsAniso();
        r[n++] = (SetupRow){@"RENDER SCALE", scale == 1 ? @"1X SENDS EVERY FRAME BACK FROM THE GPU AND RUNS SLOWER THAN 2X." :
            [NSString stringWithFormat:@"THE GAME DRAWN AT %d X %d. HIGHER IS SHARPER AND SLOWER; 2X HOLDS 30 FPS ON AN M1 PRO.", 640 * scale, 480 * scale],
            nil, @selector(forward:), YES, [NSString stringWithFormat:@"%dX", scale]};
        r[n++] = (SetupRow){@"ANTI-ALIASING", @"SMOOTHS JAGGED EDGES. SMAA IS SHARPER, FXAA IS CHEAPER.", nil, @selector(forward:), YES,
            aa == 2 ? @"SMAA" : aa == 1 ? @"FXAA" : @"OFF"};
        r[n++] = (SetupRow){@"UPSCALING", @"METALFX SHARPENS THE RENDER SCALE UP TO THE WINDOW'S PIXELS.", nil, @selector(forward:), YES,
            black_SettingsUpscaler() ? @"METALFX" : @"OFF"};
        r[n++] = (SetupRow){@"TEXTURE FILTERING", @"SHARPER FLOORS AND WALLS AT AN ANGLE. GAME KEEPS THE ORIGINAL FILTERING.", nil, @selector(forward:), YES,
            af ? [NSString stringWithFormat:@"%dX", af] : @"GAME"};
        r[n++] = (SetupRow){@"FIELD OF VIEW", @"CAMERA ANGLE IN DEGREES. ORIGINAL: 70.", nil, @selector(forward:), YES,
            [NSString stringWithFormat:@"%d", black_SettingsFov()]};
        r[n++] = (SetupRow){@"USE RECOMMENDED", @"2X, SMAA, METALFX, THE GAME'S FILTERING AND 70 DEGREES: 30 FPS ON AN M1 PRO.", nil, @selector(recommended:), YES};
        r[n++] = (SetupRow){@"CONTINUE", @"ALL OF THESE CHANGE LATER IN SETTINGS AND APPLY ON THE NEXT LAUNCH.", nil, @selector(next:), YES};
        break;
    }
    case BLACK_SETUP_DONE:
        r[n++] = (SetupRow){@"PLAY", @"STARTS BLACK. SETTINGS: CMD+, OR F1 DURING PLAY.", nil, @selector(play:), YES};
        r[n++] = (SetupRow){@"QUIT", @"THE GAME STAYS INSTALLED; OPEN BLACK AGAIN TO PLAY.", nil, @selector(quit:), YES};
        break;
    }
    return n;
}
- (CGFloat)rowsTop
{
    switch (page) {
    case BLACK_SETUP_REVIEW: return 336;
    case BLACK_SETUP_LANGUAGE: return 236;
    case BLACK_SETUP_GRAPHICS: return 210;
    case BLACK_SETUP_COPYING: return 316;
    case BLACK_SETUP_DISC: return 300;
    default: return 276;
    }
}
- (CGFloat)pitch { return page == BLACK_SETUP_GRAPHICS ? 34 : 40; }
- (CGFloat)rowY:(int)i { return self.rowsTop + i * self.pitch; }

/* ---- actions ---- */
- (void)chooseImage:(id)sender { (void)sender; [self choose:NO]; }
- (void)chooseFolder:(id)sender { (void)sender; [self choose:YES]; }
- (void)choose:(BOOL)folder
{
    NSOpenPanel *panel = NSOpenPanel.openPanel;
    panel.canChooseFiles = !folder; panel.canChooseDirectories = folder; panel.allowsMultipleSelection = NO;
    panel.prompt = @"Use"; panel.message = folder ? @"Choose the folder with your BLACK disc's files." : @"Choose your BLACK disc image (.iso).";
    if ([panel runModal] == NSModalResponseOK) [self inspect:panel.URL];
}
- (void)inspect:(NSURL *)source
{
    unsigned ticket = ++generation;
    [message release]; message = [source.lastPathComponent.uppercaseString retain];
    [self go:BLACK_SETUP_CHECKING];
    NSURL *destination = game;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{ @autoreleasepool {
        NSError *error = nil;
        BlackDiscInfo *found = black_InspectDisc(source, &error);
        uint64_t copied = found ? black_PartialBytes(found, destination) : 0;
        uint64_t room = black_FreeSpace(destination.URLByDeletingLastPathComponent);
        NSString *problem = [error.localizedDescription ?: @"The selected copy could not be read." retain];
        [found retain];
        dispatch_async(dispatch_get_main_queue(), ^{
            if (ticket == generation && page == BLACK_SETUP_CHECKING) {
                if (found) {
                    [info release]; info = [found retain]; partial = copied; free_space = room;
                    fprintf(stderr, "[SETUP] checked %s: %s, %lu files, %llu bytes, languages %s, partial %llu, free %llu\n",
                        source.lastPathComponent.UTF8String, found.region.UTF8String, (unsigned long)found.files.count,
                        found.totalBytes, [found.languages componentsJoinedByString:@","].UTF8String, copied, room);
                    [self useFonts]; [self go:BLACK_SETUP_REVIEW];
                } else {
                    [message release]; message = [problem.uppercaseString retain];
                    fprintf(stderr, "[SETUP] check failed: %s\n", problem.UTF8String);
                    [self go:BLACK_SETUP_FAILED];
                }
            }
            [found release]; [problem release];
        });
    }});
}
- (void)install:(id)sender
{
    (void)sender;
    if (!info) return;
    [progress release]; progress = [[BlackInstallProgress alloc] init];
    sample_time = 0; speed = 0;
    [self go:BLACK_SETUP_COPYING];
    BlackDiscInfo *disc = info; BlackInstallProgress *watch = progress; NSURL *destination = game, *europe = patch;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{ @autoreleasepool {
        NSError *error = nil;
        BOOL ok = black_InstallDisc(disc, destination, europe, watch, &error);
        BOOL stopped = !ok && error.code == BLACK_INSTALL_CANCELLED;
        uint64_t copied = stopped ? black_PartialBytes(disc, destination) : 0;
        NSString *problem = [error.localizedDescription ?: @"The game could not be copied." retain];
        dispatch_async(dispatch_get_main_queue(), ^{
            fprintf(stderr, "[SETUP] copy %s: %llu of %llu bytes, %lu of %lu files\n", ok ? "complete" : stopped ? "stopped" : "failed",
                watch.bytesDone, watch.bytesTotal, (unsigned long)watch.filesDone, (unsigned long)watch.filesTotal);
            if (ok) [self enterLanguage];
            else if (stopped) { partial = copied; [self go:BLACK_SETUP_STOPPED]; }
            else { [message release]; message = [problem.uppercaseString retain]; [self go:BLACK_SETUP_FAILED]; }
            [problem release];
        });
    }});
}
- (void)forward:(id)sender { (void)sender; [self adjust:1]; }
/* Left / right on a value row; each change is saved at once, as in the settings panel. */
- (void)adjust:(int)delta
{
    if (page == BLACK_SETUP_LANGUAGE && selected == 0) {
        int language = black_SettingsGameLanguage();
        for (int i = 0; i < BLACK_GAME_LANGUAGES; ++i) {
            language = (language + delta + BLACK_GAME_LANGUAGES) % BLACK_GAME_LANGUAGES;
            if (on_disc(info, language)) break;
        }
        black_SettingsSetGameLanguage(language);
    } else if (page == BLACK_SETUP_LANGUAGE && selected == 1) {
        int count = (int)(sizeof menu_language_names / sizeof menu_language_names[0]);
        black_SettingsSetMenuLanguage((black_SettingsMenuLanguage() + delta + count) % count);
    } else if (page == BLACK_SETUP_GRAPHICS) {
        switch (selected) {
        case 0: black_SettingsSetRenderScale((black_SettingsRenderScale() - 1 + delta + 4) % 4 + 1); break;
        case 1: black_SettingsSetAntiAliasing((black_SettingsAntiAliasing() + delta + 3) % 3); break;
        case 2: black_SettingsSetUpscaler(!black_SettingsUpscaler()); break;
        case 3: {
            int at = 0; while (at < 4 && af_levels[at] != black_SettingsAniso()) ++at;
            black_SettingsSetAniso(af_levels[(at + delta + 5) % 5]); break;
        }
        case 4: black_SettingsSetFov(MAX(45, MIN(140, (black_SettingsFov() + delta * 5) / 5 * 5))); break;
        default: return;
        }
    } else return;
    [self logSettings:@"changed"];
    [self setNeedsDisplay:YES];
}
- (void)logSettings:(NSString *)why
{
    fprintf(stderr, "[SETUP] settings %s: game_language=%d menu_language=%d render_scale=%d anti_aliasing=%d upscaler=%d anisotropic=%d fov=%d saved=%d\n",
        why.UTF8String, black_SettingsGameLanguage(), black_SettingsMenuLanguage(), black_SettingsRenderScale(), black_SettingsAntiAliasing(),
        black_SettingsUpscaler(), black_SettingsAniso(), black_SettingsFov(), !black_SettingsSaveFailed());
}
- (void)recommended:(id)sender
{
    (void)sender;
    black_SettingsSetRenderScale(REC_SCALE); black_SettingsSetAntiAliasing(REC_AA); black_SettingsSetUpscaler(REC_UPSCALER);
    black_SettingsSetAniso(REC_AF); black_SettingsSetFov(REC_FOV);
    [self logSettings:@"recommended"];
}
- (void)next:(id)sender
{
    (void)sender;
    [self go:page == BLACK_SETUP_LANGUAGE ? BLACK_SETUP_GRAPHICS : BLACK_SETUP_DONE];
}
/* A fresh install starts the game in the Mac's own language when the disc has it. */
- (void)enterLanguage
{
    const char *settings = getenv("BLACK_SETTINGS_FILE");
    BOOL fresh = !settings || ![NSFileManager.defaultManager fileExistsAtPath:@(settings)];
    if (!on_disc(info, black_SettingsGameLanguage())) black_SettingsSetGameLanguage(BLACK_LANG_ENGLISH);
    else if (fresh) {
        NSArray *codes = @[@"en", @"fr", @"de", @"es", @"it", @"ru"];
        for (NSString *preferred in NSLocale.preferredLanguages) {
            NSUInteger language = [codes indexOfObject:[preferred substringToIndex:MIN(2u, preferred.length)]];
            if (language != NSNotFound && on_disc(info, (int)language)) { black_SettingsSetGameLanguage((int)language); break; }
        }
    }
    [self logSettings:fresh ? @"first" : @"kept"];
    [self go:BLACK_SETUP_LANGUAGE];
}
- (void)stop:(id)sender { (void)sender; progress.cancelled = YES; fprintf(stderr, "[SETUP] stop requested\n"); }
- (void)back:(id)sender
{
    (void)sender;
    if (page == BLACK_SETUP_COPYING) { [self stop:nil]; return; }
    if (page == BLACK_SETUP_DISC || page == BLACK_SETUP_LANGUAGE) return;
    if (page == BLACK_SETUP_GRAPHICS) { [self go:BLACK_SETUP_LANGUAGE]; return; }
    if (page == BLACK_SETUP_DONE) { [self go:BLACK_SETUP_GRAPHICS]; return; }
    ++generation; [self go:BLACK_SETUP_DISC];
}
- (void)quit:(id)sender { (void)sender; if (page == BLACK_SETUP_COPYING) [self stop:nil]; else finished = -1; }
- (void)play:(id)sender { (void)sender; finished = 1; }
- (void)activate
{
    SetupRow r[8]; int n = [self rows:r];
    if (selected < n && r[selected].enabled) { [self performSelector:r[selected].action withObject:nil]; [self setNeedsDisplay:YES]; }
    else NSBeep();
}
- (void)step:(int)delta
{
    SetupRow r[8]; int n = [self rows:r];
    if (n) selected = (selected + delta + n) % n;
    [self setNeedsDisplay:YES];
}

/* ---- input ---- */
- (NSPoint)logical:(NSPoint)p
{
    NSSize size = self.bounds.size; CGFloat scale = MIN(size.width / 960, size.height / 600);
    return NSMakePoint((p.x - (size.width - 960 * scale) / 2) / scale, (p.y - (size.height - 600 * scale) / 2) / scale);
}
- (int)rowAt:(NSPoint)window
{
    NSPoint p = [self logical:[self convertPoint:window fromView:nil]];
    SetupRow r[8]; int n = [self rows:r];
    for (int i = 0; i < n; ++i) if (p.x >= 84 && p.x <= 876 && p.y >= [self rowY:i] && p.y < [self rowY:i] + self.pitch - 6) return i;
    return -1;
}
- (void)mouseMoved:(NSEvent *)event
{ int row = [self rowAt:event.locationInWindow]; if (row >= 0 && row != selected) { selected = row; [self setNeedsDisplay:YES]; } }
- (void)mouseDown:(NSEvent *)event
{
    int row = [self rowAt:event.locationInWindow];
    if (row < 0) return;
    selected = row;
    SetupRow r[8]; [self rows:r];
    /* On a value row the left arrow steps back; anywhere else steps on. */
    if (r[row].value && [self logical:[self convertPoint:event.locationInWindow fromView:nil]].x < 735) [self adjust:-1];
    else [self activate];
}
- (void)keyDown:(NSEvent *)event
{
    unsigned short code = event.keyCode;
    if (event.modifierFlags & NSEventModifierFlagCommand) {
        if (code == 31 && (page == BLACK_SETUP_DISC || page == BLACK_SETUP_FAILED)) [self chooseImage:nil];
        else [super keyDown:event];
        return;
    }
    if (code == 123 || code == 124) { [self adjust:code == 124 ? 1 : -1]; return; }
    if (code == 126) [self step:-1];
    else if (code == 125) [self step:1];
    else if (code == 36 || code == 76 || code == 49) [self activate];
    else if (code == 53 || code == 51) [self back:nil];
    else [super keyDown:event];
}
- (BOOL)acceptsDrop { return page == BLACK_SETUP_DISC || page == BLACK_SETUP_REVIEW || page == BLACK_SETUP_STOPPED || page == BLACK_SETUP_FAILED; }
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender
{ (void)sender; dragging = self.acceptsDrop; [self setNeedsDisplay:YES]; return dragging ? NSDragOperationCopy : NSDragOperationNone; }
- (void)draggingExited:(id<NSDraggingInfo>)sender { (void)sender; dragging = NO; [self setNeedsDisplay:YES]; }
- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender
{
    dragging = NO;
    NSURL *url = [[sender.draggingPasteboard readObjectsForClasses:@[NSURL.class] options:@{NSPasteboardURLReadingFileURLsOnlyKey:@YES}] firstObject];
    if (!url || !self.acceptsDrop) return NO;
    [self inspect:url]; return YES;
}
/* Controllers: d-pad or left stick moves, A confirms, B goes back; held directions repeat. */
- (void)pollPad:(double)now
{
    BOOL up = NO, down = NO, left = NO, right = NO, a = NO, b = NO;
    for (GCController *pad in GCController.controllers) {
        GCExtendedGamepad *g = pad.extendedGamepad;
        if (!g) continue;
        up |= g.dpad.up.pressed || g.leftThumbstick.yAxis.value > .55f;
        down |= g.dpad.down.pressed || g.leftThumbstick.yAxis.value < -.55f;
        left |= g.dpad.left.pressed || g.leftThumbstick.xAxis.value < -.55f;
        right |= g.dpad.right.pressed || g.leftThumbstick.xAxis.value > .55f;
        a |= g.buttonA.pressed; b |= g.buttonB.pressed;
    }
    static BOOL was_a, was_b;
    if (a && !was_a) [self activate];
    if (b && !was_b) [self back:nil];
    was_a = a; was_b = b;
    if (up || down || left || right) {
        if (!pad_held || now >= pad_next) {
            if (up || down) [self step:up ? -1 : 1]; else [self adjust:right ? 1 : -1];
            pad_next = now + (pad_held ? .12 : .4); pad_held = YES;
        }
    } else pad_held = NO;
}
- (void)tick
{
    double now = CACurrentMediaTime(), dt = last_tick ? MIN(.1, now - last_tick) : 0;
    last_tick = now;
    /* Automated app check (BLACK_TEST_SETUP_SOURCE): install that copy and play, untouched. */
    if (getenv("BLACK_TEST_SETUP_SOURCE") && self.age > 1) {
        if (page == BLACK_SETUP_REVIEW) [self install:nil];
        else if (page == BLACK_SETUP_LANGUAGE && getenv("BLACK_TEST_SETUP_LANGUAGE") &&
                 black_SettingsGameLanguage() != atoi(getenv("BLACK_TEST_SETUP_LANGUAGE"))) { selected = 0; [self adjust:1]; }
        else if (page == BLACK_SETUP_LANGUAGE || page == BLACK_SETUP_GRAPHICS) [self next:nil];
        else if (page == BLACK_SETUP_DONE) finished = 1;
        else if (page == BLACK_SETUP_FAILED) finished = -1;
    }
    [self pollPad:now];
    CGFloat target = [self rowY:selected];
    selector_y = selector_y < 0 || still() ? target : selector_y + (target - selector_y) * MIN(1, dt * 16);
    if (page == BLACK_SETUP_COPYING) {
        uint64_t done = progress.bytesDone;
        if (!sample_time) { sample_time = now; sample_bytes = done; }
        else if (now - sample_time >= .5) {
            double rate = (done - sample_bytes) / (now - sample_time);
            speed = speed ? speed * .7 + rate * .3 : rate;
            sample_time = now; sample_bytes = done;
        }
    }
    [self setNeedsDisplay:YES];
}

/* ---- drawing ---- */
- (void)prompt:(NSString *)key pad:(NSString *)button label:(NSString *)label at:(CGFloat *)x
{
    if (button) {
        unsigned rgb = [button isEqual:@"A"] ? 0x3f7d45 : 0x9a3a32;
        [black_ui_color(rgb, 1) setFill];
        [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(*x, 527, 17, 17)] fill];
        text(button, *x + 8.5 - width_of(button, 9, 0) / 2, 531, 9, 0xe8e8e8, 1, 0);
        *x += 24;
    }
    CGFloat w = width_of(key, 9, 0) + 12;
    [black_ui_color(BLACK_UI_RAIL, .7) setStroke];
    NSBezierPath *box = [NSBezierPath bezierPathWithRoundedRect:NSMakeRect(*x + .5, 527.5, w, 16) xRadius:2 yRadius:2];
    box.lineWidth = 1; [box stroke];
    text(key, *x + 6.5, 531, 9, BLACK_UI_TEXT, 1, 0);
    *x += w + 8;
    text(label, *x, 531, 10, BLACK_UI_ROW, 1, 0);
    *x += width_of(label, 10, 0) + 26;
}
- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    NSSize size = self.bounds.size;
    black_ui_fill(self.bounds, BLACK_UI_BACKDROP, 1);
    CGFloat scale = MIN(size.width / 960, size.height / 600), ox = (size.width - 960 * scale) / 2, oy = (size.height - 600 * scale) / 2;
    [NSGraphicsContext saveGraphicsState];
    NSAffineTransform *t = [NSAffineTransform transform];
    [t translateXBy:ox yBy:oy]; [t scaleBy:scale]; [t concat];
    double age = self.age, now = CACurrentMediaTime();
    black_ui_rails(30, -oy / scale, 600 + oy / scale, still() || frozen_age >= 0 ? -1 : fmod(now / 7, 1) * 1.3 - .15);
    CGFloat rail = 76, edge = 960 + ox / scale;

    /* Title and the steps of setup. */
    text(@"BLACK", 96, 34, 40, BLACK_UI_TEXT, 1, 1);
    text(@"FIRST-TIME SETUP  /  ORIGINAL XBOX  /  NATIVE ON MAC", 98, 86, 10, BLACK_UI_ROW, 1, 0);
    NSArray *steps = @[@"1  DISC", @"2  COPY", @"3  LANGUAGE", @"4  GRAPHICS", @"5  PLAY"];
    int step = page == BLACK_SETUP_DONE ? 4 : page == BLACK_SETUP_GRAPHICS ? 3 : page == BLACK_SETUP_LANGUAGE ? 2 :
        page == BLACK_SETUP_COPYING || page == BLACK_SETUP_STOPPED ? 1 : 0;
    CGFloat sx = 876;
    for (int i = (int)steps.count - 1; i >= 0; --i) {
        sx -= width_of(steps[i], 11, 0);
        text(steps[i], sx, 48, 11, i == step ? BLACK_UI_TEXT : i < step ? BLACK_UI_ROW : BLACK_UI_DISABLED, 1, 0);
        if (i == step) over(NSMakeRect(sx, 64, width_of(steps[i], 11, 0), 1.5), BLACK_UI_TEXT, .9);
        sx -= 28;
    }
    over(NSMakeRect(84, 108, 792, 1), BLACK_UI_TICK, 1);

    /* The page heading types in with a block cursor, as the game's menus do. */
    NSString *heading = @[@"YOUR DISC", @"CHECKING YOUR COPY", @"READY TO INSTALL", @"COPYING YOUR DISC",
                          @"COPYING STOPPED", @"SETUP COULD NOT FINISH", @"LANGUAGE", @"GRAPHICS", @"READY"][page];
    NSUInteger shown = MIN(heading.length, (NSUInteger)(age * 42));
    text([heading substringToIndex:shown], 96, 130, 24, BLACK_UI_TEXT, 1, 1);
    if (shown < heading.length) {
        CGFloat cx = 96 + width_of([heading substringToIndex:shown], 24, 1) + 2;
        over(NSMakeRect(cx, 128, 13, 28), BLACK_UI_TEXT, .55);
        over(NSMakeRect(cx + 15, 158, 10, 2), BLACK_UI_TEXT, .8);
    }
    CGFloat body = ease((age - .15) / .3);
    NSString *intro = nil;
    switch (page) {
    case BLACK_SETUP_DISC:
        intro = @"USE YOUR OWN ORIGINAL XBOX COPY OF BLACK, USA OR EUROPE: AN IMAGE MADE FROM THE DISC, OR ITS FILES IN A FOLDER. SETUP CHECKS THE RELEASE BEFORE COPYING ANYTHING AND NEVER CHANGES YOUR COPY.";
        break;
    case BLACK_SETUP_CHECKING:
        intro = [NSString stringWithFormat:@"READING %@%@\nTHE GAME'S RELEASE, ITS FILES AND LANGUAGES.", message ?: @"", [@"..." substringToIndex:(NSUInteger)fmod(now * 3, 4) % 4]];
        break;
    case BLACK_SETUP_STOPPED:
        intro = [NSString stringWithFormat:@"%@ OF %@ ARE COPIED AND KEPT. CONTINUE NOW OR THE NEXT TIME YOU OPEN BLACK.", bytes(partial), bytes(info.totalBytes)];
        break;
    case BLACK_SETUP_FAILED: intro = message; break;
    case BLACK_SETUP_LANGUAGE:
        intro = [NSString stringWithFormat:@"THE GAME IS INSTALLED AND CHECKED. YOUR DISC HAS GAME TEXT IN %@%@. THE MENU LANGUAGE SETS THE RECOMP SETTINGS PANEL.",
            languages(info), on_disc(info, BLACK_LANG_RUSSIAN) ? @"; RUSSIAN IS BUILT FROM ITS ENGLISH TEXT" : @""];
        break;
    case BLACK_SETUP_GRAPHICS:
        intro = [NSString stringWithFormat:@"THIS MAC: %@. EACH CHANGE IS SAVED AT ONCE.", this_mac()];
        break;
    case BLACK_SETUP_DONE:
        intro = [NSString stringWithFormat:@"THE %@ RELEASE IS IN YOUR LIBRARY AND CHECKED. GAME TEXT: %@. GRAPHICS: %dX, %@, %@.",
            [info.region isEqual:@"USA"] ? @"USA" : @"EUROPEAN", game_language_names[black_SettingsGameLanguage()], black_SettingsRenderScale(),
            @[@"NO ANTI-ALIASING", @"FXAA", @"SMAA"][black_SettingsAntiAliasing()], black_SettingsUpscaler() ? @"METALFX" : @"NO UPSCALING"];
        break;
    default: break;
    }
    CGFloat y = 176;
    if (intro) for (NSString *line in wrap(intro, 760, 11)) { text(line, 96, y, 11, page == BLACK_SETUP_FAILED ? BLACK_UI_WARN : BLACK_UI_TEXT, body, 0); y += 19; }

    if (page == BLACK_SETUP_DISC) {
        NSRect zone = NSMakeRect(96, 246, 768, 34);
        NSBezierPath *dash = [NSBezierPath bezierPath];
        CGFloat pattern[] = {5, 4}; [dash setLineDash:pattern count:2 phase:0];
        [dash moveToPoint:NSMakePoint(NSMinX(zone), NSMinY(zone) + .5)]; [dash lineToPoint:NSMakePoint(NSMaxX(zone), NSMinY(zone) + .5)];
        [dash moveToPoint:NSMakePoint(NSMinX(zone), NSMaxY(zone) - .5)]; [dash lineToPoint:NSMakePoint(NSMaxX(zone), NSMaxY(zone) - .5)];
        [black_ui_color(dragging ? BLACK_UI_TEXT : BLACK_UI_TICK, body) setStroke]; [dash stroke];
        NSString *drop = dragging ? @"RELEASE TO CHECK THIS COPY" : @"OR DROP AN ISO OR FOLDER ONTO THIS WINDOW";
        text(drop, 480 - width_of(drop, 11, 0) / 2, 258, 11, dragging ? BLACK_UI_TEXT : BLACK_UI_ROW, body, 0);
    } else if (page == BLACK_SETUP_REVIEW) {
        NSString *release = [info.region isEqual:@"USA"] ? @"USA (NTSC)" : @"EUROPE (PAL), RUN WITH THE USA EXECUTABLE";
        NSArray *table = @[@[@"FROM", info.source.lastPathComponent.uppercaseString ?: @""], @[@"RELEASE", release],
            @[@"GAME TEXT", languages(info)], @[@"SIZE", [NSString stringWithFormat:@"%@  /  %lu FILES", bytes(info.totalBytes), (unsigned long)info.files.count]],
            @[@"INSTALL TO", @"YOUR LIBRARY: APPLICATION SUPPORT / BLACK"],
            @[@"FREE SPACE", partial ? [NSString stringWithFormat:@"%@  /  %@ ALREADY COPIED", bytes(free_space), bytes(partial)] : bytes(free_space)]];
        y = 178;
        for (NSUInteger i = 0; i < table.count; ++i) {
            CGFloat a = ease((age - .2 - i * .05) / .25);
            text(table[i][0], 96, y + 2, 10, BLACK_UI_ROW, a, 0);
            text(table[i][1], 236, y, 12, BLACK_UI_TEXT, a, 0);
            y += 24;
        }
    } else if (page == BLACK_SETUP_COPYING) {
        uint64_t done = progress.bytesDone, total = MAX(1, progress.bytesTotal);
        double fraction = MIN(1.0, (double)done / total);
        NSString *percent = [NSString stringWithFormat:@"%d%%", (int)(fraction * 100)];
        text(@"COPYING THE GAME'S FILES", 96, 184, 11, BLACK_UI_TEXT, body, 0);
        text(percent, 876 - width_of(percent, 30, 1), 168, 30, BLACK_UI_TEXT, body, 1);
        NSRect track = NSMakeRect(96, 212, 780, 14);
        [black_ui_color(BLACK_UI_RAIL, .6 * body) setStroke];
        NSBezierPath *outline = [NSBezierPath bezierPathWithRect:NSInsetRect(track, .5, .5)]; outline.lineWidth = 1; [outline stroke];
        over(NSMakeRect(98, 214, 776 * fraction, 10), 0xc9c2ae, .85 * body);
        for (int i = 1; i < 10; ++i) over(NSMakeRect(96 + 78 * i, 226, 1, 5), BLACK_UI_RAIL, .5 * body);
        NSString *left = !speed ? @"MEASURING SPEED" : (total - done) / speed < 60 ? @"LESS THAN A MINUTE LEFT" :
            [NSString stringWithFormat:@"ABOUT %d MINUTES LEFT", (int)ceil((total - done) / speed / 60)];
        text([NSString stringWithFormat:@"%@ OF %@     %@/S     %@", bytes(done), bytes(total), speed ? bytes((uint64_t)speed) : @"-", left],
            96, 244, 11, BLACK_UI_TEXT, body, 0);
        NSString *file = [NSString stringWithFormat:@"FILE %lu OF %lu:  %@", (unsigned long)MIN(progress.filesDone + 1, progress.filesTotal),
            (unsigned long)progress.filesTotal, progress.currentFile.uppercaseString ?: @""];
        text(file, 96, 266, 10, BLACK_UI_ROW, body, 0);
    }

    /* Rows: each slides in a moment after the one above; the bar follows the selection. */
    SetupRow r[8]; int n = [self rows:r];
    if (n) {
        CGFloat bar_alpha = ease((age - .25) / .2);
        if (bar_alpha > 0) {
            [NSGraphicsContext saveGraphicsState];
            CGContextSetAlpha(NSGraphicsContext.currentContext.CGContext, bar_alpha);
            black_ui_selector(NSMakeRect(84, selector_y, 792, self.pitch - 6), rail, edge);
            [NSGraphicsContext restoreGraphicsState];
        }
    }
    for (int i = 0; i < n; ++i) {
        CGFloat e = ease((age - .25 - i * .06) / .22), ry = [self rowY:i];
        unsigned rgb = !r[i].enabled ? BLACK_UI_DISABLED : i == selected ? BLACK_UI_TEXT : BLACK_UI_ROW;
        text(r[i].label, 96 + 14 * (1 - e), ry + (self.pitch - 6) / 2 - 6, 13, rgb, e, 0);
        if (r[i].tag) text(r[i].tag, 862 - width_of(r[i].tag, 11, 0), ry + 12, 11, rgb, e, 0);
        if (r[i].value) {
            CGFloat vy = ry + (self.pitch - 6) / 2 - 6;
            text(@"<", 716, vy, 12, rgb, e, 0);
            text(r[i].value, 790 - width_of(r[i].value, 12, 0) / 2, vy, 12, rgb, e, 0);
            text(@">", 856, vy, 12, rgb, e, 0);
        }
    }
    if (n && selected < n) {
        CGFloat ty = self.rowsTop + n * self.pitch + 10;
        for (NSString *line in wrap(r[selected].help, 768, 10)) { text(line, 96, ty, 10, r[selected].enabled ? BLACK_UI_ROW : BLACK_UI_WARN, body, 0); ty += 17; }
    }

    /* Prompts along the bottom, keyboard and controller. */
    over(NSMakeRect(84, 512, 792, 1), BLACK_UI_TICK, 1);
    CGFloat px = 96;
    [self prompt:@"UP / DOWN" pad:nil label:@"CHOOSE" at:&px];
    if (page == BLACK_SETUP_LANGUAGE || page == BLACK_SETUP_GRAPHICS) [self prompt:@"LEFT / RIGHT" pad:nil label:@"CHANGE" at:&px];
    [self prompt:@"ENTER" pad:@"A" label:@"SELECT" at:&px];
    if (page != BLACK_SETUP_DISC && page != BLACK_SETUP_LANGUAGE) [self prompt:@"ESC" pad:@"B" label:@"BACK" at:&px];
    NSString *version = [NSString stringWithFormat:@"BLACK %@", [NSBundle.mainBundle objectForInfoDictionaryKey:@"CFBundleShortVersionString"] ?: @"DEV"];
    text(version, 876 - width_of(version, 10, 0), 531, 10, BLACK_UI_DISABLED, 1, 0);
    [NSGraphicsContext restoreGraphicsState];
}
@end

@interface BlackSetupWindow : NSObject <NSWindowDelegate> { @public BlackSetupView *view; }
@end
@implementation BlackSetupWindow
- (BOOL)windowShouldClose:(NSWindow *)sender { (void)sender; [view quit:nil]; if (view->page == BLACK_SETUP_COPYING) NSBeep(); return NO; }
- (void)quit:(id)sender { (void)sender; [view quit:nil]; }
@end

NSView *black_SetupMakeView(NSURL *game, NSURL *patch)
{
    BlackSetupView *view = [[[BlackSetupView alloc] initWithFrame:NSMakeRect(0, 0, 960, 600)] autorelease];
    view->game = [game retain]; view->patch = [patch retain];
    return view;
}

void black_SetupShow(NSView *view, BlackSetupPage page, BlackDiscInfo *info, BlackInstallProgress *progress,
    NSString *message, int selected, double age)
{
    BlackSetupView *v = (BlackSetupView *)view;
    if (info != v->info) { [v->info release]; v->info = [info retain]; [v useFonts]; }
    [v->progress release]; v->progress = [progress retain];
    [v->message release]; v->message = [message retain];
    v->page = page; v->selected = selected; v->frozen_age = age; v->selector_y = [v rowY:selected];
    if (info) { v->free_space = black_FreeSpace(v->game.URLByDeletingLastPathComponent); v->partial = black_PartialBytes(info, v->game); }
    [view setNeedsDisplay:YES];
}

static void run_until(BlackSetupView *view, BOOL (^done)(void), double timeout)
{
    double end = CACurrentMediaTime() + timeout;
    while (!done() && CACurrentMediaTime() < end) @autoreleasepool {
        NSEvent *event = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:[NSDate dateWithTimeIntervalSinceNow:1.0 / 60]
            inMode:NSDefaultRunLoopMode dequeue:YES];
        if (event) [NSApp sendEvent:event];
        [view tick];
        if (view.window) [view displayIfNeeded];
    }
}

BlackSetupPage black_SetupDrive(NSView *view, NSURL *source, double timeout)
{
    BlackSetupView *v = (BlackSetupView *)view;
    [v inspect:source];
    run_until(v, ^{ return (BOOL)(v->page != BLACK_SETUP_CHECKING); }, timeout);
    if (v->page != BLACK_SETUP_REVIEW) return v->page;
    [v install:nil];
    run_until(v, ^{ return (BOOL)(v->page != BLACK_SETUP_COPYING); }, timeout);
    /* Language and graphics keep what they show, as Continue does. */
    if (v->page == BLACK_SETUP_LANGUAGE) [v next:nil];
    if (v->page == BLACK_SETUP_GRAPHICS) [v next:nil];
    return v->page;
}

BOOL black_SetupRun(NSURL *game, NSURL *patch)
{
    NSWindow *window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 960, 600)
        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable |
                  NSWindowStyleMaskResizable | NSWindowStyleMaskFullSizeContentView
        backing:NSBackingStoreBuffered defer:NO];
    window.title = @"BLACK Setup"; window.titleVisibility = NSWindowTitleHidden; window.titlebarAppearsTransparent = YES;
    window.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
    window.backgroundColor = black_ui_color(BLACK_UI_BACKDROP, 1);
    window.contentMinSize = NSMakeSize(720, 450); window.releasedWhenClosed = NO;
    BlackSetupView *view = (BlackSetupView *)black_SetupMakeView(game, patch);
    window.contentView = view;
    BlackSetupWindow *controller = [[BlackSetupWindow alloc] init]; controller->view = view;
    window.delegate = controller;
    /* A menu of its own while setup is up: Quit, and Cmd+W closes as Quit does. */
    BOOL own_menu = !NSApp.mainMenu;
    if (own_menu) {
        NSMenu *main = [[[NSMenu alloc] initWithTitle:@"BLACK"] autorelease], *app = [[[NSMenu alloc] initWithTitle:@"BLACK"] autorelease];
        NSMenuItem *item = [[[NSMenuItem alloc] initWithTitle:@"BLACK" action:nil keyEquivalent:@""] autorelease];
        item.submenu = app; [main addItem:item];
        [app addItemWithTitle:@"Quit BLACK" action:@selector(quit:) keyEquivalent:@"q"].target = controller;
        NSApp.mainMenu = main;
    }
    [window center]; [window makeKeyAndOrderFront:nil]; [window makeFirstResponder:view];
    [NSApp activateIgnoringOtherApps:YES];
    fprintf(stderr, "[SETUP] opened; reduce motion %d\n", still());
    const char *test = getenv("BLACK_TEST_SETUP_SOURCE");
    if (test) [view inspect:[NSURL fileURLWithPath:@(test)]];
    run_until(view, ^{ return (BOOL)(view->finished != 0); }, INFINITY);
    BOOL play = view->finished > 0;
    fprintf(stderr, "[SETUP] closed: %s\n", play ? "play" : "quit");
    [window orderOut:nil]; window.delegate = nil;
    if (own_menu) NSApp.mainMenu = nil;
    [controller release]; [window release];
    black_ui_free_font(&big_font); black_ui_free_font(&small_font);
    return play;
}
