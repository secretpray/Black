/* Native macOS adaptation of TGP's src/common/menu.cpp settings layout and
 * palette. Cocoa controls and local BLACK fonts retain the pinned Mac ABI. */
#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#include "port_menu.h"
#import "black_ui.h"
#include "controller_monitor.h"
#include "controller_axes.h"
#include "keyboard_mouse.h"
#include "game_mods.h"
#include "game_language.h"
#include "subtitles.h"
#include "menu_text.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static BlackUIFont big_font, small_font;
static void load_font(BlackUIFont *font, NSString *path)
{
    black_ui_load_font(font, [NSData dataWithContentsOfFile:path]);
    if (font->cap > 0) fprintf(stderr, "[MENU] local font %s: %ux%u DXT3, cap %.0f\n", path.lastPathComponent.UTF8String,
        font->atlas_width, font->atlas_height, font->cap);
}
static void free_font(BlackUIFont *font) { black_ui_free_font(font); }
static NSColor *color(unsigned rgb, CGFloat alpha) { return black_ui_color(rgb, alpha); }
static void fill(NSRect r, unsigned rgb, CGFloat opacity) { black_ui_fill(r, rgb, opacity); }
static void line(CGFloat x, CGFloat y, CGFloat w, unsigned rgb, CGFloat opacity) { fill(NSMakeRect(x, y, w, 1), rgb, opacity); }
/* Large text falls back to Arial Black, small text to Arial Bold. */
static NSString *fallback(int large) { return large ? @"Arial-Black" : @"Arial-BoldMT"; }
static void text(NSString *value, CGFloat x, CGFloat y, CGFloat size, unsigned rgb, int large)
{ black_ui_text(large ? &big_font : &small_font, fallback(large), value, x, y, size, rgb, 1); }
static CGFloat text_width(NSString *value, CGFloat size, int large)
{ return black_ui_text_width(large ? &big_font : &small_font, fallback(large), value, size); }
static void fitted_text(NSString *value, CGFloat x, CGFloat y, CGFloat size, CGFloat width, unsigned rgb, int large)
{
    CGFloat advance = text_width(value, size, large);
    text(value, x, y, advance > width ? size * width / advance : size, rgb, large);
}
static void centered_text(NSString *value, CGFloat center, CGFloat y, CGFloat size, unsigned rgb)
{ text(value, center - text_width(value, size, 0) / 2, y, size, rgb, 0); }

/* Panel text in the menu language (menu_text.h), keyed by its English in
 * capitals; untranslated text stays English. Results are capitals, keeping
 * any format specifiers lower case. */
static int menu_text_language(void)
{
    int menu = black_SettingsMenuLanguage();
    return menu ? menu - 1 : black_SettingsGameLanguage();
}
static NSString *T(NSString *english)
{
    static NSDictionary *tables[BLACK_TEXT_COUNT];
    int language = menu_text_language();
    if (language <= BLACK_TEXT_EN || language >= BLACK_TEXT_COUNT || !english) return english;
    if (!tables[language]) {
        NSMutableDictionary *table = [NSMutableDictionary dictionary];
        for (size_t i = 0; i < sizeof black_menu_text / sizeof black_menu_text[0]; ++i) {
            const char *text = black_menu_text[i].text[language - 1];
            if (!*text) continue;
            NSMutableString *upper = [[[NSString stringWithUTF8String:text] uppercaseString] mutableCopy];
            NSRegularExpression *spec = [NSRegularExpression regularExpressionWithPattern:@"%[-+ 0-9.]*[A-Z]" options:0 error:nil];
            for (NSTextCheckingResult *m in [spec matchesInString:upper options:0 range:NSMakeRange(0, upper.length)]) {
                NSRange last = NSMakeRange(m.range.location + m.range.length - 1, 1);
                [upper replaceCharactersInRange:last withString:[[upper substringWithRange:last] lowercaseString]];
            }
            table[[NSString stringWithUTF8String:black_menu_text[i].english]] = upper;
            [upper release];
        }
        tables[language] = [table copy];
    }
    return tables[language][english] ?: english;
}

/* TGP's centered settings panel: supported recomp groups and controller page. */
enum { ROW_SCALE, ROW_UPSCALE, ROW_FOV, ROW_FPS, ROW_AA, ROW_AF, ROW_GAME_LANG, ROW_MENU_LANG, ROW_TEXTURES, ROW_TEXTURE_SIZE,
       ROW_LOOK_X, ROW_LOOK_Y, ROW_AIM_X, ROW_AIM_Y, ROW_INVERT_X, ROW_HOLD_AIM, ROW_AIM_ASSIST,
       ROW_LEFT_DZ, ROW_RIGHT_DZ, ROW_TRIGGER_DZ, ROW_VIBRATION, ROW_RUMBLE,
       ROW_HID, ROW_MEASURE, ROW_RESET,
       ROW_MOUSE_X, ROW_MOUSE_Y, ROW_MOUSE_AIM_X, ROW_MOUSE_AIM_Y, ROW_MOUSE_INVERT_X, ROW_MOUSE_INVERT_Y,
       ROW_MOUSE_HOLD, ROW_WALK_SPEED, ROW_BIND_FIRST, ROW_BIND_LAST = ROW_BIND_FIRST + BLACK_ACTION_COUNT - 1,
       ROW_INTROS, ROW_MISSIONS, ROW_MOD_FIRST, ROW_MOD_LAST = ROW_MOD_FIRST + BLACK_MOD_COUNT - 1, MENU_ROWS };
/* TGP's tab bar: one tab a page, sharing the line equally. */
enum { MENU_TABS = 4, MENU_TAB_MAX_ROWS = 5, TAB_WIDTH = 198 };
static NSString *const tab_names[MENU_TABS] = {@"RECOMP SETTINGS", @"MOUSE AND KEYBOARD", @"CONTROLLER", @"MODS"};
static const int tab_first[MENU_TABS] = {ROW_SCALE, ROW_MOUSE_X, ROW_LOOK_X, ROW_INTROS};
static const int tab_rows[MENU_TABS] = {10, 8 + BLACK_ACTION_COUNT, 15, 2 + BLACK_MOD_COUNT};
static int tab_of(int row) { return row >= ROW_INTROS ? 3 : row >= ROW_MOUSE_X ? 1 : row >= ROW_LOOK_X ? 2 : 0; }
static int is_binding(int row) { return row >= ROW_BIND_FIRST && row <= ROW_BIND_LAST; }
static int group_of(int row)
{
    if (row >= ROW_MOD_FIRST + BLACK_MOD_UNLOCK_MISSIONS) return 13;
    if (row >= ROW_MOD_FIRST) return 12;
    if (row >= ROW_INTROS) return 11;
    if (row >= ROW_BIND_FIRST) return 10;
    if (row >= ROW_MOUSE_X) return 9;
    if (row <= ROW_FPS) return 0;
    if (row <= ROW_AF) return 1;
    if (row <= ROW_MENU_LANG) return 2;
    if (row == ROW_TEXTURES || row == ROW_TEXTURE_SIZE) return 14;
    if (row <= ROW_INVERT_X) return 3;
    if (row <= ROW_AIM_ASSIST) return 4;
    if (row <= ROW_TRIGGER_DZ) return 5;
    if (row <= ROW_RUMBLE) return 6;
    if (row == ROW_HID) return 7;
    return 8;
}
static NSString *group_name(int group)
{ return (@[@"DISPLAY", @"GRAPHICS", @"GENERAL", @"LOOK SENSITIVITY", @"AIMING", @"DEADZONES", @"FEEDBACK", @"DRIVER", @"CALIBRATION", @"MOUSE LOOK", @"KEY BINDINGS",
    @"FILMS", @"PLAYER", @"UNLOCKS", @"TEXTURES"])[group]; }
static int visible_start(int selected)
{ return MAX(tab_first[tab_of(selected)], selected - MENU_TAB_MAX_ROWS + 1); }
static CGFloat row_y(int row, int selected)
{
    int start = visible_start(selected); CGFloat y = 146;
    for (int i = start; i <= row; ++i) {
        if (i == start || group_of(i) != group_of(i - 1)) y += 20;
        if (i == row) return y;
        y += 40;
    }
    return y;
}
static NSArray *row_labels(void)
{
    NSMutableArray *labels = [NSMutableArray arrayWithArray:@[@"Render Scale", @"Upscaling", @"Field of View", @"FPS Counter", @"Anti-Aliasing", @"Anisotropic Filtering", @"Game Language", @"Menu Language", @"Texture Replacement", @"HD Texture Size",
        @"Horizontal Sensitivity", @"Vertical Sensitivity",
        @"Aim Horizontal Sensitivity", @"Aim Vertical Sensitivity", @"Invert X Axis", @"Hold to Aim", @"Aim Assist",
        @"Left Stick Deadzone", @"Right Stick Deadzone", @"Trigger Deadzone", @"Vibration",
        @"Rumble Intensity", @"DualSense Raw HID", @"Measure Stick Drift", @"Reset Input", @"Mouse Horizontal Sensitivity", @"Mouse Vertical Sensitivity",
        @"Mouse Aim Horizontal Sensitivity", @"Mouse Aim Vertical Sensitivity", @"Invert Mouse X", @"Invert Mouse Y",
        @"Mouse Hold to Aim", @"Walk Speed"]];
    for (int i = 0; i < BLACK_ACTION_COUNT; ++i) [labels addObject:[NSString stringWithUTF8String:black_KeyActions()[i].label]];
    [labels addObjectsFromArray:@[@"Skip Intro Movies", @"Skip Mission Intros"]];
    for (int i = 0; i < BLACK_MOD_COUNT; ++i) [labels addObject:[NSString stringWithUTF8String:black_mod_info[i].label]];
    return labels;
}
/* BLACK's own menu palette (black_ui.h): grey text on the selected row, green-grey
 * rows, light rails; TGP's layout and rows are kept, its gold palette is not. */
