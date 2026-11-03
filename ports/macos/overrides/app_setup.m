/* First-run setup drawn like BLACK's menu pages (black_ui.h): rails, a heading
 * typed in with a block cursor, green-grey rows and the selector bar. Layout and
 * flow follow portingpete's BLACK launcher (MIT) and the Burnout Revenge setup;
 * the code is native AppKit. Until the disc is read, DIN Condensed stands in
 * for the game's fonts; then its own Big/Small are read from the player's copy. */
#import "app_setup.h"
#import "black_ui.h"
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
static const char *const page_names[] = {"Disc", "Checking", "Review", "Copying", "Stopped", "Failed", "Done"};

typedef struct { NSString *label, *help, *tag; SEL action; BOOL enabled; } SetupRow;

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
    case BLACK_SETUP_COPYING: return 316;
    case BLACK_SETUP_DISC: return 300;
    default: return 276;
    }
}
- (CGFloat)rowY:(int)i { return self.rowsTop + i * 40; }

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
            if (ok) [self go:BLACK_SETUP_DONE];
            else if (stopped) { partial = copied; [self go:BLACK_SETUP_STOPPED]; }
            else { [message release]; message = [problem.uppercaseString retain]; [self go:BLACK_SETUP_FAILED]; }
            [problem release];
        });
    }});
}
- (void)stop:(id)sender { (void)sender; progress.cancelled = YES; fprintf(stderr, "[SETUP] stop requested\n"); }
- (void)back:(id)sender
{
    (void)sender;
    if (page == BLACK_SETUP_COPYING) { [self stop:nil]; return; }
    if (page == BLACK_SETUP_DISC || page == BLACK_SETUP_DONE) return;
    ++generation; [self go:BLACK_SETUP_DISC];
}
- (void)quit:(id)sender { (void)sender; if (page == BLACK_SETUP_COPYING) [self stop:nil]; else finished = -1; }
- (void)play:(id)sender { (void)sender; finished = 1; }
- (void)activate
{
    SetupRow r[4]; int n = [self rows:r];
    if (selected < n && r[selected].enabled) { [self performSelector:r[selected].action withObject:nil]; [self setNeedsDisplay:YES]; }
    else NSBeep();
}
- (void)step:(int)delta
{
    SetupRow r[4]; int n = [self rows:r];
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
    SetupRow r[4]; int n = [self rows:r];
    for (int i = 0; i < n; ++i) if (p.x >= 84 && p.x <= 876 && p.y >= [self rowY:i] && p.y < [self rowY:i] + 34) return i;
    return -1;
}
- (void)mouseMoved:(NSEvent *)event
{ int row = [self rowAt:event.locationInWindow]; if (row >= 0 && row != selected) { selected = row; [self setNeedsDisplay:YES]; } }
- (void)mouseDown:(NSEvent *)event
{ int row = [self rowAt:event.locationInWindow]; if (row >= 0) { selected = row; [self activate]; } }
- (void)keyDown:(NSEvent *)event
{
    unsigned short code = event.keyCode;
    if (event.modifierFlags & NSEventModifierFlagCommand) {
        if (code == 31 && (page == BLACK_SETUP_DISC || page == BLACK_SETUP_FAILED)) [self chooseImage:nil];
        else [super keyDown:event];
        return;
    }
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
    BOOL up = NO, down = NO, a = NO, b = NO;
    for (GCController *pad in GCController.controllers) {
        GCExtendedGamepad *g = pad.extendedGamepad;
        if (!g) continue;
        up |= g.dpad.up.pressed || g.leftThumbstick.yAxis.value > .55f;
        down |= g.dpad.down.pressed || g.leftThumbstick.yAxis.value < -.55f;
        a |= g.buttonA.pressed; b |= g.buttonB.pressed;
    }
    static BOOL was_a, was_b;
    if (a && !was_a) [self activate];
    if (b && !was_b) [self back:nil];
    was_a = a; was_b = b;
    if (up || down) {
        if (!pad_held || now >= pad_next) { [self step:up ? -1 : 1]; pad_next = now + (pad_held ? .12 : .4); pad_held = YES; }
    } else pad_held = NO;
}
- (void)tick
{
    double now = CACurrentMediaTime(), dt = last_tick ? MIN(.1, now - last_tick) : 0;
    last_tick = now;
    /* Automated app check (BLACK_TEST_SETUP_SOURCE): install that copy and play, untouched. */
    if (getenv("BLACK_TEST_SETUP_SOURCE") && self.age > 1) {
        if (page == BLACK_SETUP_REVIEW) [self install:nil];
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
    NSArray *steps = @[@"1  DISC", @"2  COPY", @"3  PLAY"];
    int step = page == BLACK_SETUP_DONE ? 2 : page == BLACK_SETUP_COPYING || page == BLACK_SETUP_STOPPED ? 1 : 0;
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
                          @"COPYING STOPPED", @"SETUP COULD NOT FINISH", @"BLACK IS INSTALLED"][page];
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
    case BLACK_SETUP_DONE:
        intro = [NSString stringWithFormat:@"THE %@ RELEASE IS IN YOUR LIBRARY AND CHECKED. GAME TEXT: %@.",
            [info.region isEqual:@"USA"] ? @"USA" : @"EUROPEAN", languages(info) ?: @"ENGLISH"];
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
    SetupRow r[4]; int n = [self rows:r];
    if (n) {
        CGFloat bar_alpha = ease((age - .25) / .2);
        if (bar_alpha > 0) {
            [NSGraphicsContext saveGraphicsState];
            CGContextSetAlpha(NSGraphicsContext.currentContext.CGContext, bar_alpha);
            black_ui_selector(NSMakeRect(84, selector_y, 792, 34), rail, edge);
            [NSGraphicsContext restoreGraphicsState];
        }
    }
    for (int i = 0; i < n; ++i) {
        CGFloat e = ease((age - .25 - i * .06) / .22), ry = [self rowY:i];
        unsigned rgb = !r[i].enabled ? BLACK_UI_DISABLED : i == selected ? BLACK_UI_TEXT : BLACK_UI_ROW;
        text(r[i].label, 96 + 14 * (1 - e), ry + 11, 13, rgb, e, 0);
        if (r[i].tag) text(r[i].tag, 862 - width_of(r[i].tag, 11, 0), ry + 12, 11, rgb, e, 0);
    }
    if (n && selected < n) {
        CGFloat ty = self.rowsTop + n * 40 + 10;
        for (NSString *line in wrap(r[selected].help, 768, 10)) { text(line, 96, ty, 10, r[selected].enabled ? BLACK_UI_ROW : BLACK_UI_WARN, body, 0); ty += 17; }
    }

    /* Prompts along the bottom, keyboard and controller. */
    over(NSMakeRect(84, 512, 792, 1), BLACK_UI_TICK, 1);
    CGFloat px = 96;
    [self prompt:@"UP / DOWN" pad:nil label:@"CHOOSE" at:&px];
    [self prompt:@"ENTER" pad:@"A" label:@"SELECT" at:&px];
    if (page != BLACK_SETUP_DISC && page != BLACK_SETUP_DONE) [self prompt:@"ESC" pad:@"B" label:@"BACK" at:&px];
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