enum { TEXT = BLACK_UI_TEXT, DIM = BLACK_UI_ROW, HEADING = BLACK_UI_RAIL, HIGHLIGHT = BLACK_UI_TICK,
       TAB = BLACK_UI_BAR, BUTTON = BLACK_UI_BAR, WARN = BLACK_UI_WARN };
static const int aniso_levels[] = {0, 2, 4, 8, 16};
static NSString *aa_name(int mode) { return mode == 2 ? @"SMAA" : mode == 1 ? @"FXAA" : @"OFF"; }
static NSString *af_name(int level) { return level ? [NSString stringWithFormat:@"%dX", level] : @"GAME"; }
static int active_hid, launch_language;
static NSString *const game_language_names[BLACK_GAME_LANGUAGES] = {@"ENGLISH", @"FRANÇAIS", @"DEUTSCH", @"ESPAÑOL", @"ITALIANO"};
static NSString *const menu_language_names[] = {@"AUTO", @"ENGLISH", @"FRANÇAIS", @"DEUTSCH", @"ESPAÑOL", @"ITALIANO", @"РУССКИЙ"};
static int language_available(int language) { return black_game_language_available(getenv("BLACK_GAME_DIR"), language); }
static int languages_on_disc(void)
{
    int count = 0;
    for (int i = 0; i < BLACK_GAME_LANGUAGES; ++i) count += language_available(i);
    return count;
}
/* What each mod does, then when it takes effect and what it leaves behind. */
static NSString *const mod_help[BLACK_MOD_COUNT][2] = {
    {@"REFILLS YOUR HEALTH AND BLOCKS EVERY HIT, AS THE GAME'S OWN SCRIPTS DO.", @"LIVE. YOUR CHARACTER ONLY; ENEMIES AND SAVES ARE UNCHANGED."},
    {@"KEEPS THE MEDKIT POUCH FULL: THREE, THE GAME'S OWN LIMIT.", @"LIVE. USING ONE STILL HEALS AS USUAL."},
    {@"KEEPS NINE GRENADES.", @"LIVE. THROW AS MANY AS YOU LIKE."},
    {@"YOUR MAGAZINE AND RESERVE NEVER GO DOWN, SO YOU NEVER RELOAD.", @"LIVE. ANY WEAPON YOU HOLD, PICKUPS INCLUDED."},
    {@"THE GAME'S SILVER WEAPONS REWARD: MAGAZINES REFILL, 999 SHOWN.", @"FROM THE NEXT MISSION START OR RESTART. CAMPAIGN MISSIONS ONLY."},
    {@"YOUR WEAPON FIRES TWO, THREE OR FOUR TIMES AS FAST.", @"LIVE. WITHOUT INFINITE AMMO IT EMPTIES THE MAGAZINE AS MUCH FASTER."},
    {@"YOUR SHOTS NO LONGER KICK THE VIEW UP.", @"LIVE. BULLET SPREAD AND THE GUN'S OWN ANIMATION ARE UNCHANGED."},
    {@"ANY HIT OF YOUR BULLETS KILLS A SOLDIER.", @"LIVE. YOUR OWN SHOTS ONLY; EXPLOSIONS AND MELEE ARE UNCHANGED."},
    {@"THE PACE OF PLAY: 50% AND 75% SLOW IT DOWN, 150% AND 200% SPEED IT UP.", @"LIVE IN 3D PLAY. MENUS, FILMS AND MUSIC KEEP THEIR OWN SPEED."},
    {@"FITS A SILENCER EVERYWHERE THE +9 TRAINER DID. EXPERIMENTAL.", @"LIVE. AFFECTS EVERY WEAPON THE GAME UPDATES, NOT ONLY YOURS."},
    {@"OPENS ALL EIGHT MISSIONS ON EASY, NORMAL AND HARD.", @"ALSO BLACK OPS WHEN THAT MOD IS ON. SAVED IN THE PROFILE; OFF DOES NOT RELOCK."},
    {@"OPENS THE BLACK OPS DIFFICULTY, NORMALLY EARNED ON HARD.", @"FIRST MISSION, OR ALL WITH UNLOCK ALL MISSIONS. SAVED IN THE PROFILE."},
    {@"STARTS EACH MISSION WITH THE M16A2, THE BLACK OPS REWARD.", @"FROM THE NEXT MISSION START OR RESTART. CAMPAIGN MISSIONS ONLY."},
    {@"THE DEVELOPERS' CODE: AN M249 WITH 600 ROUNDS INSTEAD OF THE FIRST MISSION'S RIFLE.", @"FIRST MISSION ONLY. THE GUN HAS NO TEXTURE THERE. SAME AS A XXXX-XXXX-XXXX-XXXX PROFILE NAME."},
};
static NSString *mod_value(int mod)
{
    if (black_ModsSupported() == 0) return @"N/A";
    return [NSString stringWithUTF8String:black_mod_value_name(mod, black_SettingsMod(mod))];
}
static NSString *dz_name(int right)
{
    int percent = black_SettingsStickDeadzone(right);
    return percent < 0 ? @"GAME 20%" : [NSString stringWithFormat:@"%d%%", percent];
}

@interface BlackMenuView : NSView {
@public
    NSButton *rows[MENU_ROWS];
    NSButton *categories[MENU_TABS];
    NSButton *back, *defaults;
    int selected, binding_slot;
    BOOL binding_capture;
    NSTimeInterval changed;
}
- (void)changeOption:(id)sender;
- (void)selectRow:(int)row;
- (void)stepTab:(int)direction;
- (void)stepRow:(int)direction;
- (void)adjustOption:(int)direction cycle:(BOOL)cycle;
- (void)refreshValues;
- (void)layoutRows;
- (void)captureBinding:(int)key;
@end
/* Native transparent controls retain accessibility and mouse hit targets;
 * their visible labels/focus state are drawn in the game's typography. */
@interface BlackMenuButton : NSButton
@end
@implementation BlackMenuButton
- (void)drawRect:(NSRect)dirty { (void)dirty; }
@end

/* TGP's FPS COUNTER (src/game/fps_counter.cpp): "FPS: 30.0" in the game's
 * small font, HUD grey with a black shadow, in the top left corner; smaller
 * than TGP's 15-line text so it stays unobtrusive over the picture. It
 * counts the game's finished frames, not the fixed-rate presentation. */
uint32_t black_GameFrameCount(void);
@interface BlackFpsView : NSView {
@public
    double fps;
}
@end
@implementation BlackFpsView
- (BOOL)isFlipped { return YES; }
- (NSView *)hitTest:(NSPoint)point { (void)point; return nil; }
- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    /* text() sizes by capital height: about 5 lines of a 480 frame, never
     * below 7 points, with a one-point shadow. */
    CGFloat unit = self.bounds.size.height / 480, cap = MAX(7, 5 * unit), margin = 6 * unit;
    NSString *value = [NSString stringWithFormat:@"FPS: %.1f", fps];
    text(value, margin + 1, margin + 1, cap, 0x000000, 0);
    text(value, margin, margin, cap, 0xbababa, 0);
}
@end

static NSWindow *menu_window;
static NSView *game_view, *container;
static BlackMenuView *menu_view;
static BlackFpsView *fps_view;
static id event_monitor;
static void (*capture_input)(int);
static int shown, closing;
static NSTimeInterval transition, last_draw;

@implementation BlackMenuView
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (instancetype)initWithFrame:(NSRect)frame
{
    if ((self = [super initWithFrame:frame])) {
        self.wantsLayer = YES;
        [self setAccessibilityLabel:@"BLACK recomp settings"];
        NSArray *labels = row_labels();
        NSArray *help = @[@"Internal render resolution, 1 to 6 times native. Applied next launch.",
            @"Off or MetalFX spatial. Applied next launch.", @"Camera angle, 45 to 140 degrees. Applied next launch.",
            @"Displays the game's frame rate on screen. Live.",
            @"Off, FXAA or SMAA. Applied next launch.", @"Game default, 2, 4, 8 or 16 times. Applied next launch.",
            @"Text of the game itself from your disc's string banks. Applied next launch.",
            @"Language of this settings panel. Auto follows the game language. Live.",
            @"HD textures from your own BLACK HD Reimagined pack, found through portingpete's map. Live.",
            @"Largest side of an HD texture: 1024, 2048 or 4096. Larger files use a smaller level. Live.",
            @"Horizontal look speed after the game's stick curve. Live.", @"Vertical look speed. Live.",
            @"Horizontal look speed while aiming or scoped. Live.", @"Vertical look speed while aiming or scoped. Live.",
            @"Invert the controller's horizontal look axis. Live. Invert Y is in the game's own options.",
            @"Hold the aim button for aiming and scopes; Off keeps the original mixed/toggle behavior. Live, controller only.",
            @"Enable the game's view attraction and shot assistance on a controller. Off skips both. Live.",
            @"Movement inner deadzone. Game keeps BLACK's original 20 percent. Live.",
            @"Camera inner deadzone. Replaces BLACK's threshold. Live.", @"Trigger travel before activation, 0 to 50 percent. Live.",
            @"Enable controller rumble. Live.", @"Strength of the game's vibration, 0 to 200 percent. Live.",
            @"SDL raw HID or platform driver. Restart required.",
            @"Release both sticks for two seconds, then start. Sets neutral noise plus a two percent margin. Movement or disconnect cancels.",
            @"Restore original controller response and driver defaults. Other settings are kept."];
        for (int i = 0; i < MENU_ROWS; ++i) {
            rows[i] = [[BlackMenuButton alloc] initWithFrame:NSZeroRect];
            rows[i].title = labels[i]; rows[i].tag = i;
            rows[i].target = self; rows[i].action = @selector(changeOption:);
            rows[i].focusRingType = NSFocusRingTypeNone;
            [rows[i] setAccessibilityLabel:labels[i]];
            [rows[i] setAccessibilityHelp:i < ROW_MOUSE_X ? help[i] :
                i >= ROW_INTROS ? (i == ROW_INTROS ? @"Skip startup logos and intro films." :
                    i == ROW_MISSIONS ? @"Skip mission briefings." :
                    [mod_help[i - ROW_MOD_FIRST][0] stringByAppendingFormat:@" %@", mod_help[i - ROW_MOD_FIRST][1]].capitalizedString) :
                is_binding(i) ? @"Two key slots. Left and Right choose a slot. Enter waits for a key or mouse button. Escape cancels, Delete clears." :
                @"Mouse and keyboard preferences. Applied live and saved automatically."];
            [self addSubview:rows[i]]; [rows[i] release];
        }
        for (int i = 0; i < MENU_TABS; ++i) {
            categories[i] = [[BlackMenuButton alloc] initWithFrame:NSZeroRect];
            categories[i].title = tab_names[i].capitalizedString; categories[i].tag = tab_first[i];
            categories[i].target = self; categories[i].action = @selector(changeCategory:);
            [categories[i] setAccessibilityLabel:categories[i].title];
            [self addSubview:categories[i]]; [categories[i] release];
        }
        back = [[BlackMenuButton alloc] initWithFrame:NSZeroRect];
        back.title = @"Back"; back.target = self; back.action = @selector(goBack:);
        [self addSubview:back]; [back release];
        defaults = [[BlackMenuButton alloc] initWithFrame:NSZeroRect];
        defaults.title = @"Defaults"; defaults.target = self; defaults.action = @selector(restoreDefaults:);
        [defaults setAccessibilityHelp:@"Restore defaults for the selected page only."];
        [self addSubview:defaults]; [defaults release];
        [self selectRow:ROW_SCALE];
    }
    return self;
}
- (void)setFrameSize:(NSSize)size
{
    [super setFrameSize:size];
    [self layoutRows];
}
- (void)layoutRows
{
    NSSize size = self.bounds.size;
    CGFloat scale = MIN(size.width / 960.0, size.height / 600.0);
    CGFloat ox = (size.width - 960 * scale) / 2, oy = (size.height - 600 * scale) / 2;
    int tab = tab_of(selected), start = visible_start(selected);
    for (int i = 0; i < MENU_ROWS; ++i) {
        rows[i].hidden = tab_of(i) != tab || i < start || i >= start + MENU_TAB_MAX_ROWS;
        rows[i].frame = NSMakeRect(ox + 84 * scale, oy + row_y(i, selected) * scale,
            (tab == 2 ? 600 : 792) * scale, 38 * scale);
    }
    for (int i = 0; i < MENU_TABS; ++i)
        categories[i].frame = NSMakeRect(ox + (84 + i * TAB_WIDTH) * scale, oy + 82 * scale, TAB_WIDTH * scale, 38 * scale);
    back.frame = NSMakeRect(ox + 84 * scale, oy + 526 * scale, 126 * scale, 30 * scale);
    defaults.frame = NSMakeRect(ox + 738 * scale, oy + 526 * scale, 138 * scale, 30 * scale);
}
- (void)refreshValues
{
    [rows[ROW_INTROS] setAccessibilityValue:black_SettingsSkipIntros() ? @"On" : @"Off"];
    [rows[ROW_MISSIONS] setAccessibilityValue:black_SettingsSkipMissionIntros() ? @"On" : @"Off"];
    [rows[ROW_SCALE] setAccessibilityValue:[NSString stringWithFormat:@"%d times, next launch", black_SettingsRenderScale()]];
    [rows[ROW_UPSCALE] setAccessibilityValue:black_SettingsUpscaler() ? @"MetalFX, next launch" : @"Off, next launch"];
    [rows[ROW_GAME_LANG] setAccessibilityValue:[NSString stringWithFormat:@"%@, next launch", game_language_names[black_SettingsGameLanguage()].capitalizedString]];
    [rows[ROW_MENU_LANG] setAccessibilityValue:menu_language_names[black_SettingsMenuLanguage()].capitalizedString];
    [rows[ROW_FPS] setAccessibilityValue:black_SettingsFpsCounter() ? @"On" : @"Off"];
    [rows[ROW_TEXTURES] setAccessibilityValue:black_SettingsTextureReplacement() ? @"On" : @"Off"];
    [rows[ROW_TEXTURE_SIZE] setAccessibilityValue:[NSString stringWithFormat:@"%d", black_SettingsTextureMaxSize()]];
    [rows[ROW_FOV] setAccessibilityValue:[NSString stringWithFormat:@"%d degrees, next launch", black_SettingsFov()]];
    [rows[ROW_AA] setAccessibilityValue:[NSString stringWithFormat:@"%@, next launch", aa_name(black_SettingsAntiAliasing())]];
    [rows[ROW_AF] setAccessibilityValue:[NSString stringWithFormat:@"%@, next launch", af_name(black_SettingsAniso())]];
    for (int i = 0; i < 4; ++i) [rows[ROW_LOOK_X + i] setAccessibilityValue:
        [NSString stringWithFormat:@"%.2f times", black_SettingsLookSpeed(i) / 50.0]];
    [rows[ROW_HOLD_AIM] setAccessibilityValue:black_SettingsHoldAim() ? @"On" : @"Off"];
    [rows[ROW_AIM_ASSIST] setAccessibilityValue:black_SettingsAimAssist() ? @"On" : @"Off"];
    [rows[ROW_INVERT_X] setAccessibilityValue:black_SettingsInvertX() ? @"On" : @"Off"];
    [rows[ROW_RUMBLE] setAccessibilityValue:[NSString stringWithFormat:@"%d percent", black_SettingsRumbleIntensity()]];
    [rows[ROW_LEFT_DZ] setAccessibilityValue:dz_name(0)];
    [rows[ROW_RIGHT_DZ] setAccessibilityValue:dz_name(1)];
    [rows[ROW_TRIGGER_DZ] setAccessibilityValue:[NSString stringWithFormat:@"%d percent", black_SettingsTriggerDeadzone()]];
    [rows[ROW_VIBRATION] setAccessibilityValue:black_SettingsVibration() ? @"On" : @"Off"];
    [rows[ROW_HID] setAccessibilityValue:black_SettingsDualSenseHid() ? @"On, next launch" : @"Off, next launch"];
    for (int i = 0; i < BLACK_MOD_COUNT; ++i) [rows[ROW_MOD_FIRST + i] setAccessibilityValue:mod_value(i).capitalizedString];
    for (int i = 0; i < BLACK_ACTION_COUNT; ++i) [rows[ROW_BIND_FIRST+i] setAccessibilityValue:
        [NSString stringWithFormat:@"%s / %s",black_KeyName(black_SettingsBinding(i,0)),black_KeyName(black_SettingsBinding(i,1))]];
    for (int i=0;i<4;++i) [rows[ROW_MOUSE_X+i] setAccessibilityValue:[NSString stringWithFormat:@"%.2f times",black_SettingsMouseSpeed(i)/50.0]];
    [rows[ROW_MOUSE_INVERT_X] setAccessibilityValue:black_SettingsMouseInvert(0)?@"On":@"Off"];
    [rows[ROW_MOUSE_INVERT_Y] setAccessibilityValue:black_SettingsMouseInvert(1)?@"On":@"Off"];
    [rows[ROW_MOUSE_HOLD] setAccessibilityValue:black_SettingsMouseHoldAim()?@"On":@"Off"];
    [rows[ROW_WALK_SPEED] setAccessibilityValue:[NSString stringWithFormat:@"%d percent",black_SettingsWalkSpeed()]];
    [rows[ROW_MEASURE] setAccessibilityValue:[NSString stringWithFormat:@"Measurement state %d", black_ControllerMonitor().calibration]];
}
- (void)goBack:(id)sender { (void)sender; black_MenuCommand(BLACK_MENU_BACK); }
- (void)restoreDefaults:(id)sender
{
    (void)sender; black_ControllerCancelMeasure();
    binding_capture = NO;
    if (tab_of(selected) == 1) black_SettingsResetKeyboard();
    else if (tab_of(selected) == 2) black_SettingsResetInput();
    else if (tab_of(selected) == 3) black_SettingsResetMods();
    else {
        black_SettingsSetRenderScale(2); black_SettingsSetUpscaler(1); black_SettingsSetFov(70);
        black_SettingsSetAntiAliasing(2); black_SettingsSetAniso(0); black_SettingsSetFpsCounter(0);
        black_SettingsSetTextureReplacement(0); black_SettingsSetTextureMaxSize(2048);
    }
    changed = CACurrentMediaTime(); [self refreshValues]; [self setNeedsDisplay:YES];
}
- (void)selectRow:(int)row
{
    binding_capture = NO;
    selected = (row + MENU_ROWS) % MENU_ROWS;
    [self layoutRows];
    for (int i = 0; i < MENU_TABS; ++i)
        [categories[i] setAccessibilityValue:i == tab_of(selected) ? @"Selected" : @""];
    [self refreshValues];
    if (menu_window && black_MenuIsOpen()) [menu_window makeFirstResponder:rows[selected]];
    [self setNeedsDisplay:YES];
}
- (void)changeCategory:(id)sender { [self selectRow:(int)[sender tag]]; }
- (void)stepTab:(int)direction
{
    int tab = (tab_of(selected) + direction + MENU_TABS) % MENU_TABS;
    [self selectRow:tab_first[tab]];
}
- (void)stepRow:(int)direction
{
    int tab = tab_of(selected), count = tab_rows[tab], row = selected - tab_first[tab];
    [self selectRow:tab_first[tab] + (row + direction + count) % count];
}
- (void)captureBinding:(int)key
{
    if (!binding_capture || !is_binding(selected)) return;
    if (key == -2) binding_capture = NO;
    else if (black_KeyValid(key)) {
        black_SettingsSetBinding(selected - ROW_BIND_FIRST, binding_slot, key);
        binding_capture = NO; changed = CACurrentMediaTime();
    }
    black_KeyReleaseAll(); [self refreshValues]; [self setNeedsDisplay:YES];
}
- (void)changeOption:(id)sender
{
    if (sender) [self selectRow:(int)[sender tag]];
    if (sender && selected != ROW_MEASURE && selected != ROW_RESET && NSApp.currentEvent.type == NSEventTypeLeftMouseUp) {
        NSSize size = self.bounds.size;
        CGFloat scale = MIN(size.width / 960.0, size.height / 600.0);
        NSPoint point = [self convertPoint:NSApp.currentEvent.locationInWindow fromView:nil];
        CGFloat x = (point.x - (size.width - 960 * scale) / 2) / scale;
        if (is_binding(selected)) {
            if (x >= 600 && x < 876) {
                binding_slot = x >= 738; binding_capture = YES; black_KeyReleaseAll(); [self setNeedsDisplay:YES];
            }
            return;
        }
        CGFloat left = tab_of(selected) == 2 ? 560 : 752;
        if (x >= left && x < left + 26) { [self adjustOption:-1 cycle:NO]; return; }
        if (x >= left + 96 && x < left + 122) { [self adjustOption:1 cycle:NO]; return; }
        return;
    }
    [self adjustOption:1 cycle:YES];
}
- (void)adjustOption:(int)direction cycle:(BOOL)cycle
{
    if (is_binding(selected)) {
        if (cycle) { binding_capture = YES; black_KeyReleaseAll(); }
        else { binding_capture = NO; binding_slot = direction > 0; }
        [self setNeedsDisplay:YES]; return;
    } else if (selected >= ROW_MOD_FIRST) {
        int mod = selected - ROW_MOD_FIRST, current = black_SettingsMod(mod);
        int value = black_mod_step(mod, current, direction, cycle);
        if (value == current) return;
        black_SettingsSetMod(mod, value);
    } else if (selected >= ROW_MOUSE_X && selected <= ROW_MOUSE_AIM_Y) {
        int axis = selected - ROW_MOUSE_X;
        int value = black_SettingsMouseSpeed(axis) + direction;
        if (cycle && value > 200) value = 1;
        black_SettingsSetMouseSpeed(axis,MAX(1,MIN(200,value)));
    } else if (selected == ROW_MOUSE_INVERT_X || selected == ROW_MOUSE_INVERT_Y) {
        int vertical = selected == ROW_MOUSE_INVERT_Y;
        black_SettingsSetMouseInvert(vertical,cycle ? !black_SettingsMouseInvert(vertical) : direction > 0);
    } else if (selected == ROW_MOUSE_HOLD) {
        black_SettingsSetMouseHoldAim(cycle ? !black_SettingsMouseHoldAim() : direction > 0);
    } else if (selected == ROW_WALK_SPEED) {
        int value = black_SettingsWalkSpeed() + direction * 5;
        if (cycle && value > 100) value = 5;
        black_SettingsSetWalkSpeed(MAX(1,MIN(100,value)));
    } else if (selected >= ROW_LOOK_X && selected <= ROW_AIM_Y) {
        int axis = selected - ROW_LOOK_X, current = black_SettingsLookSpeed(axis);
        int value = current + direction;
        if (cycle && value > 200) value = 1;
        value = MAX(1, MIN(200, value));
        if (value == current) return;
        black_SettingsSetLookSpeed(axis, value);
    } else if (selected == ROW_INVERT_X) {
        black_SettingsSetInvertX(cycle ? !black_SettingsInvertX() : direction > 0);
    } else if (selected == ROW_HOLD_AIM || selected == ROW_AIM_ASSIST) {
        int current = selected == ROW_HOLD_AIM ? black_SettingsHoldAim() : black_SettingsAimAssist();
        int value = cycle ? !current : direction > 0;
        if (value == current) return;
        if (selected == ROW_HOLD_AIM) black_SettingsSetHoldAim(value); else black_SettingsSetAimAssist(value);
    } else if (selected == ROW_RUMBLE) {
        int current = black_SettingsRumbleIntensity(), value = current + direction * 10;
        if (cycle && value > 200) value = 0;
        black_SettingsSetRumbleIntensity(MAX(0, MIN(200, value)));
    } else if (selected == ROW_MEASURE) {
        if (direction < 0) black_ControllerCancelMeasure();
        else black_ControllerMeasure();
        [self setNeedsDisplay:YES];
        return;
    } else if (selected == ROW_RESET) {
        if (direction < 0) return;
        black_ControllerCancelMeasure();
        black_SettingsResetInput();
    } else if (selected == ROW_LEFT_DZ || selected == ROW_RIGHT_DZ) {
        int right = selected == ROW_RIGHT_DZ;
        int current = black_SettingsStickDeadzone(right);
        int value = (current < 0 ? 20 : current) + direction;
        if (cycle && value > 50) value = 0;
        value = MAX(0, MIN(50, value));
        if (value == current) return;
        black_ControllerCancelMeasure();
        black_SettingsSetStickDeadzone(right, value);
    } else if (selected == ROW_TRIGGER_DZ) {
        int current = black_SettingsTriggerDeadzone(), value = current + direction;
        if (cycle && value > 50) value = 0;
        value = MAX(0, MIN(50, value));
        if (value == current) return;
        black_SettingsSetTriggerDeadzone(value);
    } else if (selected == ROW_VIBRATION || selected == ROW_HID) {
        int hid = selected == ROW_HID;
        int current = hid ? black_SettingsDualSenseHid() : black_SettingsVibration();
        int value = cycle ? !current : direction > 0;
        if (value == current) return;
        if (hid) black_SettingsSetDualSenseHid(value); else black_SettingsSetVibration(value);
    } else if ((selected == ROW_INTROS || selected == ROW_MISSIONS)) {
        int current = selected == ROW_MISSIONS ? black_SettingsSkipMissionIntros() : black_SettingsSkipIntros();
        int value = cycle ? !current : direction > 0;
        if (current == value) return;
        if (selected == ROW_MISSIONS) black_SettingsSetSkipMissionIntros(value);
        else black_SettingsSetSkipIntros(value);
    } else if (selected == ROW_GAME_LANG) {
        /* Step over the languages this disc has no string bank for. */
        int current = black_SettingsGameLanguage(), value = current;
        for (int step = 0; step < BLACK_GAME_LANGUAGES; ++step) {
            value += direction;
            if (value < 0 || value >= BLACK_GAME_LANGUAGES) {
                if (!cycle) return;
                value = (value + BLACK_GAME_LANGUAGES) % BLACK_GAME_LANGUAGES;
            }
            if (language_available(value)) break;
        }
        if (value == current || !language_available(value)) return;
        black_SettingsSetGameLanguage(value);
    } else if (selected == ROW_MENU_LANG) {
        int count = (int)(sizeof menu_language_names / sizeof menu_language_names[0]);
        int current = black_SettingsMenuLanguage(), value = current + direction;
        if (cycle) value = (value + count) % count;
        value = MAX(0, MIN(count - 1, value));
        if (value == current) return;
        black_SettingsSetMenuLanguage(value);
    } else if (selected == ROW_FPS) {
        int current = black_SettingsFpsCounter(), value = cycle ? !current : direction > 0;
        if (value == current) return;
        black_SettingsSetFpsCounter(value);
    } else if (selected == ROW_TEXTURES) {
        int current = black_SettingsTextureReplacement(), value = cycle ? !current : direction > 0;
        if (value == current) return;
        black_SettingsSetTextureReplacement(value);
    } else if (selected == ROW_TEXTURE_SIZE) {
        static const int sizes[] = {1024, 2048, 4096};
        int index = 0;
        for (int i = 0; i < 3; ++i) if (sizes[i] == black_SettingsTextureMaxSize()) index = i;
        int next = index + direction;
        if (cycle && next > 2) next = 0;
        next = MAX(0, MIN(2, next));
        if (next == index) return;
        black_SettingsSetTextureMaxSize(sizes[next]);
    } else if (selected == ROW_AA) {
        int current = black_SettingsAntiAliasing(), value = current + direction;
        if (cycle && value > 2) value = 0;
        value = MAX(0, MIN(2, value));
        if (value == current) return;
        black_SettingsSetAntiAliasing(value);
    } else if (selected == ROW_UPSCALE) {
        int current = black_SettingsUpscaler(), value = cycle ? !current : direction > 0;
        if (value == current) return;
        black_SettingsSetUpscaler(value);
    } else if (selected == ROW_AF) {
        int count = (int)(sizeof aniso_levels / sizeof aniso_levels[0]), index = 0;
        for (int i = 0; i < count; ++i) if (aniso_levels[i] == black_SettingsAniso()) index = i;
        int next = index + direction;
        if (cycle && next >= count) next = 0;
        next = MAX(0, MIN(count - 1, next));
        if (next == index) return;
        black_SettingsSetAniso(aniso_levels[next]);
    } else if (selected == ROW_SCALE) {
        int current = black_SettingsRenderScale(), value = current + direction;
        if (cycle && value > 6) value = 1;
        value = MAX(1, MIN(6, value));
        if (value == current) return;
        black_SettingsSetRenderScale(value);
    } else {
        int current = black_SettingsFov(), value = current + 5 * direction;
        if (cycle && value > 140) value = 45;
        value = MAX(45, MIN(140, value));
        if (value == current) return;
        black_SettingsSetFov(value);
    }
    changed = CACurrentMediaTime();
    [self refreshValues];
    [self setNeedsDisplay:YES];
}
- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    NSSize size = self.bounds.size;
    fill(self.bounds, BLACK_UI_BACKDROP, .9);
    [NSGraphicsContext saveGraphicsState];
    CGFloat scale = MIN(size.width / 960.0, size.height / 600.0), ox = (size.width - 960 * scale) / 2, oy = (size.height - 600 * scale) / 2;
    NSAffineTransform *transform = [NSAffineTransform transform];
    [transform translateXBy:ox yBy:oy];
    [transform scaleBy:scale]; [transform concat];
    /* The game's page: rails on the left, heading in its large font, rows to the right. */
    BOOL still = NSWorkspace.sharedWorkspace.accessibilityDisplayShouldReduceMotion;
    black_ui_rails(30, -oy / scale, 600 + oy / scale, still ? -1 : fmod(CACurrentMediaTime() / 7, 1) * 1.3 - .15);
    CGFloat rail = 76, edge = 960 + ox / scale;
    text(T(@"SETTINGS"), 96, 38, 24, TEXT, 1);
    int tab = tab_of(selected);
    for (int i = 0; i < MENU_TABS; ++i) {
        NSString *name = T(tab_names[i]);
        CGFloat x = 84 + i * TAB_WIDTH, w = text_width(name, 14, 0), size = MIN(14, 14 * (TAB_WIDTH - 16) / w);
        if (i == tab) { fill(NSMakeRect(x, 82, TAB_WIDTH, 38), BLACK_UI_BAR, .88); line(x + 12, 116, TAB_WIDTH - 24, TEXT, .9); }
        centered_text(name, x + TAB_WIDTH / 2, 95 + (14 - size) / 2, size, i == tab ? TEXT : DIM);
    }
    if (tab == 3) {
        int supported = black_ModsSupported();
        text([NSString stringWithFormat:T(@"GAME: %s%@"), black_ModsRevision(),
            supported > 0 ? T(@" XBE") : supported < 0 ? @"" : T(@" - MODS OFF")], 96, 130, 10, supported ? DIM : WARN, 0);
    }
    text([NSString stringWithFormat:@"%02d / %02d", selected - tab_first[tab] + 1, tab_rows[tab]],
        820, 130, 10, DIM, 0);
    int start = visible_start(selected), end = MIN(start + MENU_TAB_MAX_ROWS, tab_first[tab] + tab_rows[tab]);
    CGFloat width = tab == 2 ? 600 : 792, left = tab == 2 ? 560 : 752;
    for (int index = start; index < end; ++index) {
        CGFloat y = row_y(index, selected);
        if (index == start || group_of(index) != group_of(index - 1)) {
            text(T(group_name(group_of(index))), 96, y - 17, 10, DIM, 0);
            line(96, y - 3, width - 24, HIGHLIGHT, 1);
        }
        BOOL focus = selected == index;
        /* The live sticks sit right of the controller rows: the line stops at their divider. */
        if (focus) black_ui_selector(NSMakeRect(84, y, width, 38), rail, tab == 2 ? 700 : edge);
        fitted_text(T([row_labels()[index] uppercaseString]), 96, y + 13, 13,
            is_binding(index) ? 490 : left - 108, focus ? TEXT : DIM, 0);
        if (is_binding(index)) {
            for (int slot = 0; slot < 2; ++slot) {
                CGFloat x = 600 + slot * 138;
                BOOL active = focus && slot == binding_slot;
                fill(NSMakeRect(x,y+6,132,26),BUTTON,.9);
                if (active) line(x+6,y+29,120,TEXT,.9);
                NSString *key = active && binding_capture ? @"PRESS A KEY" :
                    [NSString stringWithUTF8String:black_KeyName(black_SettingsBinding(index-ROW_BIND_FIRST,slot))];
                fitted_text(T(key.uppercaseString),x+8,y+13,11,116,active ? TEXT : DIM,0);
            }
            continue;
        }
        NSString *value = index >= ROW_MOD_FIRST ? mod_value(index - ROW_MOD_FIRST) :
            index >= ROW_MOUSE_X && index <= ROW_MOUSE_AIM_Y ?
            [NSString stringWithFormat:@"%.2fX",black_SettingsMouseSpeed(index-ROW_MOUSE_X)/50.0] :
            index == ROW_MOUSE_INVERT_X ? (black_SettingsMouseInvert(0) ? @"ON" : @"OFF") :
            index == ROW_MOUSE_INVERT_Y ? (black_SettingsMouseInvert(1) ? @"ON" : @"OFF") :
            index == ROW_MOUSE_HOLD ? (black_SettingsMouseHoldAim() ? @"ON" : @"OFF") :
            index == ROW_WALK_SPEED ? [NSString stringWithFormat:@"%d%%",black_SettingsWalkSpeed()] :
            index >= ROW_LOOK_X && index <= ROW_AIM_Y ?
            [NSString stringWithFormat:@"%.2fX", black_SettingsLookSpeed(index - ROW_LOOK_X) / 50.0] :
            index == ROW_HOLD_AIM ? (black_SettingsHoldAim() ? @"ON" : @"OFF") :
            index == ROW_AIM_ASSIST ? (black_SettingsAimAssist() ? @"ON" : @"OFF") :
            index == ROW_INVERT_X ? (black_SettingsInvertX() ? @"ON" : @"OFF") :
            index == ROW_RUMBLE ? [NSString stringWithFormat:@"%d%%", black_SettingsRumbleIntensity()] :
            index == ROW_LEFT_DZ ? dz_name(0) : index == ROW_RIGHT_DZ ? dz_name(1) :
            index == ROW_TRIGGER_DZ ? [NSString stringWithFormat:@"%d%%", black_SettingsTriggerDeadzone()] :
            index == ROW_VIBRATION ? (black_SettingsVibration() ? @"ON" : @"OFF") :
            index == ROW_HID ? (black_SettingsDualSenseHid() ? @"ON" : @"OFF") :
            index == ROW_MEASURE ? (black_ControllerMonitor().calibration == BLACK_CAL_MEASURING ? @"WAIT" : @"START") :
            index == ROW_RESET ? @"RESET" :
            index == ROW_FOV ? [NSString stringWithFormat:@"%d", black_SettingsFov()] :
            index == ROW_UPSCALE ? (black_SettingsUpscaler() ? @"METALFX" : @"OFF") :
            index == ROW_FPS ? (black_SettingsFpsCounter() ? @"ON" : @"OFF") :
            index == ROW_TEXTURES ? (black_SettingsTextureReplacement() ? @"ON" : @"OFF") :
            index == ROW_TEXTURE_SIZE ? [NSString stringWithFormat:@"%d", black_SettingsTextureMaxSize()] :
            index == ROW_GAME_LANG ? game_language_names[black_SettingsGameLanguage()] :
            index == ROW_MENU_LANG ? menu_language_names[black_SettingsMenuLanguage()] :
            index == ROW_AA ? aa_name(black_SettingsAntiAliasing()) :
            index == ROW_AF ? af_name(black_SettingsAniso()) :
            index == ROW_INTROS ? (black_SettingsSkipIntros() ? @"ON" : @"OFF") :
            index == ROW_MISSIONS ? (black_SettingsSkipMissionIntros() ? @"ON" : @"OFF") :
            [NSString stringWithFormat:@"%dX", black_SettingsRenderScale()];
        if (index != ROW_MEASURE && index != ROW_RESET) {
            text(@"<", left + 9, y + 13, 13, focus ? TEXT : DIM, 0);
            text(@">", left + 105, y + 13, 13, focus ? TEXT : DIM, 0);
        }
        fitted_text(T(value), left + 32, y + 13, 12, 61, focus ? TEXT : DIM, 0);
    }
    NSString *help, *detail;
    if (selected >= ROW_MOD_FIRST) {
        help = mod_help[selected - ROW_MOD_FIRST][0];
        detail = black_ModsSupported() == 0 ? @"THIS GAME REVISION HAS NO VERIFIED ADDRESSES. MODS STAY OFF." :
            mod_help[selected - ROW_MOD_FIRST][1];
    } else if (is_binding(selected)) {
        help = binding_capture ? @"PRESS A KEY, MOUSE BUTTON OR WHEEL. ESC CANCELS, DELETE CLEARS." :
            @"LEFT / RIGHT SELECT A SLOT. ENTER / A STARTS KEY CAPTURE.";
        detail = @"TWO BINDINGS PER ACTION. PHYSICAL KEY POSITIONS; SAVED IMMEDIATELY.";
    } else if (selected >= ROW_MOUSE_X && selected <= ROW_MOUSE_AIM_Y) {
        help = selected >= ROW_MOUSE_AIM_X ? @"MOUSE SPEED WHILE AIMING OR SCOPED. ORIGINAL: 1.00X." :
            @"RELATIVE MOUSE MOVEMENT TURNS THE VIEW DIRECTLY, WITHOUT STICK LIMITS.";
        detail = @"LIVE. CLICK THE GAME TO CAPTURE THE MOUSE; ESC RELEASES AND PAUSES.";
    } else if (selected == ROW_MOUSE_INVERT_X || selected == ROW_MOUSE_INVERT_Y) {
        help = @"REVERSE THIS AXIS OF THE MOUSE. CONTROLLER INVERSION IS SEPARATE.";
        detail = @"LIVE. DOES NOT CHANGE THE GAME'S CONTROLLER INVERT Y OPTION.";
    } else if (selected == ROW_MOUSE_HOLD) {
        help = @"HOLD THE AIM BUTTON FOR AIMING AND SCOPES WITH MOUSE AND KEYBOARD.";
        detail = @"LIVE. OFF KEEPS THE GAME'S ORIGINAL AIM RULES. CONTROLLER IS SEPARATE.";
    } else if (selected == ROW_WALK_SPEED) {
        help = @"MOVEMENT SPEED WHILE THE WALK ACTION IS HELD. DEFAULT: 50%.";
        detail = @"LIVE. WALK IS ALT BY DEFAULT; BOTH KEY SLOTS CAN BE REBOUND.";
    } else if (selected >= ROW_LOOK_X && selected <= ROW_AIM_Y) {
        help = selected >= ROW_AIM_X ? @"LOOK SPEED WHILE AIMING OR SCOPED. ORIGINAL: 1.00X." : @"LOOK SPEED AFTER BLACK'S STICK CURVE. ORIGINAL: 1.00X.";
        detail = @"LIVE. X / Y AND AIMING ARE INDEPENDENT. STEP: 0.02X.";
    } else if (selected == ROW_INVERT_X) {
        help = @"REVERSE HORIZONTAL CAMERA MOVEMENT ON THE CONTROLLER.";
        detail = @"LIVE. VERTICAL INVERSION IS IN THE GAME'S OWN OPTIONS.";
    } else if (selected == ROW_HOLD_AIM) {
        help = @"HOLD THE AIM BUTTON FOR AIMING AND SCOPES.";
        detail = @"LIVE, CONTROLLER ONLY. OFF KEEPS THE GAME'S ORIGINAL AIM RULES.";
    } else if (selected == ROW_AIM_ASSIST) {
        help = @"ENABLE BLACK'S VIEW ATTRACTION AND SHOT ASSISTANCE.";
        detail = @"LIVE. OFF SKIPS BOTH. ASSISTANCE IS DISABLED WITH KEYBOARD INPUT.";
    } else if (selected == ROW_RUMBLE) {
        help = @"MOTOR STRENGTH AS A PERCENTAGE OF THE GAME'S VIBRATION.";
        detail = @"LIVE. ORIGINAL: 100%. ZERO IS SILENT. MAXIMUM: 200%.";
    } else if (selected == ROW_LEFT_DZ || selected == ROW_RIGHT_DZ) {
        help = selected == ROW_LEFT_DZ ? @"MOVEMENT: LOWER FOR SMALLER, EARLIER INPUTS." : @"CAMERA: LOWER FOR FINER AIM NEAR THE CENTER.";
        detail = @"LIVE. GAME KEEPS 20%. REPLACES THE INNER ZONE, NOT ADDED TO IT.";
    } else if (selected == ROW_TRIGGER_DZ) {
        help = @"TRIGGER TRAVEL BEFORE INPUT COUNTS. ZERO KEEPS ORIGINAL.";
        detail = @"LIVE. REMAINING TRAVEL IS RESCALED TO FULL INPUT.";
    } else if (selected == ROW_VIBRATION) {
        help = @"OFF STOPS RUMBLE. ON ALLOWS THE GAME TO DRIVE THE MOTORS.";
        detail = @"LIVE. SUPPORT DEPENDS ON THE CONTROLLER AND DRIVER.";
    } else if (selected == ROW_HID) {
        help = @"ON: SDL RAW HID. OFF: PLATFORM CONTROLLER DRIVER.";
        detail = [NSString stringWithFormat:T(@"RESTART REQUIRED. CURRENT REQUEST: %@."), T(active_hid ? @"RAW HID" : @"PLATFORM")];
    } else if (selected == ROW_MEASURE) {
        BlackControllerMonitor monitor = black_ControllerMonitor();
        help = @"RELEASE BOTH STICKS FOR TWO SECONDS. ENTER / A STARTS.";
        switch (monitor.calibration) {
        case BLACK_CAL_MEASURING: detail = @"MEASURING... DO NOT TOUCH THE STICKS. LEFT CANCELS."; break;
        case BLACK_CAL_APPLIED: detail = [NSString stringWithFormat:T(@"SET LEFT %d%% / RIGHT %d%%, WITH A 2%% NOISE MARGIN."), monitor.suggested[0], monitor.suggested[1]]; break;
        case BLACK_CAL_MOVED: detail = @"TOO MUCH MOVEMENT. NO SETTINGS CHANGED. TRY AGAIN."; break;
        case BLACK_CAL_DISCONNECTED: detail = @"CONNECT THE CONTROLLER AND TRY AGAIN. NO CHANGE."; break;
        case BLACK_CAL_CANCELLED: detail = @"MEASUREMENT CANCELLED. NO SETTINGS CHANGED."; break;
        default: detail = @"SETS INNER ZONES ONLY. NEVER CHANGES THE STICK CENTER.";
        }
    } else if (selected == ROW_RESET) {
        help = @"RESTORE ORIGINAL LOOK, STICK ZONES AND CONTROLLER DEFAULTS.";
        detail = @"ENTER / A RESETS INPUT ONLY. DRIVER CHANGES NEED A RESTART.";
    } else if (selected == ROW_GAME_LANG) {
        help = @"TEXT AND MENUS OF THE GAME ITSELF, FROM YOUR DISC'S STRING BANKS.";
        detail = languages_on_disc() > 1 ?
            [NSString stringWithFormat:T(@"APPLIES NEXT LAUNCH. CURRENT: %@."), game_language_names[launch_language]] :
            @"THIS DISC HAS ENGLISH ONLY. THE EUROPEAN DISC ADDS FOUR LANGUAGES.";
    } else if (selected == ROW_MENU_LANG) {
        help = @"LANGUAGE OF THIS SETTINGS PANEL. AUTO FOLLOWS THE GAME LANGUAGE.";
        detail = @"LIVE. RUSSIAN IS FOR THIS PANEL ONLY: THE GAME HAS NO RUSSIAN TEXT.";
    } else if (selected == ROW_TEXTURES) {
        help = @"YOUR BLACK HD REIMAGINED PACK IN APPLICATION SUPPORT/BLACK/TEXTURES/REPLACEMENT.";
        detail = @"LIVE. PORTINGPETE'S MAP FINDS EACH FILE; MISSING ONES KEEP THE GAME'S TEXTURE.";
    } else if (selected == ROW_TEXTURE_SIZE) {
        help = @"LARGEST SIDE OF AN HD TEXTURE. LARGER FILES USE A SMALLER LEVEL.";
        detail = @"LIVE. 1024 USES ABOUT A QUARTER OF THE MEMORY OF 2048; MOST OF THE PACK IS 2048.";
    } else if (selected == ROW_FPS) {
        help = @"DISPLAYS THE FRAME RATE ON SCREEN, IN THE GAME'S OWN FONT.";
        detail = @"LIVE. GAME FRAMES PER SECOND; BLACK RUNS NEAR 30 BY DESIGN.";
    } else if (selected == ROW_AA) {
        const char *aa = getenv("RECOMP_AA");
        int active = !aa ? 1 : !strcmp(aa, "smaa") ? 2 : strcmp(aa, "0") ? 1 : 0;
        help = @"SMOOTHS JAGGED EDGES. SMAA IS SHARPER, FXAA IS CHEAPER.";
        detail = [NSString stringWithFormat:T(@"APPLIES NEXT LAUNCH. CURRENT: %@."), T(aa_name(active))];
    } else if (selected == ROW_AF) {
        help = @"SHARPER TEXTURES ON FLOORS AND WALLS AT AN ANGLE.";
        detail = [NSString stringWithFormat:T(@"APPLIES NEXT LAUNCH. CURRENT: %@."), T(af_name(atoi(getenv("RECOMP_AF") ?: "0")))];
    } else if (selected == ROW_UPSCALE) {
        const char *up = getenv("RECOMP_UPSCALE");
        help = @"METALFX SHARPENS THE RENDER SCALE UP TO THE WINDOW'S PIXELS.";
        detail = [NSString stringWithFormat:T(@"APPLIES NEXT LAUNCH. CURRENT: %@."),
            T(up && !strcmp(up, "metalfx") ? @"METALFX" : @"OFF")];
    } else if (selected == ROW_SCALE) {
        int scale = black_SettingsRenderScale();
        help = [NSString stringWithFormat:T(@"INTERNAL RENDER TARGETS: %d X %d."), 640 * scale, 480 * scale];
        detail = [NSString stringWithFormat:T(@"APPLIES NEXT LAUNCH. CURRENT: %dX."), atoi(getenv("RECOMP_SCALE") ?: "2")];
    } else if (selected == ROW_FOV) {
        help = @"CAMERA ANGLE IN DEGREES. ORIGINAL: 70.";
        detail = [NSString stringWithFormat:T(@"APPLIES NEXT LAUNCH. CURRENT: %d."), atoi(getenv("RECOMP_FOV") ?: "70")];
    } else {
        help = selected == ROW_MISSIONS ? @"SKIP THE BRIEFING FILM BEFORE EACH MISSION." : @"SKIP EA, CRITERION AND STARTUP FILMS.";
        detail = selected == ROW_MISSIONS ? @"MISSION PROGRESS AND ENDING ARE UNCHANGED." : @"FILMS PLAY BRIEFLY BEFORE SKIPPING.";
    }
    line(84, 430, 792, HIGHLIGHT, 1);
    fitted_text(T(help), 96, 443, 11, 768, TEXT, 0);
    fitted_text(T(detail), 96, 464, 10, 768, DIM, 0);
    if (tab == 2) {
        BlackControllerMonitor monitor = black_ControllerMonitor();
        fill(NSMakeRect(704, 147, 1, 264), HEADING, .35);
        text(T(monitor.connected ? @"LIVE STICKS" : @"NO CONTROLLER"), 720, 153, 10, HEADING, 0);
        if (monitor.connected) fitted_text([NSString stringWithUTF8String:monitor.name], 720, 170, 9, 150, DIM, 0);
        for (int stick = 0; stick < 2; ++stick) {
            CGFloat cx = 750, cy = 225 + stick * 118;
            text(T(stick ? @"AIM" : @"MOVE"), 792, cy - 26, 10, TEXT, 0);
            int threshold = black_SettingsStickThreshold(stick);
            CGFloat dz = threshold < 0 ? .2 : threshold / 32767.0;
            fill(NSMakeRect(cx - 30, cy - 30, 60, 60), TAB, 1);
            fill(NSMakeRect(cx - dz * 30, cy - dz * 30, dz * 60, dz * 60), HIGHLIGHT, 1);
            line(cx - 30, cy, 60, HEADING, .35); fill(NSMakeRect(cx, cy - 30, 1, 60), HEADING, .35);
            CGFloat rawx = monitor.raw[stick * 2] / 32767.0, rawy = monitor.raw[stick * 2 + 1] / 32767.0;
            CGFloat outx = black_game_axis(monitor.mapped[stick * 2]);
            CGFloat outy = -black_game_axis(monitor.mapped[stick * 2 + 1]);
            fill(NSMakeRect(cx + rawx * 30 - 3, cy + rawy * 30 - 3, 6, 6), HEADING, 1);
            fill(NSMakeRect(cx + outx * 30 - 2, cy + outy * 30 - 2, 4, 4), TEXT, 1);
            fitted_text([NSString stringWithFormat:T(@"RAW %+.0f / %+.0f%%"), rawx * 100, -rawy * 100], 720, cy + 38, 9, 150, HEADING, 0);
            fitted_text([NSString stringWithFormat:T(@"OUT %+.0f / %+.0f%%"), outx * 100, -outy * 100], 720, cy + 54, 9, 150, TEXT, 0);
        }
        text([NSString stringWithFormat:@"LT %d%%  RT %d%%", monitor.mapped[4] * 100 / 255, monitor.mapped[5] * 100 / 255], 720, 410, 9, DIM, 0);
    }
    BOOL failed = black_SettingsSaveFailed();
    fitted_text(T(failed ? @"COULD NOT SAVE. SESSION ONLY." : changed ? @"SAVED" : @"CHANGES ARE SAVED AUTOMATICALLY."),
        96, 490, 10, 400, failed ? WARN : DIM, 0);
    fitted_text(T(@"Q / E, LB / RB: PAGE     UP / DOWN: SELECT"), 480, 490, 9, 396, DIM, 0);
    line(84, 514, 792, HIGHLIGHT, 1);
    fill(NSMakeRect(84, 526, 126, 30), BUTTON, .88); line(96, 553, 102, DIM, 1);
    centered_text(T(@"BACK"), 147, 536, 12, TEXT);
    fill(NSMakeRect(738, 526, 138, 30), BUTTON, .88); line(750, 553, 114, DIM, 1);
    { NSString *label = T(@"DEFAULTS"); CGFloat w = text_width(label, 12, 0);
      text(label, 807 - MIN(w, 122) / 2, 536, w > 122 ? 12 * 122 / w : 12, TEXT, 0); }
    fitted_text(T(@"LEFT / RIGHT: ADJUST    ENTER / A: CHANGE    ESC / B: BACK"), 232, 538, 10, 494, DIM, 0);
    [NSGraphicsContext restoreGraphicsState];
}
- (void)mouseDown:(NSEvent *)event { (void)event; }
- (void)rightMouseDown:(NSEvent *)event { (void)event; }
- (void)scrollWheel:(NSEvent *)event
{
    static NSTimeInterval last_scroll;
    NSTimeInterval now = CACurrentMediaTime();
    if (fabs(event.scrollingDeltaY) > .1 && now - last_scroll > .09) {
        last_scroll = now; [self stepRow:event.scrollingDeltaY < 0 ? 1 : -1];
    }
}
@end

extern void black_HostQuit(void) __attribute__((weak));
static void request_quit(void)
{
    if (black_HostQuit) black_HostQuit();
    else [NSApp terminate:nil];
}
@interface BlackMenuActions : NSObject <NSWindowDelegate>
- (void)toggle:(id)sender;
- (void)quit:(id)sender;
@end
@implementation BlackMenuActions
- (void)toggle:(id)sender { (void)sender; black_MenuCommand(BLACK_MENU_TOGGLE); }
- (void)quit:(id)sender { (void)sender; request_quit(); }
- (BOOL)windowShouldClose:(NSWindow *)sender { (void)sender; request_quit(); return NO; }
- (void)windowDidEnterFullScreen:(NSNotification *)note
{ (void)note;fprintf(stderr,"[HOST-APP] native_fullscreen=1\n"); }
- (void)windowDidExitFullScreen:(NSNotification *)note
{ (void)note;fprintf(stderr,"[HOST-APP] native_fullscreen=0\n"); }
- (void)windowDidResignKey:(NSNotification *)note
{ (void)note; menu_view->binding_capture = NO; black_KeyReleaseAll(); [menu_view setNeedsDisplay:YES]; }
@end
static BlackMenuActions *actions;
static NSMenuItem *settings_item;
static BOOL reduced_motion(void)
{
    return NSWorkspace.sharedWorkspace.accessibilityDisplayShouldReduceMotion;
}

static void set_open(int open)
{
    if (open) {
        closing = 0;
        if (!shown) {
            shown = 1; black_MenuSetOpen(1);
            if (capture_input) capture_input(1);
            menu_view.hidden = NO;
            menu_view.alphaValue = reduced_motion() ? 1 : 0;
            transition = CACurrentMediaTime();
            [menu_view setFrameSize:container.bounds.size];
            [menu_view selectRow:menu_view->selected];
            fprintf(stderr, "[MENU] opened %s\n", tab_names[tab_of(menu_view->selected)].UTF8String);
        }
    } else if (shown && !closing) {
        black_ControllerCancelMeasure(); menu_view->binding_capture = NO;
        closing = 1; transition = CACurrentMediaTime();
    }
}

void black_MenuAttach(void *window, void (*capture)(int))
{
    if (menu_window || !window) return;
    menu_window = (NSWindow *)window; capture_input = capture;
    NSBundle *bundle=NSBundle.mainBundle;
    fprintf(stderr,"[HOST-APP] bundle=%s category=%s game_mode_opt_in=%d\n",
        (bundle.bundleIdentifier ?: @"none").UTF8String,
        [(NSString *)([bundle objectForInfoDictionaryKey:@"LSApplicationCategoryType"] ?: @"none") UTF8String],
        [[bundle objectForInfoDictionaryKey:@"LSSupportsGameMode"] boolValue]);
    active_hid = black_SettingsDualSenseHid();
    launch_language = black_SettingsGameLanguage();
    if (!language_available(launch_language)) launch_language = BLACK_LANG_ENGLISH;
    NSString *game = [NSString stringWithUTF8String:getenv("BLACK_GAME_DIR") ?: ""];
    load_font(&big_font, [game stringByAppendingPathComponent:@"language/fonts/Big.bin"]);
    load_font(&small_font, [game stringByAppendingPathComponent:@"language/fonts/Small.bin"]);
    game_view = [menu_window.contentView retain];
    container = [[NSView alloc] initWithFrame:game_view.bounds];
    container.wantsLayer = YES;
    [menu_window setContentView:container];
    game_view.frame = container.bounds;
    game_view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [container addSubview:game_view];
    menu_view = [[BlackMenuView alloc] initWithFrame:container.bounds];
    menu_view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    menu_view.hidden = YES;
    fps_view = [[BlackFpsView alloc] initWithFrame:container.bounds];
    fps_view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    fps_view.hidden = YES;
    [container addSubview:fps_view];
    [container addSubview:menu_view];
    black_SubtitleAttach(container);
    [menu_window makeFirstResponder:game_view];
    event_monitor = [[NSEvent addLocalMonitorForEventsMatchingMask:
        NSEventMaskKeyDown | NSEventMaskKeyUp | NSEventMaskFlagsChanged | NSEventMaskLeftMouseDown |
        NSEventMaskRightMouseDown | NSEventMaskOtherMouseDown | NSEventMaskScrollWheel handler:^NSEvent *(NSEvent *event) {
        if (event.window != menu_window) return event;
        BOOL mouse = event.type == NSEventTypeLeftMouseDown || event.type == NSEventTypeRightMouseDown ||
            event.type == NSEventTypeOtherMouseDown || event.type == NSEventTypeScrollWheel;
        if (mouse) {
            if (black_MenuIsOpen() && menu_view->binding_capture) {
                if (event.type == NSEventTypeScrollWheel) {
                    if (event.scrollingDeltaY != 0) [menu_view captureBinding:event.scrollingDeltaY > 0 ? BLACK_WHEEL_UP : BLACK_WHEEL_DOWN];
                } else if (event.type == NSEventTypeLeftMouseDown) [menu_view captureBinding:BLACK_MOUSE_LEFT];
                else if (event.type == NSEventTypeRightMouseDown) [menu_view captureBinding:BLACK_MOUSE_RIGHT];
                else if (event.buttonNumber >= 2 && event.buttonNumber <= 4) [menu_view captureBinding:BLACK_MOUSE_LEFT + (int)event.buttonNumber];
                return nil;
            }
            return event;
        }
        unsigned short code = event.keyCode;
        BOOL down = event.type == NSEventTypeKeyDown;
        if (code == 12 && (event.modifierFlags & NSEventModifierFlagCommand)) {
            if (down && !event.isARepeat) request_quit();
            return nil;
        }
        if (black_MenuIsOpen() && menu_view->binding_capture) {
            if (event.modifierFlags & NSEventModifierFlagCommand) return nil;
            if (down && !event.isARepeat) [menu_view captureBinding:code == 53 ? -2 : code == 117 ? -1 : code];
            if (event.type == NSEventTypeFlagsChanged) {
                NSUInteger mask = code == 56 || code == 60 ? NSEventModifierFlagShift : code == 58 || code == 61 ? NSEventModifierFlagOption :
                    code == 59 || code == 62 ? NSEventModifierFlagControl : 0;
                if (event.modifierFlags & mask) [menu_view captureBinding:code];
            }
            return nil;
        }
        BOOL shortcut = code == 122 || code == 109 || (code == 43 && (event.modifierFlags & NSEventModifierFlagCommand));
        if (shortcut) {
            if (down && !event.isARepeat) black_MenuCommand(BLACK_MENU_TOGGLE);
            return nil;
        }
        if (!black_MenuIsOpen()) return event;
        if (down && !event.isARepeat) {
            if (code == 117 && is_binding(menu_view->selected)) {
                menu_view->binding_capture = YES; [menu_view captureBinding:-1];
            } else if (code == 53 || code == 11) black_MenuCommand(BLACK_MENU_BACK);
            else if (code == 36 || code == 76 || code == 49) black_MenuCommand(BLACK_MENU_CHANGE);
            else if (code == 123) black_MenuCommand(BLACK_MENU_OFF);
            else if (code == 124) black_MenuCommand(BLACK_MENU_ON);
            else if (code == 126) black_MenuCommand(BLACK_MENU_UP);
            else if (code == 125) black_MenuCommand(BLACK_MENU_DOWN);
            else if (code == 12 || (code == 48 && (event.modifierFlags & NSEventModifierFlagShift)))
                black_MenuCommand(BLACK_MENU_PREV_TAB);
            else if (code == 14 || code == 48) black_MenuCommand(BLACK_MENU_NEXT_TAB);
        }
        return nil;
    }] retain];
    actions = [[BlackMenuActions alloc] init];
    menu_window.delegate = actions;
    NSMenu *main = NSApp.mainMenu;
    if (!main) {
        main = [[[NSMenu alloc] initWithTitle:@"BLACK"] autorelease];
        NSApp.mainMenu = main;
        NSMenuItem *app = [[[NSMenuItem alloc] initWithTitle:@"BLACK" action:nil keyEquivalent:@""] autorelease];
        [main addItem:app];
        app.submenu = [[[NSMenu alloc] initWithTitle:@"BLACK"] autorelease];
    }
    NSMenu *app_menu = main.itemArray.firstObject.submenu ?: main;
    NSMenuItem *quit = [app_menu itemWithTitle:@"Quit BLACK"];
    if (!quit) quit = [app_menu addItemWithTitle:@"Quit BLACK" action:@selector(quit:) keyEquivalent:@"q"];
    quit.action = @selector(quit:); quit.target = actions;
    settings_item = [[NSMenuItem alloc] initWithTitle:@"Recomp Settings…" action:@selector(toggle:) keyEquivalent:@","];
    settings_item.target = actions;
    [app_menu insertItem:settings_item atIndex:0];
    if (getenv("BLACK_OPEN_SETTINGS")) black_MenuCommand(BLACK_MENU_TOGGLE);
}

void black_MenuPump(void)
{
    /* Local process-only integration check; never injects OS keyboard input. */
    static double test_start;
    static int test_sent;
    const char *test_quit = getenv("BLACK_TEST_QUIT_AFTER");
    if (test_quit && menu_window && !test_sent) {
        if (!test_start) test_start = CACurrentMediaTime();
        if (CACurrentMediaTime() - test_start >= atof(test_quit)) {
            test_sent = 1;
            NSEvent *event = [NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint
                modifierFlags:NSEventModifierFlagCommand timestamp:0 windowNumber:menu_window.windowNumber
                context:nil characters:@"q" charactersIgnoringModifiers:@"q" isARepeat:NO keyCode:12];
            [NSApp postEvent:event atStart:NO];
        }
    }
    if (!menu_window) return;
    int measured[2];
    if (black_ControllerTakeMeasurement(measured)) {
        black_SettingsSetStickDeadzone(0, measured[0]);
        black_SettingsSetStickDeadzone(1, measured[1]);
        menu_view->changed = CACurrentMediaTime();
        [menu_view refreshValues];
    }
    static uint32_t fps_frames;
    static double fps_time;
    if (black_SettingsFpsCounter()) {
        uint32_t frames = black_GameFrameCount();
        double now = CACurrentMediaTime();
        if (!fps_time || frames < fps_frames) { fps_time = now; fps_frames = frames; }
        else if (now - fps_time >= .5) {
            fps_view->fps = (frames - fps_frames) / (now - fps_time);
            fps_time = now; fps_frames = frames;
            fps_view.hidden = NO; [fps_view setNeedsDisplay:YES];
        }
    } else if (!fps_view.hidden) { fps_view.hidden = YES; fps_time = 0; }
    unsigned command = black_MenuTakeCommands();
    if (command & BLACK_MENU_TOGGLE) set_open(!black_MenuIsOpen());
    else if (command & BLACK_MENU_BACK) {
        if (menu_view->binding_capture) [menu_view captureBinding:-2]; else set_open(0);
    }
    else if (black_MenuIsOpen() && !closing) {
        if (command & (BLACK_MENU_UP | BLACK_MENU_DOWN)) {
            [menu_view stepRow:(command & BLACK_MENU_DOWN) ? 1 : -1];
        } else if (command & (BLACK_MENU_PREV_TAB | BLACK_MENU_NEXT_TAB)) {
            [menu_view stepTab:(command & BLACK_MENU_NEXT_TAB) ? 1 : -1];
        } else if (command & BLACK_MENU_CHANGE) [menu_view changeOption:nil];
        else if (command & (BLACK_MENU_ON | BLACK_MENU_OFF)) {
            [menu_view adjustOption:(command & BLACK_MENU_ON) ? 1 : -1 cycle:NO];
        }
    }
    if (!shown) return;
    NSTimeInterval now = CACurrentMediaTime(), duration = closing ? 0.12 : 0.18;
    double t = reduced_motion() ? 1 : MIN(1, (now - transition) / duration);
    double eased = 1 - pow(1 - t, 4);
    menu_view.alphaValue = closing ? 1 - eased : eased;
    menu_view.layer.transform = CATransform3DMakeTranslation(reduced_motion() ? 0 :
        (closing ? eased : 1 - eased) * 12, 0, 0);
    if (closing && t >= 1) {
        menu_view.hidden = YES; shown = closing = 0;
        black_MenuSetOpen(0);
        [menu_window makeFirstResponder:game_view];
        if (capture_input) capture_input(0);
        fprintf(stderr, "[MENU] closed\n");
    } else if (now - last_draw > 1.0 / 30) {
        last_draw = now; [menu_view setNeedsDisplay:YES];
    }
}

void black_MenuDetach(void)
{
    if (!menu_window) return;
    black_MenuSetOpen(0);
    if (event_monitor) { [NSEvent removeMonitor:event_monitor]; [event_monitor release]; event_monitor = nil; }
    [settings_item.menu removeItem:settings_item]; [settings_item release]; settings_item = nil;
    if (menu_window.delegate == actions) menu_window.delegate = nil;
    NSMenuItem *quit = [NSApp.mainMenu.itemArray.firstObject.submenu itemWithTitle:@"Quit BLACK"];
    if (quit.target == actions) { quit.target = NSApp; quit.action = @selector(terminate:); }
    [actions release]; actions = nil;
    [menu_view removeFromSuperview]; [menu_view release]; menu_view = nil;
    [fps_view removeFromSuperview]; [fps_view release]; fps_view = nil;
    [game_view removeFromSuperview]; [menu_window setContentView:game_view]; [game_view release]; game_view = nil;
    [container release]; container = nil;
    menu_window = nil; capture_input = NULL; shown = closing = 0;
    free_font(&big_font); free_font(&small_font);
}

int black_MenuUsingGameFonts(void)
{
    return big_font.cap > 0 && small_font.cap > 0 && big_font.image['A'] && small_font.image['A'];
}
