/* Native macOS adaptation of TGP's src/common/menu.cpp settings layout and
 * palette. Cocoa controls and local BLACK fonts retain the pinned Mac ABI. */
#import <Cocoa/Cocoa.h>
#include <stdatomic.h>
#import <GameController/GameController.h>
#import <QuartzCore/QuartzCore.h>
#include "port_menu.h"
#import "black_ui.h"
#include "controller_monitor.h"
#include "controller_axes.h"
#include "keyboard_mouse.h"
#include "input_profile.h"
#import "input_glyphs.h"
#include "game_mods.h"
#include "game_language.h"
#include "game_checkpoint.h"
#include "slot_info.h"
#include <time.h>
#include "subtitles.h"
#include "menu_text.h"
#include "texture_pack.h"
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
static NSString *icon_string(unsigned code)
{ unichar c=code;return [NSString stringWithCharacters:&c length:1]; }
static CGImageRef hint_image(unsigned code)
{
    static CGImageRef images[0x400];
    if(!black_InputGlyphValid(code))return NULL;
    unsigned slot=code-0xE100;if(!images[slot])images[slot]=black_InputGlyphCreate(code,64);
    return images[slot];
}
static CGFloat icon_width(unsigned code,CGFloat size)
{ CGImageRef image=hint_image(code);return image?(size*1.5*CGImageGetWidth(image)/CGImageGetHeight(image)+size*.2):0; }
/* The settings page sets this while it draws: its text is larger than the
 * layout's sizes, grown about each line's middle, as the game's menus are. */
static CGFloat text_scale = 1;
/* The settings page is scaled more in height than in width (page_geometry);
 * text undoes that, height over width, so letters keep their shape. */
static CGFloat text_aspect = 1;
static CGFloat text_width(NSString *value, CGFloat size, int large)
{
    size *= text_scale;
    CGFloat width=0;NSUInteger start=0;
    for(NSUInteger i=0;i<value.length;++i)if(black_InputGlyphValid([value characterAtIndex:i])){
        width+=black_ui_text_width(large?&big_font:&small_font,fallback(large),[value substringWithRange:NSMakeRange(start,i-start)],size);
        width+=icon_width([value characterAtIndex:i],size);start=i+1;
    }
    return width+black_ui_text_width(large?&big_font:&small_font,fallback(large),[value substringFromIndex:start],size);
}
static void text(NSString *value, CGFloat x, CGFloat y, CGFloat size, unsigned rgb, int large)
{
    y -= size * (text_scale - 1) * .5; size *= text_scale;
    if (text_aspect != 1) {
        CGContextRef ctx = NSGraphicsContext.currentContext.CGContext; CGFloat mid = y + size * .5, keep = text_aspect;
        CGContextSaveGState(ctx); CGContextTranslateCTM(ctx, 0, mid); CGContextScaleCTM(ctx, 1, keep); CGContextTranslateCTM(ctx, 0, -mid);
        text_aspect = 1; CGFloat scale = text_scale; text_scale = 1;
        text(value, x, y, size, rgb, large);
        text_scale = scale; text_aspect = keep; CGContextRestoreGState(ctx);
        return;
    }
    NSUInteger start=0;
    for(NSUInteger i=0;i<value.length;++i){unsigned code=[value characterAtIndex:i];if(!black_InputGlyphValid(code))continue;
        NSString *part=[value substringWithRange:NSMakeRange(start,i-start)];
        black_ui_text(large?&big_font:&small_font,fallback(large),part,x,y,size,rgb,1);
        x+=black_ui_text_width(large?&big_font:&small_font,fallback(large),part,size);
        CGImageRef image=hint_image(code);CGFloat h=size*1.5,w=icon_width(code,size)-size*.2;
        CGContextRef ctx=NSGraphicsContext.currentContext.CGContext;CGContextSaveGState(ctx);
        CGContextTranslateCTM(ctx,x,y-size*.25+h);CGContextScaleCTM(ctx,1,-1);
        CGContextDrawImage(ctx,CGRectMake(0,0,w,h),image);CGContextRestoreGState(ctx);
        x+=w+size*.2;start=i+1;
    }
    black_ui_text(large?&big_font:&small_font,fallback(large),[value substringFromIndex:start],x,y,size,rgb,1);
}
static void fitted_text(NSString *value, CGFloat x, CGFloat y, CGFloat size, CGFloat width, unsigned rgb, int large)
{
    CGFloat advance = text_width(value, size, large); /* already scaled */
    CGFloat fit = advance > width ? size * width / advance : size;
    text(value, x, y + (size - fit) * .5, fit, rgb, large); /* shrunk about its middle */
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

/* Dialog text: written in sentence case, looked up by its capitals and shown
 * as translated, not in capitals. Numbers go in as %@. */
static NSString *D(NSString *english)
{
    int language = menu_text_language();
    if (language <= BLACK_TEXT_EN || language >= BLACK_TEXT_COUNT) return english;
    const char *key = english.uppercaseString.UTF8String;
    for (size_t i = 0; i < sizeof black_menu_text / sizeof black_menu_text[0]; ++i)
        if (!strcmp(black_menu_text[i].english, key) && *black_menu_text[i].text[language - 1])
            return [NSString stringWithUTF8String:black_menu_text[i].text[language - 1]];
    return english;
}

#include "menu_pages.h"
#include "display_gamma.h"
static NSString *const tab_names[MENU_TABS]={@"GENERAL",@"CONTROLS",@"MODS",@"SAVES"};
static NSString *const page_names[MENU_PAGES]={@"GENERAL",@"CONTROLS",@"BUTTON BINDINGS",@"BUTTON BINDINGS",@"MOUSE AND AIMING",@"GAMEPAD AND VIBRATION",@"ADVANCED GAMEPAD",@"CONTROLLER DIAGNOSTICS",@"MODS",@"SAVES",@"PROFILE BACKUPS",@"PROFILE SLOT",@"IMPORT A PROFILE",@"MISSIONS",@"CONFIRM",@"BACKUP",@"RECOMP SETTINGS"};
static int is_binding(int row){return row>=ROW_BIND_FIRST&&row<=ROW_BIND_LAST;}
static int group_of(int row)
{
    if(row==ROW_QUIT)return 18;
    if(row>=ROW_SAVE_FIRST&&row<=ROW_SAVE_LAST)return 19;
    if(row==ROW_NAV_BACKUPS||row==ROW_PICK_FOLDER||row==ROW_PICK_FINDER)return 20;
    if(row>=ROW_BACKUP_FIRST&&row<=ROW_BACKUP_LAST)return 21;
    if(row>=ROW_SLOT_MISSIONS&&row<=ROW_SLOT_CHECKPOINT_DELETE)return 22;
    if(row>=ROW_MISSION_FIRST&&row<=ROW_MISSION_LAST)return 25+(row-ROW_MISSION_FIRST)/8;
    if(row>=ROW_PICK_FIRST&&row<=ROW_PICK_LAST)return 24;
    if(row==ROW_CONFIRM_YES||row==ROW_CONFIRM_NO)return 23;
    if(row==ROW_BACKUP_RESTORE||row==ROW_BACKUP_DELETE)return 21;
    if(row==ROW_BACKUPS_CLEAR)return 20;
    if(black_menu_pair(row))return 3;
    if(black_menu_link(row)||row==ROW_EDIT_DEVICE)return 17;
    if (row >= ROW_MOD_FIRST + BLACK_MOD_UNLOCK_MISSIONS) return 13;
    if (row >= ROW_MOD_FIRST) return 12;
    if (row >= ROW_ANY_FILM) return 11;
    if (row >= ROW_BIND_FIRST) return 10;
    if (row >= ROW_MOUSE_X) return 9;
    if (row >= ROW_PAD_FIRST) return 16;
    if (row == ROW_DEVICE || row == ROW_PROMPTS) return 15;
    if (row <= ROW_GAMMA) return 0;
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
    @"FILMS", @"PLAYER", @"UNLOCKS", @"TEXTURES", @"DEVICE", @"CONTROLLER BINDINGS", @"CONTROLS", @"GAME", @"PROFILE SLOTS", @"MORE", @"BACKUPS", @"PROFILE", @"CONFIRM", @"IN THE IMPORT FOLDER", @"EASY", @"NORMAL", @"HARD", @"BLACK OPS"])[group]; }
/* One setting back to its default (△ / Y, Backspace); 0 for rows without one
 * (the game language, actions, links, saves). `apply` 0 only asks. */
static int reset_row(int row, int apply)
{
    #define R(cond, action) if (cond) { if (apply) { action; } return 1; }
    R(row == ROW_SCALE, black_SettingsSetRenderScale(2)) R(row == ROW_UPSCALE, black_SettingsSetUpscaler(1))
    R(row == ROW_FOV, black_SettingsSetFov(70)) R(row == ROW_FPS, black_SettingsSetFpsCounter(0))
    R(row == ROW_BRIGHTNESS, black_SettingsSetDisplayBrightness(BLACK_BRIGHTNESS_NEUTRAL))
    R(row == ROW_GAMMA, black_SettingsSetDisplayGamma(BLACK_GAMMA_NEUTRAL))
    R(row == ROW_AA, black_SettingsSetAntiAliasing(2)) R(row == ROW_AF, black_SettingsSetAniso(0))
    R(row == ROW_MENU_LANG, black_SettingsSetMenuLanguage(0)) R(row == ROW_TEXTURES, black_SettingsSetTextureReplacement(0))
    R(row == ROW_TEXTURE_SIZE, black_SettingsSetTextureMaxSize(2048))
    R(row == ROW_ANY_FILM, black_SettingsSetSkipAnyFilm(1)) R(row == ROW_INTROS, black_SettingsSetSkipIntros(0))
    R(row == ROW_MISSIONS, black_SettingsSetSkipMissionIntros(0))
    R(row == ROW_CHECKPOINTS, black_SettingsSetSaveCheckpoints(1))
    R(row >= ROW_MOD_FIRST && row <= ROW_MOD_LAST, black_SettingsSetMod(row - ROW_MOD_FIRST, 0))
    R(row >= ROW_LOOK_X && row <= ROW_AIM_Y, black_SettingsSetLookSpeed(row - ROW_LOOK_X, 50))
    R(row == ROW_INVERT_X, black_SettingsSetInvertX(0)) R(row == ROW_HOLD_AIM, black_SettingsSetHoldAim(0))
    R(row == ROW_AIM_ASSIST, black_SettingsSetAimAssist(1))
    R(row == ROW_LEFT_DZ || row == ROW_RIGHT_DZ, black_SettingsSetStickDeadzone(row == ROW_RIGHT_DZ, -1))
    R(row == ROW_TRIGGER_DZ, black_SettingsSetTriggerDeadzone(0)) R(row == ROW_VIBRATION, black_SettingsSetVibration(1))
    R(row == ROW_RUMBLE, black_SettingsSetRumbleIntensity(100)) R(row == ROW_HID, black_SettingsSetDualSenseHid(1))
    R(row == ROW_DEVICE, black_SettingsSetInputDevice(0)) R(row == ROW_PROMPTS, black_SettingsSetButtonPrompts(0))
    R(row >= ROW_MOUSE_X && row <= ROW_MOUSE_AIM_Y, black_SettingsSetMouseSpeed(row - ROW_MOUSE_X, 50))
    R(row == ROW_MOUSE_INVERT_X || row == ROW_MOUSE_INVERT_Y, black_SettingsSetMouseInvert(row == ROW_MOUSE_INVERT_Y, 0))
    R(row == ROW_MOUSE_HOLD, black_SettingsSetMouseHoldAim(1)) R(row == ROW_WALK_SPEED, black_SettingsSetWalkSpeed(50))
    R(row == ROW_MOUSE_LOOK_PAIR || row == ROW_MOUSE_AIM_PAIR,
      { int a = row == ROW_MOUSE_AIM_PAIR ? 2 : 0; black_SettingsSetMouseSpeed(a, 50); black_SettingsSetMouseSpeed(a + 1, 50); })
    R(row == ROW_PAD_LOOK_PAIR || row == ROW_PAD_AIM_PAIR,
      { int a = row == ROW_PAD_AIM_PAIR ? 2 : 0; black_SettingsSetLookSpeed(a, 50); black_SettingsSetLookSpeed(a + 1, 50); })
    R(row >= ROW_BIND_FIRST && row <= ROW_BIND_LAST,
      { int i = row - ROW_BIND_FIRST; for (int slot = 0; slot < 2; ++slot) black_SettingsSetBinding(i, slot, black_KeyActions()[i].keys[slot]); })
    R(row >= ROW_PAD_FIRST && row <= ROW_PAD_LAST, black_SettingsSetPadBinding(row - ROW_PAD_FIRST, row - ROW_PAD_FIRST))
    #undef R
    return 0;
}
/* One rhythm on every page, as the game's own menus and TGP's: rows of one
 * height from one top, no group headings between them. */
/* Nearer the top than before, as the game's own pages sit, which leaves room
 * for their prompts in a column under the description (PROMPTS_TOP). */
enum { ROWS_TOP = 108, ROW_STEP = 40, PROMPTS_TOP = 500, PROMPT_STEP = ROW_STEP };
/* The rail band is wider than the setup screen's, nearer the game's own
 * column, and the page sits that much further right (CONTENT_DX). */
/* The game's composition, measured on its own menus: a dark margin, the
 * rail band from 25% to 44% of the window's width, the menu from 46% to 93%.
 * The band is drawn on the 960x600 page; the menu's own 960x600 layout is
 * scaled by k and moved by dx, dy into that column, centred in height. The
 * same numbers place the rows' hit targets and read mouse clicks. */
typedef struct { CGFloat scale, ox, oy, rail_x, band, dx, dy, k, ky; } BlackPageGeometry;
static BlackPageGeometry page_geometry(NSSize size)
{
    BlackPageGeometry g;
    g.scale = MIN(size.width / 960.0, size.height / 600.0);
    g.ox = (size.width - 960 * g.scale) / 2; g.oy = (size.height - 600 * g.scale) / 2;
    CGFloat left = -g.ox / g.scale, width = size.width / g.scale;
    g.rail_x = left + .25 * width; g.band = .185 * width;
    CGFloat from = left + .46 * width, to = left + .93 * width;
    g.k = (to - from) / 792; g.ky = MAX(g.k, .82); g.dx = from - 84 * g.k; g.dy = (600 - 560 * g.ky) / 2;
    return g;
}
/* A setting's value box: left edge and width, on the menu's layout. */
enum { VALUE_LEFT = 652, VALUE_WIDTH = 224 };
static CGFloat trail_end = 96; /* where the way here ends, for the page's status */
static CGFloat back_hint_x = 700, back_hint_y = 542, back_hint_w = 120; /* set where the hint is drawn */
static BOOL grouped(int page){(void)page;return NO;}
static int visible_start(int page,int selected)
{return MAX(0,black_menu_index(page,selected)-MENU_TAB_MAX_ROWS+1);}
static CGFloat row_y(int page,int row,int selected)
{ return ROWS_TOP + (black_menu_index(page,row) - visible_start(page,selected)) * ROW_STEP; }
static NSArray *row_labels(void)
{
    NSMutableArray *labels = [NSMutableArray arrayWithArray:@[@"Render Scale", @"Upscaling", @"Field of View", @"FPS Counter", @"Display Brightness", @"Display Gamma", @"Anti-Aliasing", @"Anisotropic Filtering", @"Game Language", @"Menu Language", @"Texture Replacement", @"HD Texture Size",
        @"Horizontal Sensitivity", @"Vertical Sensitivity",
        @"Aim Horizontal Sensitivity", @"Aim Vertical Sensitivity", @"Invert X Axis", @"Hold to Aim", @"Aim Assist",
        @"Left Stick Deadzone", @"Right Stick Deadzone", @"Trigger Deadzone", @"Vibration",
        @"Rumble Intensity", @"DualSense Raw HID", @"Measure Stick Drift", @"Reset Input", @"Input Device", @"Button Prompts"]];
    for (int i = 0; i < BLACK_PAD_CONTROLS; ++i) [labels addObject:@(black_PadControlLabel(i))];
    [labels addObjectsFromArray:@[@"Mouse Horizontal Sensitivity", @"Mouse Vertical Sensitivity",
        @"Mouse Aim Horizontal Sensitivity", @"Mouse Aim Vertical Sensitivity", @"Invert Mouse X", @"Invert Mouse Y",
        @"Mouse Hold to Aim", @"Walk Speed"]];
    for (int i = 0; i < BLACK_ACTION_COUNT; ++i) [labels addObject:[NSString stringWithUTF8String:black_KeyActions()[i].label]];
    [labels addObjectsFromArray:@[@"Skip Films with a Button", @"Auto-Skip Intro Films", @"Auto-Skip Mission Briefings"]];
    for (int i = 0; i < BLACK_MOD_COUNT; ++i) [labels addObject:[NSString stringWithUTF8String:black_mod_info[i].label]];
    [labels addObjectsFromArray:@[@"Button Bindings",@"Mouse and Aiming",@"Gamepad and Vibration",@"Advanced Gamepad",@"Controller Diagnostics",@"Profile Backups",@"General",@"Controls",@"Mods",@"Saves",@"Edit Bindings For",@"Mouse Look",@"Mouse Aiming",@"Gamepad Look",@"Gamepad Aiming",@"Quit Game"]];
    for (int i = 0; i < BLACK_PROFILE_SLOTS; ++i) [labels addObject:@"Profile Slot"];
    [labels addObjectsFromArray:@[@"Missions", @"Import", @"Export", @"Delete", @"Checkpoint", @"Delete the Checkpoint", @"Save Checkpoints"]];
    for (int i = 0; i < BLACK_PROFILE_MISSIONS; ++i) [labels addObject:@"Mission"];
    for (int i = 0; i < BLACK_SAVE_BACKUPS; ++i) [labels addObject:@"Backup"];
    for (int i = 0; i < BLACK_SAVE_IMPORTS; ++i) [labels addObject:@"Import"];
    [labels addObjectsFromArray:@[@"Open the Import Folder", @"Choose in Finder", @"Restore", @"Delete", @"Delete All", @"Yes", @"No", @"Close the Menu", @"Defaults", @"Reset All Settings"]];
    return labels;
}
/* The Saves section (save_profiles.h), object first, as portingpete's save
 * manager: SAVES lists the three slots and the backups; a slot's page offers
 * what fits it (missions, import or replace, export, delete or clear); every
 * change ends on a CONFIRM page saying what it does. Edits are applied by the
 * game's thread; the panel shows its answer. */
static uint8_t profile_data[BLACK_PROFILE_SIZE], confirm_edited[BLACK_PROFILE_SIZE];
static BlackProfileSlot profile_slots[BLACK_PROFILE_SLOTS];
static BlackSaveBackup backups[BLACK_SAVE_BACKUPS];
static BlackCheckpointInfo slot_checkpoint;
static int profile_ok, slot_checkpoint_ok, backup_count, slot_open, backup_open, save_waiting, save_outcome, confirm_reason;
/* What Yes does: 0 an edit of the profile (the game's thread applies it),
 * 1 this backup to the Trash, 2 every backup to the Trash, 3 the page it
 * came from back to its defaults. */
static int confirm_kind;
static NSString *confirm_question, *confirm_detail, *save_note;
/* Profiles to import: one row each, a file's slot. */
typedef struct { char path[1024]; char file[128]; int slot; BlackProfileSlot info; } BlackPick;
static BlackPick picks[BLACK_SAVE_IMPORTS];
static int pick_count;
static void set_note(NSString *note) { [save_note release]; save_note = [note copy]; }
/* Each slot's record beside the saves (slot_info.h) and its picture; none
 * when the record no longer matches the slot. */
static BlackSlotInfo slot_infos[BLACK_PROFILE_SLOTS];
static int slot_info_ok[BLACK_PROFILE_SLOTS];
static NSImage *slot_pictures[BLACK_PROFILE_SLOTS];
static void refresh_slot_infos(void)
{
    for (int i = 0; i < BLACK_PROFILE_SLOTS; ++i) {
        slot_info_ok[i] = profile_ok && black_SlotInfoRead(profile_data, i, &slot_infos[i]);
        [slot_pictures[i] release]; slot_pictures[i] = nil;
        if (slot_info_ok[i] && slot_infos[i].has_picture)
            slot_pictures[i] = [[NSImage alloc] initWithContentsOfFile:@(slot_infos[i].picture)];
    }
}
static void refresh_saves(void)
{
    profile_ok = black_SaveRead(profile_data, NULL, 0) && black_profile_slots(profile_data, BLACK_PROFILE_SIZE, profile_slots);
    if (!profile_ok) memset(profile_slots, 0, sizeof profile_slots);
    refresh_slot_infos();
    backup_count = black_SaveBackups(backups, BLACK_SAVE_BACKUPS);
    black_menu_backup_rows = backup_count;
}
/* The slot page's rows: what can be done with this slot. Its checkpoint rows
 * appear only while this profile has a checkpoint file. */
static void refresh_slot_rows(void)
{
    const BlackProfileSlot *s = &profile_slots[slot_open];
    int n = 0;
    slot_checkpoint_ok = profile_ok && s->active && black_CheckpointForSlot(profile_data, slot_open, &slot_checkpoint);
    if (s->active) { black_menu_slot_rows[n++] = ROW_SLOT_MISSIONS;
                     if (slot_checkpoint_ok) { black_menu_slot_rows[n++] = ROW_SLOT_CHECKPOINT;
                                               black_menu_slot_rows[n++] = ROW_SLOT_CHECKPOINT_DELETE; }
                     black_menu_slot_rows[n++] = ROW_SLOT_IMPORT;
                     black_menu_slot_rows[n++] = ROW_SLOT_EXPORT; black_menu_slot_rows[n++] = ROW_SLOT_DELETE; }
    else if (s->damaged) { black_menu_slot_rows[n++] = ROW_SLOT_DELETE; black_menu_slot_rows[n++] = ROW_SLOT_IMPORT; }
    else black_menu_slot_rows[n++] = ROW_SLOT_IMPORT;
    black_menu_slot_count = n;
}
static void add_picks(const char *path, const char *file)
{
    uint8_t data[BLACK_PROFILE_SIZE]; BlackProfileSlot slots[BLACK_PROFILE_SLOTS];
    if (!black_SaveReadImport(path, data) || !black_profile_slots(data, sizeof data, slots)) return;
    for (int i = 0; i < BLACK_PROFILE_SLOTS && pick_count < BLACK_SAVE_IMPORTS; ++i) {
        if (!slots[i].active) continue;
        BlackPick *p = &picks[pick_count++];
        snprintf(p->path, sizeof p->path, "%s", path); snprintf(p->file, sizeof p->file, "%s", file);
        p->slot = i; p->info = slots[i];
    }
}
static void refresh_picks(void)
{
    BlackSaveImport files[BLACK_SAVE_IMPORTS];
    int count = black_SaveImports(files, BLACK_SAVE_IMPORTS);
    pick_count = 0;
    for (int i = 0; i < count; ++i) add_picks(files[i].path, files[i].name);
    black_menu_pick_rows = pick_count;
}
static BOOL slot_in_use(int slot) { return slot == black_SaveSlotInUse(); }
/* Actions that cannot apply now: replacing or deleting the slot the game
 * plays. Drawn grey, skipped by the cursor, ignored by clicks. */
static BOOL row_disabled(int row)
{ return (row == ROW_SLOT_IMPORT || row == ROW_SLOT_DELETE) && slot_in_use(slot_open); }
/* Save Checkpoints follows the game's own Autosave (its Profile options): with
 * Autosave off the row stays selectable, to say why, but is grey and fixed. */
static BOOL checkpoints_locked(void) { return black_CheckpointAutosave() == 0; }
static NSString *profile_label(const BlackProfileSlot *slot)
{ return [NSString stringWithFormat:@"%@ (%d/32)", [@(slot->name) uppercaseString], slot->completed]; }
static NSString *slot_text(const BlackProfileSlot *slot)
{
    return slot->active ? [NSString stringWithFormat:@"%@ · %d %@ 32", [@(slot->name) uppercaseString], slot->completed, T(@"OF")] :
        slot->damaged ? T(@"DAMAGED") : T(@"EMPTY");
}
/* The record's level is the game's level number; the campaign plays them in
 * the order of its table at 0x288648 (0, 1, 5, 3, 4, 6, 7, 8; 2 is loading).
 * Streaming unit 1 starts a mission, so unit n is its checkpoint n - 1. */
static NSString *mission_text(uint32_t level, uint32_t difficulty)
{
    static NSString *const names[4] = {@"EASY", @"NORMAL", @"HARD", @"BLACK OPS"};
    static const int mission_of_level[9] = {1, 2, 0, 4, 5, 3, 6, 7, 8};
    int mission = level < 9 ? mission_of_level[level] : 0;
    return [NSString stringWithFormat:@"%@ · %@ %d", T(names[difficulty < 4 ? difficulty : 0]), T(@"MISSION"), mission];
}
static NSString *checkpoint_text(const BlackCheckpointInfo *info)
{
    return [NSString stringWithFormat:@"%@ · %@ %u", mission_text(info->level, info->difficulty),
        T(@"CHECKPOINT"), info->unit > 1 ? info->unit - 1 : 1];
}
/* The slot whose picture the rail band shows: the open slot, or the one
 * selected in the Saves list. -1 for none. */
static int pictured_slot(int page, int selected)
{
    int slot = page == PAGE_SLOT || page == PAGE_MISSIONS ? slot_open :
        page == PAGE_SAVES && selected >= ROW_SAVE_FIRST && selected <= ROW_SAVE_LAST ? selected - ROW_SAVE_FIRST : -1;
    return slot >= 0 && slot < BLACK_PROFILE_SLOTS && profile_slots[slot].active && slot_pictures[slot] ? slot : -1;
}
static NSString *duration_text(uint64_t seconds)
{
    uint64_t minutes = seconds / 60;
    if (!minutes) return T(@"UNDER A MINUTE");
    return minutes < 60 ? [NSString stringWithFormat:T(@"%d MIN"), (int)minutes] :
        [NSString stringWithFormat:T(@"%d H %02d MIN"), (int)(minutes / 60), (int)(minutes % 60)];
}
/* A run's time as the game's clocks show it: M:SS, or H:MM:SS. */
static NSString *clock_text(double seconds)
{
    long t = seconds > 0 ? (long)(seconds + 0.5) : 0;
    return t >= 3600 ? [NSString stringWithFormat:@"%ld:%02ld:%02ld", t / 3600, t / 60 % 60, t % 60] :
        [NSString stringWithFormat:@"%ld:%02ld", t / 60, t % 60];
}
/* Tenths of a percent; a dash without shots. */
static NSString *accuracy_text(int tenths)
{ return tenths < 0 ? @"—" : [NSString stringWithFormat:@"%d.%d%%", tenths / 10, tenths % 10]; }
static NSString *date_text(int64_t when)
{
    time_t t = (time_t)when; struct tm local; char out[32];
    localtime_r(&t, &local); strftime(out, sizeof out, "%d.%m.%Y %H:%M", &local);
    return @(out);
}
static NSString *const reason_labels[BLACK_SAVE_REASONS] = {@"BEFORE A DELETE", @"BEFORE AN IMPORT", @"BEFORE A RESTORE", @"AT LAUNCH"};
static NSString *const result_labels[3] = {@"DONE", @"BELOW TARGET", @"TARGET MET"};
/* BLACK's own menu palette (black_ui.h): grey text on the selected row, green-grey
 * rows, light rails; TGP's layout and rows are kept, its gold palette is not. */
enum { TEXT = BLACK_UI_TEXT, DIM = BLACK_UI_ROW, HEADING = BLACK_UI_RAIL, HIGHLIGHT = BLACK_UI_TICK,
       TAB = BLACK_UI_BAR, BUTTON = BLACK_UI_BAR, WARN = BLACK_UI_WARN };
static const int aniso_levels[] = {0, 2, 4, 8, 16};
static NSString *aa_name(int mode) { return mode == 2 ? @"SMAA" : mode == 1 ? @"FXAA" : @"OFF"; }
static NSString *af_name(int level) { return level ? [NSString stringWithFormat:@"%dX", level] : @"GAME"; }
/* Display brightness as a signed percent, gamma as its exponent; neutral 0 / 1.00. */
static NSString *brightness_name(void)
{ int b = black_SettingsDisplayBrightness(); return b ? [NSString stringWithFormat:@"%+d", b] : @"0"; }
static NSString *gamma_name(void)
{ return [NSString stringWithFormat:@"%.2f", black_SettingsDisplayGamma() / 100.0]; }
static int active_hid, launch_language;
static NSString *const game_language_names[BLACK_GAME_LANGUAGES] = {@"ENGLISH", @"FRANÇAIS", @"DEUTSCH", @"ESPAÑOL", @"ITALIANO", @"РУССКИЙ"};
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
    NSButton *back, *defaults, *quit;
    int selected, binding_slot, page, axis_slot, editor_page;
    BOOL axis_edit;
    BOOL binding_capture;
    BOOL pad_capture;
    BOOL pad_disconnected;
    NSTimeInterval changed;
}
- (void)openPage:(int)newPage row:(int)row;
- (BOOL)navigateBack;
- (void)changeOption:(id)sender;
- (void)selectRow:(int)row;
- (void)stepTab:(int)direction;
- (void)stepRow:(int)direction;
- (void)resetSelected;
- (void)adjustOption:(int)direction cycle:(BOOL)cycle;
- (void)refreshValues;
- (void)layoutRows;
- (void)captureBinding:(int)key;
- (void)offerTexturePack;
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

/* "A: SKIP" in the bottom right corner while a film can be skipped by
 * button (game_movies.c), with the prompt of the active device, in the same
 * small game font and shadow as the FPS counter. */
static atomic_int film_hint;
static _Atomic double film_hint_seen;
void black_FilmHintSet(int visible)
{
    atomic_store(&film_hint, !!visible);
    if (visible) atomic_store(&film_hint_seen, CACurrentMediaTime());
}
@interface BlackFilmHintView : NSView
@end
@implementation BlackFilmHintView
- (BOOL)isFlipped { return YES; }
- (NSView *)hitTest:(NSPoint)point { (void)point; return nil; }
- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    CGFloat unit = self.bounds.size.height / 480, cap = MAX(8, 7 * unit), margin = 12 * unit;
    char accept[32];
    black_InputGlyphUTF8(black_InputButtonGlyph(0, 1), accept, sizeof accept);
    NSString *value = [NSString stringWithFormat:T(@"%@: SKIP"), @(accept)];
    CGFloat x = self.bounds.size.width - margin - text_width(value, cap, 0), y = self.bounds.size.height - margin - cap * 1.5;
    text(value, x + 1, y + 1, cap, 0x000000, 0);
    text(value, x, y, cap, 0xbababa, 0);
}
@end

/* Holding Home (menu_input.h), or Port Settings > Quit Game: a short dialog
 * in the panel's look over the frozen game. Continue is selected first; in a
 * mission Quit to Main Menu hands over to the pause menu's own confirmation
 * (game_flash.c); Quit Game closes BLACK as Cmd+Q does. */
/* The same dialog asks before F9 / Back + Y restart from the checkpoint
 * (quit_restart: Continue, then Restart from Checkpoint). */
enum { QUIT_CONTINUE, QUIT_MAIN_MENU, QUIT_GAME, QUIT_RESTART };
static int quit_options[3], quit_count, quit_selected, quit_restart;
static NSString *quit_label(int option)
{
    return option == QUIT_MAIN_MENU ? @"QUIT TO MAIN MENU" : option == QUIT_GAME ? @"QUIT GAME" :
        option == QUIT_RESTART ? @"RESTART FROM CHECKPOINT" : @"CONTINUE";
}
static NSString *quit_help(int option)
{
    return option == QUIT_MAIN_MENU ? @"THE GAME THEN ASKS TO CONFIRM. CHECKPOINT PROGRESS IS LOST." :
        option == QUIT_GAME ? @"CLOSES BLACK. PROGRESS SINCE THE LAST SAVE IS LOST." :
        option == QUIT_RESTART ? @"BACK TO THE LAST CHECKPOINT. PROGRESS SINCE THEN IS LOST." : @"BACK TO THE GAME.";
}
/* Laid out as BLACK's own pause page, measured on its frames in these
 * 960x600 panel units: the light rails at 160 and 352-371 with a black band
 * between them, the gold BLACK mark at 432, options at 15 units, 31 apart,
 * in the page's grey-green, the selector line from the rail to the screen
 * edge, prompts at the bottom left. It slides in from the right as the
 * game's pages do; the selector glides between options. */
static const CGFloat quit_rail_left = 160, quit_rail_right = 371, quit_column = 426;
enum { QUIT_TITLE_Y = 110, QUIT_MESSAGE_Y = 146, QUIT_GAP = 22, QUIT_ROW_STEP = 31, QUIT_ROW_HEIGHT = 30 };
static const unsigned quit_row_colour = 0x6f9585;
static NSRect quit_row_rect(int index)
{ return NSMakeRect(quit_column, QUIT_MESSAGE_Y + 12 + QUIT_GAP + index * QUIT_ROW_STEP, 313, QUIT_ROW_HEIGHT); }
static double quit_opened, quit_closed, quit_moved, quit_from_y;
static int quit_closing;
static BOOL reduced_motion(void);
static double quit_ease(double t) { t = t < 0 ? 0 : t > 1 ? 1 : t; return 1 - pow(1 - t, 3); }
@interface BlackQuitView : NSView
@end
static void quit_choose(int index);
static NSArray<NSString *> *quit_lines(NSString *message, CGFloat size, CGFloat width)
{
    NSMutableArray *lines = [NSMutableArray array];
    NSString *current = @"";
    for (NSString *word in [message componentsSeparatedByString:@" "]) {
        NSString *next = current.length ? [current stringByAppendingFormat:@" %@", word] : word;
        if (current.length && text_width(next, size, 0) > width) { [lines addObject:current]; current = word; }
        else current = next;
    }
    if (current.length) [lines addObject:current];
    return lines;
}
/* The BLACK mark: wide heavy capitals in a worn gold, lighter at the top,
 * over a dark offset, close to the title screen's logo without its art. */
static void quit_mark(CGFloat x, CGFloat y)
{
    NSFont *font = [NSFont fontWithName:@"Arial-Black" size:34] ?: [NSFont systemFontOfSize:34 weight:NSFontWeightHeavy];
    NSDictionary *shadow = @{NSFontAttributeName: font, NSKernAttributeName: @(-0.5),
        NSForegroundColorAttributeName: [NSColor colorWithWhite:0 alpha:.85]};
    NSSize plain = [@"BLACK" sizeWithAttributes:shadow];
    NSImage *dark = [[[NSImage alloc] initWithSize:plain] autorelease];
    [dark lockFocus]; [@"BLACK" drawAtPoint:NSZeroPoint withAttributes:shadow]; [dark unlockFocus];
    [dark drawInRect:NSMakeRect(x + 2, y + 2, 203, plain.height) fromRect:NSZeroRect operation:NSCompositingOperationSourceOver
        fraction:1 respectFlipped:YES hints:nil];
    NSMutableAttributedString *mark = [[[NSMutableAttributedString alloc] initWithString:@"BLACK" attributes:@{
        NSFontAttributeName: font, NSKernAttributeName: @(-0.5), NSForegroundColorAttributeName: NSColor.blackColor}] autorelease];
    NSSize size = mark.size;
    /* Clip to the glyphs, then fill them with the gradient. */
    NSImage *mask = [[[NSImage alloc] initWithSize:size] autorelease];
    [mask lockFocus]; [mark drawAtPoint:NSZeroPoint]; [mask unlockFocus];
    NSImage *gold = [[[NSImage alloc] initWithSize:size] autorelease];
    [gold lockFocus];
    NSGradient *g = [[[NSGradient alloc] initWithColorsAndLocations:
        black_ui_color(0x7c6a44, 1), 0.0, black_ui_color(0xb59a62, 1), 0.55, black_ui_color(0xe2cf9c, 1), 1.0, nil] autorelease];
    [g drawInRect:NSMakeRect(0, 0, size.width, size.height) angle:90];
    [mask drawInRect:NSMakeRect(0, 0, size.width, size.height) fromRect:NSZeroRect operation:NSCompositingOperationDestinationIn fraction:1];
    [gold unlockFocus];
    /* The game's mark is wider than any system face: stretch it to its 203 units. */
    CGFloat wide = 203 / size.width;
    [gold drawInRect:NSMakeRect(x, y, size.width * wide, size.height) fromRect:NSZeroRect operation:NSCompositingOperationSourceOver
        fraction:1 respectFlipped:YES hints:nil];
}
@implementation BlackQuitView
- (BOOL)isFlipped { return YES; }
- (NSAffineTransform *)panelTransform
{
    NSSize size = self.bounds.size;
    CGFloat scale = MIN(size.width / 960.0, size.height / 600.0);
    NSAffineTransform *t = [NSAffineTransform transform];
    [t translateXBy:(size.width - 960 * scale) / 2 yBy:(size.height - 600 * scale) / 2];
    [t scaleBy:scale];
    return t;
}
- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    double now = CACurrentMediaTime(), still = reduced_motion();
    double in = still ? 1 : quit_ease((now - quit_opened) / 0.22);
    double out = quit_closing ? (still ? 0 : 1 - quit_ease((now - quit_closed) / 0.14)) : 1;
    CGFloat alpha = in * out, slide = (1 - in) * 48;
    NSSize size = self.bounds.size;
    CGFloat scale = MIN(size.width / 960.0, size.height / 600.0), ox = (size.width - 960 * scale) / 2, oy = (size.height - 600 * scale) / 2;
    CGFloat left = -ox / scale - 2, right = 960 + ox / scale + 2, top = -oy / scale - 2, bottom = 600 + oy / scale + 2;
    [NSGraphicsContext saveGraphicsState];
    [[self panelTransform] concat];
    CGContextSetAlpha(NSGraphicsContext.currentContext.CGContext, alpha);
    /* As on the pause page: the scene dimmed, the band between the rails black. */
    fill(NSMakeRect(left, top, quit_rail_left - left, bottom - top), BLACK_UI_BACKDROP, .84);
    fill(NSMakeRect(quit_rail_left, top, quit_rail_right - quit_rail_left, bottom - top), 0x010201, .93);
    fill(NSMakeRect(quit_rail_right, top, right - quit_rail_right, bottom - top), BLACK_UI_BACKDROP, .82);
    fill(NSMakeRect(quit_rail_left - 2, top, 6, bottom - top), BLACK_UI_RAIL, .95);
    fill(NSMakeRect(quit_rail_left + 11, top, 1.5, bottom - top), BLACK_UI_RAIL, .8);
    fill(NSMakeRect(quit_rail_right - 19, top, 1.5, bottom - top), BLACK_UI_RAIL, .8);
    fill(NSMakeRect(quit_rail_right - 14, top, 1.5, bottom - top), BLACK_UI_RAIL, .8);
    fill(NSMakeRect(quit_rail_right - 6, top, 6, bottom - top), BLACK_UI_RAIL, .95);
    for (CGFloat y = top + 4; y < bottom; y += 12.5) {
        fill(NSMakeRect(quit_rail_left - 10, y, 8, 1), BLACK_UI_RAIL, .5);
        fill(NSMakeRect(quit_rail_right, y, 8, 1), BLACK_UI_RAIL, .5);
    }
    NSAffineTransform *shift = [NSAffineTransform transform];
    [shift translateXBy:slide yBy:0]; [shift concat];
    /* The gold mark, the page title under it. */
    CGFloat x = quit_column + 6;
    quit_mark(x, 62);
    text(T(quit_restart ? @"RESTART" : @"QUIT"), x, QUIT_TITLE_Y, 22, 0xe6e6e6, 1);
    int option = quit_options[quit_selected];
    fitted_text(T(quit_restart ? @"RESTART FROM THE LAST CHECKPOINT?" : quit_count == 3 ? @"LEAVE THE MISSION OR QUIT BLACK?" : @"QUIT BLACK?"),
        x, QUIT_MESSAGE_Y, 12, 300, 0xd8d8d8, 0);
    /* The selector glides to the chosen option. */
    CGFloat target = quit_row_rect(quit_selected).origin.y;
    double glide = still ? 1 : quit_ease((now - quit_moved) / 0.12);
    CGFloat bar_y = quit_from_y + (target - quit_from_y) * glide;
    black_ui_selector(NSMakeRect(quit_column, bar_y, 313, QUIT_ROW_HEIGHT), quit_rail_right - slide, right);
    for (int i = 0; i < quit_count; ++i) {
        NSRect r = quit_row_rect(i);
        fitted_text(T(quit_label(quit_options[i])), x, r.origin.y + 7.5, 15, r.size.width - 20,
            i == quit_selected ? 0xf2f2f2 : quit_row_colour, 0);
    }
    NSArray<NSString *> *note = quit_lines(T(quit_help(option)), 10, 300);
    CGFloat note_y = NSMaxY(quit_row_rect(quit_count - 1)) + QUIT_GAP - 6;
    for (NSUInteger i = 0; i < note.count && i < 2; ++i) text(note[i], x, note_y + i * 15, 10, quit_row_colour, 0);
    /* Prompts as the pause page shows them: the buttons in one column, the words aligned after it. */
    char accept[32], cancel[32];
    black_InputGlyphUTF8(black_InputButtonGlyph(0, 1), accept, sizeof accept);
    black_InputGlyphUTF8(black_InputButtonGlyph(1, 1), cancel, sizeof cancel);
    NSString *keys[3] = {@"", @(accept), @(cancel)};
    NSString *words[3] = {T(@"SELECT"), T(@"CONFIRM"), T(@"BACK")};
    CGFloat column = 26;
    for (int i = 1; i < 3; ++i) column = MAX(column, text_width(keys[i], 14, 0));
    CGFloat px = quit_rail_right + 29 - slide;
    for (int i = 0; i < 3; ++i) {
        CGFloat y = 498 + i * 33;
        if (i == 0) {
            /* The pause page's up and down triangles. */
            for (int t = 0; t < 2; ++t) {
                CGFloat tx = px + column - 25 + t * 13, mid = tx + 5.5;
                NSBezierPath *path = [NSBezierPath bezierPath];
                if (!t) { [path moveToPoint:NSMakePoint(mid, y + 1)]; [path lineToPoint:NSMakePoint(tx + 11, y + 13)]; [path lineToPoint:NSMakePoint(tx, y + 13)]; }
                else { [path moveToPoint:NSMakePoint(tx, y + 1)]; [path lineToPoint:NSMakePoint(tx + 11, y + 1)]; [path lineToPoint:NSMakePoint(mid, y + 13)]; }
                [path closePath]; [black_ui_color(0xf2f2f2, 1) setFill]; [path fill];
            }
        } else text(keys[i], px + column - text_width(keys[i], 14, 0), y, 14, 0xf2f2f2, 0);
        text(words[i], px + column + 10, y, 14, 0xf2f2f2, 0);
    }
    [NSGraphicsContext restoreGraphicsState];
}
- (void)mouseDown:(NSEvent *)event
{
    NSAffineTransform *t = [self panelTransform]; [t invert];
    NSPoint p = [t transformPoint:[self convertPoint:event.locationInWindow fromView:nil]];
    for (int i = 0; i < quit_count; ++i) if (NSPointInRect(p, quit_row_rect(i))) { quit_choose(i); return; }
}
- (void)mouseMoved:(NSEvent *)event
{
    NSAffineTransform *t = [self panelTransform]; [t invert];
    NSPoint p = [t transformPoint:[self convertPoint:event.locationInWindow fromView:nil]];
    for (int i = 0; i < quit_count; ++i)
        if (NSPointInRect(p, quit_row_rect(i)) && quit_selected != i) {
            quit_from_y = quit_row_rect(quit_selected).origin.y; quit_moved = CACurrentMediaTime();
            quit_selected = i; [self setNeedsDisplay:YES];
        }
}
@end

static NSWindow *menu_window;
static NSView *game_view, *container;
static BlackMenuView *menu_view;
static BlackFpsView *fps_view;
static BlackFilmHintView *film_hint_view;
static BlackQuitView *quit_view;
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
            @"Black level over the game's own brightness, -20 to +20; 0 keeps the game's picture. Live.",
            @"Midtones over the game's own brightness, 0.60 to 1.60; 1.00 keeps the game's picture. Live.",
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
            [rows[i] setAccessibilityHelp:black_menu_save(i)?@"Saves: Enter twice confirms a delete, restore or import. Applied only on the game's main menu; a backup is kept.":i==ROW_QUIT?@"Open the quit dialog: main menu or quit BLACK. Holding Home does the same.":black_menu_link(i)?@"Open a controls subpage. Back returns to this row.":
                black_menu_pair(i)?@"Choose X or Y with Left and Right, press Enter to edit. Back finishes editing.":
                i==ROW_EDIT_DEVICE?@"Choose the bindings to edit; gameplay device is unchanged.":i <= ROW_RESET ? help[i] : i < ROW_MOUSE_X ?
                @"Choose the input device and prompt family, or reassign a controller button. Escape cancels capture; Delete clears a binding." :
                i >= ROW_ANY_FILM && i <= ROW_MOD_LAST ? (i == ROW_INTROS ? @"Skip startup logos and intro films without a press." :
                    i == ROW_MISSIONS ? @"Skip mission briefings without a press." :
                    i == ROW_ANY_FILM ? @"A, Start or Enter skips any film; a hint shows the button." :
                    [mod_help[i - ROW_MOD_FIRST][0] stringByAppendingFormat:@" %@", mod_help[i - ROW_MOD_FIRST][1]].capitalizedString) :
                is_binding(i) ? @"Two key slots. Left and Right choose a slot. Enter waits for a key or mouse button. Escape cancels, Delete clears." :
                @"Mouse and keyboard preferences. Applied live and saved automatically."];
            [self addSubview:rows[i]]; [rows[i] release];
        }
        for (int i = 0; i < MENU_TABS; ++i) {
            categories[i] = [[BlackMenuButton alloc] initWithFrame:NSZeroRect];
            categories[i].title = tab_names[i].capitalizedString; categories[i].tag = black_menu_top(i);
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
        quit = [[BlackMenuButton alloc] initWithFrame:NSZeroRect];
        quit.title = @"Quit Game"; quit.target = self; quit.action = @selector(quitGame:);
        [quit setAccessibilityHelp:@"Open the quit dialog: main menu or quit BLACK. Holding Home does the same."];
        [self addSubview:quit]; [quit release];
        page=PAGE_ROOT;editor_page=PAGE_KEYS;
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
    /* The menu's layout, as drawRect places it right of the band. */
    BlackPageGeometry g = page_geometry(size);
    CGFloat ox = g.ox + g.dx * g.scale, oy = g.oy + g.dy * g.scale, scale = g.scale * g.k, yscale = g.scale * g.ky;
    int start = visible_start(page,selected);
    for (int i = 0; i < MENU_ROWS; ++i) {
        int index=black_menu_index(page,i);
        rows[i].hidden=index<start||index>=start+MENU_TAB_MAX_ROWS||index<0;
        rows[i].frame = NSMakeRect(ox + 84 * scale, oy + row_y(page,i,selected) * yscale,
            (page == PAGE_DIAGNOSTICS ? 600 : 792) * scale, 38 * yscale);
    }
    for (int i = 0; i < MENU_TABS; ++i)
        categories[i].frame = NSMakeRect(ox + (84 + i * TAB_WIDTH) * scale, oy + 82 * yscale, TAB_WIDTH * scale, 38 * yscale);
    /* No buttons under the list: Back is its hint, which a click also takes. */
    back.frame = NSMakeRect(ox + back_hint_x * scale, oy + back_hint_y * yscale, back_hint_w * scale, 30 * yscale);
    back.alphaValue = 0; quit.hidden = YES;
    defaults.hidden=YES;
    for (int i = 0; i < MENU_TABS; ++i) categories[i].hidden = YES;
    for (int i = ROW_SLOT_MISSIONS; i <= ROW_SLOT_DELETE; ++i) rows[i].enabled = !row_disabled(i);
    defaults.frame = NSMakeRect(ox + 738 * scale, oy + 526 * yscale, 138 * scale, 30 * yscale);
}
- (void)refreshValues
{
    NSString *reset=page==PAGE_KEYS||page==PAGE_PAD_BINDINGS?@"RESET BINDINGS":page==PAGE_MOUSE?@"RESET MOUSE":page==PAGE_PAD?@"RESET GAMEPAD":page==PAGE_ADVANCED?@"RESET ADVANCED":@"DEFAULTS";
    [defaults setAccessibilityLabel:T(reset)];
    [rows[ROW_EDIT_DEVICE] setAccessibilityValue:T(page==PAGE_PAD_BINDINGS?@"GAMEPAD":@"KEYBOARD AND MOUSE")];
    for(int i=0;i<4;++i){int mouse=i<2,axis=(i%2)*2;
        [rows[ROW_MOUSE_LOOK_PAIR+i] setAccessibilityValue:[NSString stringWithFormat:@"X %.2f, Y %.2f",(mouse?black_SettingsMouseSpeed(axis):black_SettingsLookSpeed(axis))/50.,(mouse?black_SettingsMouseSpeed(axis+1):black_SettingsLookSpeed(axis+1))/50.]];
    }

    [rows[ROW_INTROS] setAccessibilityValue:black_SettingsSkipIntros() ? @"On" : @"Off"];
    [rows[ROW_MISSIONS] setAccessibilityValue:black_SettingsSkipMissionIntros() ? @"On" : @"Off"];
    [rows[ROW_ANY_FILM] setAccessibilityValue:black_SettingsSkipAnyFilm() ? @"On" : @"Off"];
    [rows[ROW_CHECKPOINTS] setAccessibilityValue:checkpoints_locked() ? @"Unavailable, the game's autosave is off" : black_SettingsSaveCheckpoints() ? @"On" : @"Off"];
    if (slot_checkpoint_ok) [rows[ROW_SLOT_CHECKPOINT] setAccessibilityValue:checkpoint_text(&slot_checkpoint)];
    [rows[ROW_SCALE] setAccessibilityValue:[NSString stringWithFormat:@"%d times, next launch", black_SettingsRenderScale()]];
    [rows[ROW_UPSCALE] setAccessibilityValue:black_SettingsUpscaler() ? @"MetalFX, next launch" : @"Off, next launch"];
    [rows[ROW_GAME_LANG] setAccessibilityValue:[NSString stringWithFormat:@"%@, next launch", game_language_names[black_SettingsGameLanguage()].capitalizedString]];
    [rows[ROW_MENU_LANG] setAccessibilityValue:menu_language_names[black_SettingsMenuLanguage()].capitalizedString];
    [rows[ROW_FPS] setAccessibilityValue:black_SettingsFpsCounter() ? @"On" : @"Off"];
    [rows[ROW_BRIGHTNESS] setAccessibilityValue:brightness_name()];
    [rows[ROW_GAMMA] setAccessibilityValue:gamma_name()];
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
    [rows[ROW_DEVICE] setAccessibilityValue:T((@[@"AUTO",@"GAMEPAD",@"KEYBOARD AND MOUSE"])[black_SettingsInputDevice()])];
    [rows[ROW_PROMPTS] setAccessibilityValue:T((@[@"AUTO",@"XBOX",@"PLAYSTATION",@"KEYBOARD"])[black_SettingsButtonPrompts()])];
    for (int i = 0; i < BLACK_PAD_CONTROLS; ++i) [rows[ROW_PAD_FIRST+i] setAccessibilityValue:
        @(black_PadButtonName(black_SettingsPadBinding(i),black_InputPromptFamily()))];
    [rows[ROW_MEASURE] setAccessibilityValue:[NSString stringWithFormat:@"Measurement state %d", black_ControllerMonitor().calibration]];
}
- (void)goBack:(id)sender { (void)sender; black_MenuCommand(BLACK_MENU_BACK); }
- (void)quitGame:(id)sender { (void)sender; black_MenuCommand(BLACK_MENU_QUIT); }
- (void)restoreDefaults:(id)sender
{
    (void)sender;black_ControllerCancelMeasure();
    binding_capture=pad_capture=axis_edit=NO;black_PadCaptureCancel();
    if(page==PAGE_KEYS){
        for(int i=0;i<BLACK_ACTION_COUNT;++i)for(int slot=0;slot<2;++slot)
            black_SettingsSetBinding(i,slot,black_KeyActions()[i].keys[slot]);
    }else if(page==PAGE_PAD_BINDINGS){
        for(int i=0;i<16;++i)black_SettingsSetPadBinding(i,i);
    }else if(page==PAGE_MOUSE){
        for(int i=0;i<4;++i)black_SettingsSetMouseSpeed(i,50);
        for(int i=0;i<2;++i)black_SettingsSetMouseInvert(i,0);
        black_SettingsSetMouseHoldAim(1);black_SettingsSetWalkSpeed(50);
    }else if(page==PAGE_PAD){
        for(int i=0;i<4;++i)black_SettingsSetLookSpeed(i,50);
        black_SettingsSetHoldAim(0);black_SettingsSetRumbleIntensity(100);black_SettingsSetVibration(1);
    }else if(page==PAGE_ADVANCED){
        black_SettingsSetInvertX(0);black_SettingsSetAimAssist(1);
        for(int i=0;i<2;++i)black_SettingsSetStickDeadzone(i,-1);
        black_SettingsSetTriggerDeadzone(0);black_SettingsSetVibration(1);black_SettingsSetDualSenseHid(1);
    }else if(page==PAGE_MODS)black_SettingsResetMods();
    else if(page==PAGE_PORT){
        black_SettingsSetRenderScale(2);black_SettingsSetUpscaler(1);black_SettingsSetFov(70);
        black_SettingsSetAntiAliasing(2);black_SettingsSetAniso(0);black_SettingsSetFpsCounter(0);
        black_SettingsSetDisplayBrightness(BLACK_BRIGHTNESS_NEUTRAL);black_SettingsSetDisplayGamma(BLACK_GAMMA_NEUTRAL);
        black_SettingsSetTextureReplacement(0);black_SettingsSetTextureMaxSize(2048);
    }
    changed=CACurrentMediaTime();[self refreshValues];[self setNeedsDisplay:YES];
}
- (void)openPage:(int)newPage row:(int)row
{
    black_ControllerCancelMeasure();page=newPage;
    if(page==PAGE_KEYS||page==PAGE_PAD_BINDINGS)editor_page=page;
    if(black_menu_tab(page)==3&&page!=PAGE_CONFIRM){refresh_saves();refresh_slot_rows();}
    if(page==PAGE_IMPORT_PICK)refresh_picks();
    [self selectRow:black_menu_index(page,row)>=0?row:black_menu_row(page,0)];
}
- (BOOL)navigateBack
{
    if(binding_capture){[self captureBinding:-2];return YES;}
    if(pad_capture){pad_capture=NO;black_PadCaptureCancel();[self setNeedsDisplay:YES];return YES;}
    if(axis_edit){axis_edit=NO;[self setNeedsDisplay:YES];return YES;}
    int row,parent=black_menu_parent(page,&row);
    if(page==PAGE_SLOT)row=ROW_SAVE_FIRST+slot_open;
    if(page==PAGE_BACKUP)row=ROW_BACKUP_FIRST+backup_open;
    if(page==PAGE_CONFIRM)confirm_kind=0;
    if(page==PAGE_CONFIRM&&save_waiting)return YES;
    if(parent<0)return NO;
    [self openPage:parent row:row];
    return YES;
}
- (void)selectRow:(int)row
{
    binding_capture=pad_capture=axis_edit=NO;black_PadCaptureCancel();
    selected=black_menu_index(page,row)>=0?row:black_menu_row(page,0);axis_slot=0;
    [self layoutRows];
    for(int i=0;i<MENU_TABS;++i)[categories[i] setAccessibilityValue:i==black_menu_tab(page)?@"Selected":@""];
    [self refreshValues];
    if(menu_window&&black_SettingsPanelOpen())[menu_window makeFirstResponder:rows[selected]];
    [self setNeedsDisplay:YES];
}
- (void)changeCategory:(id)sender{[self openPage:(int)[sender tag] row:-1];}
- (void)stepTab:(int)direction
{
    int tab=black_menu_tab(page);
    if(tab<0)tab=direction>0?MENU_TABS-1:0;
    [self openPage:black_menu_top((tab+direction+MENU_TABS)%MENU_TABS) row:-1];
}
- (void)resetSelected
{
    if (binding_capture || pad_capture || !reset_row(selected, 0)) return;
    axis_edit = NO; reset_row(selected, 1);
    changed = CACurrentMediaTime(); [self refreshValues]; [self setNeedsDisplay:YES];
}
- (void)stepRow:(int)direction
{
    int count=black_menu_count(page),index=black_menu_index(page,selected);
    for(int tries=0;tries<count;++tries){
        index=(index+direction+count)%count;
        if(!row_disabled(black_menu_row(page,index)))break;
    }
    [self selectRow:black_menu_row(page,index)];
}
- (void)captureBinding:(int)key
{
    if (!binding_capture || !is_binding(selected)) return;
    if (key == -2) binding_capture = NO;
    else if (black_KeyValid(key)) {
        black_SettingsSetBinding(selected - ROW_BIND_FIRST, binding_slot, key);
        binding_capture = NO; pad_capture = NO; black_PadCaptureCancel(); changed = CACurrentMediaTime();
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
        BlackPageGeometry g = page_geometry(size);
        CGFloat x = ((point.x - g.ox) / g.scale - g.dx) / g.k; (void)scale;
        if (selected >= ROW_PAD_FIRST && selected <= ROW_PAD_LAST) {
            if (x >= 600 && x < 876) [self adjustOption:1 cycle:YES];
            return;
        }
        if (is_binding(selected)) {
            if (x >= 600 && x < 876) {
                binding_slot = x >= 738; binding_capture = YES; black_KeyReleaseAll(); [self setNeedsDisplay:YES];
            }
            return;
        }
        if(black_menu_link(selected)||selected==ROW_EDIT_DEVICE||selected==ROW_QUIT||black_menu_save(selected)){[self adjustOption:1 cycle:YES];return;}
        if(black_menu_pair(selected)){
            if(x>=600&&x<876){axis_slot=x>=738;axis_edit=YES;
                CGFloat local=x-(axis_slot?738:600);
                if(local<24)[self adjustOption:-1 cycle:NO];else if(local>108)[self adjustOption:1 cycle:NO];
                [self setNeedsDisplay:YES];}
            return;
        }
        CGFloat left=selected==ROW_DEVICE||selected==ROW_PROMPTS?500:VALUE_LEFT;
        CGFloat selector_width=selected==ROW_DEVICE||selected==ROW_PROMPTS?376:VALUE_WIDTH;
        if (x >= left && x < left + 40) { [self adjustOption:-1 cycle:NO]; return; }
        if (x >= left + selector_width - 40 && x < left + selector_width) { [self adjustOption:1 cycle:NO]; return; }
        return;
    }
    [self adjustOption:1 cycle:YES];
}
- (void)adjustOption:(int)direction cycle:(BOOL)cycle
{
    if(selected==ROW_QUIT){if(cycle)black_MenuCommand(BLACK_MENU_QUIT);return;}
    if(selected==ROW_CLOSE_MENU){if(cycle)black_MenuCommand(BLACK_MENU_TOGGLE);return;}
    if(selected==ROW_DEFAULTS){
        if(!cycle)return;
        [self confirm:[NSString stringWithFormat:T(@"PUT %@ BACK TO THE RECOMMENDED SETTINGS?"),T(page_names[page])]
            detail:T(@"ONLY THIS PAGE'S SETTINGS CHANGE.") reason:0];
        confirm_kind=3;return;
    }
    if(selected==ROW_RESET_ALL){
        if(!cycle)return;
        [self confirm:T(@"PUT EVERY SETTING BACK TO THE RECOMMENDED ONE?")
            detail:T(@"GRAPHICS, CONTROLS, BINDINGS AND MODS. THE GAME'S LANGUAGE AND YOUR SAVES STAY.") reason:0];
        confirm_kind=4;return;
    }
    if(selected==ROW_CHECKPOINTS){
        if(checkpoints_locked())return;
        int current=black_SettingsSaveCheckpoints(),value=cycle?!current:direction>0;
        if(value==current)return;
        black_SettingsSetSaveCheckpoints(value);
        changed=CACurrentMediaTime();[self refreshValues];[self setNeedsDisplay:YES];return;
    }
    if(selected==ROW_SLOT_CHECKPOINT)return; /* the slot's newest checkpoint, shown */
    if(selected==ROW_SLOT_CHECKPOINT_DELETE){
        if(!cycle)return;
        [self confirm:[NSString stringWithFormat:T(@"DELETE THE CHECKPOINT OF %@?"),profile_label(&profile_slots[slot_open])]
            detail:T(@"THE PROFILE AND ITS MISSIONS STAY. THE GAME STARTS THIS PROFILE'S NEXT MISSION FROM ITS BEGINNING.") reason:0];
        confirm_kind=5;return;
    }
    if(black_menu_save(selected)){
        if(!cycle||save_waiting||row_disabled(selected))return;
        set_note(nil);
        if(selected>=ROW_SAVE_FIRST&&selected<=ROW_SAVE_LAST){
            if(!profile_ok){set_note(@"NO PROFILE YET: THE GAME CREATES ONE WHEN IT FIRST SAVES.");[self setNeedsDisplay:YES];return;}
            slot_open=selected-ROW_SAVE_FIRST;[self openPage:PAGE_SLOT row:-1];return;
        }
        const BlackProfileSlot *slot=&profile_slots[slot_open];
        if(selected==ROW_SLOT_MISSIONS){[self openPage:PAGE_MISSIONS row:-1];return;}
        if(selected==ROW_SLOT_EXPORT){
            char path[1024];
            if(black_SaveExport(profile_data,slot_open,path,sizeof path)){
                set_note(@"EXPORTED TO THE EXPORT FOLDER, SHOWN IN FINDER. IT IMPORTS LIKE ANY SAVE.");
                [NSWorkspace.sharedWorkspace activateFileViewerSelectingURLs:@[[NSURL fileURLWithPath:@(path)]]];
            }else set_note(@"COULD NOT EXPORT THIS PROFILE.");
            [self setNeedsDisplay:YES];return;
        }
        if(selected==ROW_SLOT_IMPORT||selected==ROW_SLOT_DELETE){
            if(slot_in_use(slot_open)){set_note(@"THE GAME PLAYS THIS PROFILE AND AUTOSAVES TO IT. LOAD ANOTHER PROFILE IN THE GAME FIRST.");[self setNeedsDisplay:YES];return;}
            if(selected==ROW_SLOT_IMPORT){[self openPage:PAGE_IMPORT_PICK row:-1];return;}
            memcpy(confirm_edited,profile_data,sizeof confirm_edited);
            if(!black_profile_clear(confirm_edited,slot_open))return;
            [self confirm:slot->damaged?[NSString stringWithFormat:T(@"CLEAR SLOT %d?"),slot_open+1]:
                    [NSString stringWithFormat:T(@"DELETE %@ FROM SLOT %d?"),profile_label(slot),slot_open+1]
                detail:T(@"THE SLOT BECOMES EMPTY. THE FILE AS IT IS NOW IS KEPT IN THE BACKUPS.") reason:BLACK_SAVE_BEFORE_DELETE];
            return;
        }
        if(selected==ROW_PICK_FINDER){[self chooseImport];return;}
        if(selected==ROW_PICK_FOLDER){
            char folder[1024];
            if(black_SaveImportFolder(folder,sizeof folder))[NSWorkspace.sharedWorkspace openURL:[NSURL fileURLWithPath:@(folder) isDirectory:YES]];
            return;
        }
        if(selected>=ROW_PICK_FIRST&&selected<=ROW_PICK_LAST){[self choosePick:selected-ROW_PICK_FIRST];return;}
        if(selected>=ROW_BACKUP_FIRST&&selected<=ROW_BACKUP_LAST){
            if(selected-ROW_BACKUP_FIRST>=backup_count)return;
            backup_open=selected-ROW_BACKUP_FIRST;[self openPage:PAGE_BACKUP row:-1];return;
        }
        if(selected==ROW_BACKUP_RESTORE){
            int at=backup_open;
            if(at>=backup_count||!black_SaveReadBackup(backups[at].path,confirm_edited)){set_note(@"THIS BACKUP COULD NOT BE READ.");[self setNeedsDisplay:YES];return;}
            NSMutableArray *names=[NSMutableArray array];
            for(int i=0;i<BLACK_PROFILE_SLOTS;++i)if(backups[at].slots[i].active)[names addObject:profile_label(&backups[at].slots[i])];
            confirm_kind=0;
            [self confirm:[NSString stringWithFormat:T(@"PUT THE PROFILES BACK AS ON %s?"),backups[at].when]
                detail:[NSString stringWithFormat:@"%@: %@. %@",T(@"SLOTS"),names.count?[names componentsJoinedByString:@", "]:T(@"EMPTY"),
                    T(@"THE FILE AS IT IS NOW IS KEPT IN THE BACKUPS.")] reason:BLACK_SAVE_BEFORE_RESTORE];
            return;
        }
        if(selected==ROW_BACKUP_DELETE||selected==ROW_BACKUPS_CLEAR){
            BOOL all=selected==ROW_BACKUPS_CLEAR;
            [self confirm:all?[NSString stringWithFormat:T(@"DELETE ALL %d BACKUPS?"),backup_count]:
                    [NSString stringWithFormat:T(@"DELETE THE BACKUP OF %s?"),backups[backup_open].when]
                detail:T(@"BACKUPS GO TO THE TRASH, WHERE THEY CAN BE PUT BACK. YOUR PROFILE IS NOT CHANGED.") reason:BLACK_SAVE_BEFORE_RESTORE];
            confirm_kind=all?2:1;
            return;
        }
        if(selected==ROW_CONFIRM_YES&&confirm_kind==4){
            static const int every[]={PAGE_PORT,PAGE_KEYS,PAGE_PAD_BINDINGS,PAGE_MOUSE,PAGE_PAD,PAGE_ADVANCED,PAGE_MODS};
            confirm_kind=0;
            for(size_t i=0;i<sizeof every/sizeof every[0];++i){page=every[i];[self restoreDefaults:nil];}
            black_SettingsSetInputDevice(0);black_SettingsSetButtonPrompts(0);black_SettingsSetMenuLanguage(0);
            fprintf(stderr,"[MENU] every setting back to the recommended one\n");
            [self openPage:PAGE_PORT row:ROW_RESET_ALL];return;
        }
        if(selected==ROW_CONFIRM_YES&&confirm_kind==3){
            confirm_kind=0;page=black_menu_confirm_page;[self restoreDefaults:nil];
            [self openPage:page row:ROW_DEFAULTS];return;
        }
        if(selected==ROW_CONFIRM_YES&&confirm_kind==5){
            confirm_kind=0;
            int n=black_CheckpointRemoveSlot(profile_data,slot_open);
            set_note(n?[NSString stringWithFormat:T(@"%d CHECKPOINT FILES DELETED."),n]:@"NOTHING WAS DELETED.");
            [self openPage:PAGE_SLOT row:-1];[self setNeedsDisplay:YES];return;
        }
        if(selected==ROW_CONFIRM_YES&&confirm_kind){
            int n=confirm_kind==2?black_SaveBackupsTrashAll():black_SaveBackupTrash(backups[backup_open].path);
            confirm_kind=0;
            set_note(n?[NSString stringWithFormat:T(@"%d MOVED TO THE TRASH."),n]:@"NOTHING WAS DELETED.");
            [self openPage:PAGE_BACKUPS row:-1];return;
        }
        if(selected==ROW_CONFIRM_YES){
            black_SaveRequest(profile_data,confirm_edited,confirm_reason);save_waiting=1;save_outcome=0;
            [self setNeedsDisplay:YES];return;
        }
        if(selected==ROW_CONFIRM_NO){[self navigateBack];return;}
        return;
    }
    if(black_menu_link(selected)){
        if(!cycle)return;
        int target=selected==ROW_NAV_BINDINGS?editor_page:selected==ROW_NAV_MOUSE?PAGE_MOUSE:
            selected==ROW_NAV_PAD?PAGE_PAD:selected==ROW_NAV_ADVANCED?PAGE_ADVANCED:selected==ROW_NAV_BACKUPS?PAGE_BACKUPS:
            selected==ROW_NAV_PORT?PAGE_PORT:selected==ROW_NAV_CONTROLS?PAGE_CONTROLS:selected==ROW_NAV_MODS?PAGE_MODS:
            selected==ROW_NAV_SAVES?PAGE_SAVES:PAGE_DIAGNOSTICS;
        [self openPage:target row:-1];return;
    }
    if(selected==ROW_EDIT_DEVICE){
        int next=cycle?(page==PAGE_KEYS?PAGE_PAD_BINDINGS:PAGE_KEYS):direction>0?PAGE_PAD_BINDINGS:PAGE_KEYS;
        [self openPage:next row:ROW_EDIT_DEVICE];return;
    }
    if(black_menu_pair(selected)){
        if(cycle){axis_edit=!axis_edit;[self setNeedsDisplay:YES];return;}
        if(!axis_edit){axis_slot=direction>0;[self setNeedsDisplay:YES];return;}
        int mouse=selected<=ROW_MOUSE_AIM_PAIR;
        int axis=((selected-ROW_MOUSE_LOOK_PAIR)%2)*2+axis_slot;
        int value=(mouse?black_SettingsMouseSpeed(axis):black_SettingsLookSpeed(axis))+direction;
        if(mouse)black_SettingsSetMouseSpeed(axis,MAX(1,MIN(200,value)));
        else black_SettingsSetLookSpeed(axis,MAX(1,MIN(200,value)));
        changed=CACurrentMediaTime();[self refreshValues];[self setNeedsDisplay:YES];return;
    }

    if (selected >= ROW_PAD_FIRST && selected <= ROW_PAD_LAST) {
        if (cycle) { pad_capture = YES; pad_disconnected = NO; black_PadCaptureBegin(); }
        else {
            pad_capture = NO; black_PadCaptureCancel();
            int value = black_SettingsPadBinding(selected - ROW_PAD_FIRST) + direction;
            black_SettingsSetPadBinding(selected - ROW_PAD_FIRST, MAX(-1,MIN(BLACK_PAD_CONTROLS-1,value)));
        }
        changed = CACurrentMediaTime(); [self refreshValues]; [self setNeedsDisplay:YES]; return;
    } else if (selected == ROW_DEVICE || selected == ROW_PROMPTS) {
        int prompt = selected == ROW_PROMPTS, count = prompt ? 4 : 3;
        int value = (prompt ? black_SettingsButtonPrompts() : black_SettingsInputDevice()) + direction;
        value = cycle ? (value + count) % count : MAX(0,MIN(count-1,value));
        if (prompt) black_SettingsSetButtonPrompts(value); else black_SettingsSetInputDevice(value);
    } else if (is_binding(selected)) {
        if (cycle) { binding_capture = YES; black_KeyReleaseAll(); }
        else { binding_capture = NO; binding_slot = direction > 0; }
        [self setNeedsDisplay:YES]; return;
    } else if (selected >= ROW_MOD_FIRST && selected <= ROW_MOD_LAST) {
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
        black_SettingsSetVibration(value>0);
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
    } else if (selected == ROW_INTROS || selected == ROW_MISSIONS || selected == ROW_ANY_FILM) {
        int current = selected == ROW_MISSIONS ? black_SettingsSkipMissionIntros() :
            selected == ROW_ANY_FILM ? black_SettingsSkipAnyFilm() : black_SettingsSkipIntros();
        int value = cycle ? !current : direction > 0;
        if (current == value) return;
        if (selected == ROW_MISSIONS) black_SettingsSetSkipMissionIntros(value);
        else if (selected == ROW_ANY_FILM) black_SettingsSetSkipAnyFilm(value);
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
    } else if (selected == ROW_BRIGHTNESS || selected == ROW_GAMMA) {
        int gamma = selected == ROW_GAMMA;
        int low = gamma ? BLACK_GAMMA_MIN : BLACK_BRIGHTNESS_MIN, high = gamma ? BLACK_GAMMA_MAX : BLACK_BRIGHTNESS_MAX;
        int current = gamma ? black_SettingsDisplayGamma() : black_SettingsDisplayBrightness();
        int value = current + (gamma ? 5 : 1) * direction;
        if (cycle && value > high) value = low;
        value = MAX(low, MIN(high, value));
        if (value == current) return;
        if (gamma) black_SettingsSetDisplayGamma(value); else black_SettingsSetDisplayBrightness(value);
    } else if (selected == ROW_TEXTURES) {
        int current = black_SettingsTextureReplacement(), value = cycle ? !current : direction > 0;
        if (value == current) return;
        if (value) { [self offerTexturePack]; return; }
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
/* Texture Replacement turns on only with a pack the map can use: the one in
 * replacement, or a folder the player chooses, checked and then linked. */
static NSAlert *pack_alert;
- (void)texturePackAlert:(NSString *)message info:(NSString *)info buttons:(NSArray<NSString *> *)buttons
                    then:(void (^)(NSInteger choice))then
{
    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = D(message);
    alert.informativeText = info;
    for (NSString *title in buttons) [alert addButtonWithTitle:D(title)];
    pack_alert = alert; /* owned until answered */
    [alert beginSheetModalForWindow:menu_window completionHandler:^(NSModalResponse response) {
        [pack_alert autorelease];
        pack_alert = nil;
        if (then) then(response - NSAlertFirstButtonReturn);
    }];
}
- (void)useTexturePack:(BOOL)linked
{
    black_SettingsSetTextureReplacement(1);
    if (!black_TexturePackReload() && linked)
        [self texturePackAlert:@"The new pack applies next launch" info:D(@"Another pack is already in use in this session.")
                       buttons:@[@"OK"] then:nil];
    changed = CACurrentMediaTime();
    [self refreshValues];
    [self setNeedsDisplay:YES];
}
- (void)offerTexturePack
{
    BlackTexturePackCheck check;
    int usable = black_TexturePackCheck(NULL, &check);
    if (!check.map) {
        [self texturePackAlert:@"The texture map is missing" info:D(@"This copy of BLACK has no black-hd-reimagined.map. Reinstall the app.")
                       buttons:@[@"OK"] then:nil];
        return;
    }
    if (usable && !check.missing) { [self useTexturePack:NO]; return; }
    if (usable) {
        NSString *info = [NSString stringWithFormat:D(@"Files for %@ of %@ textures are there; the rest keep the game's own. Choose the full pack's folder or use this one."),
            @(check.mapped), @(check.mapped + check.missing)];
        [self texturePackAlert:@"The HD pack is incomplete" info:info buttons:@[@"Choose Folder…", @"Use This Pack", @"Cancel"]
                          then:^(NSInteger choice) {
            if (choice == 0) [self chooseTexturePack];
            else if (choice == 1) [self useTexturePack:NO];
        }];
        return;
    }
    [self texturePackAlert:@"No HD texture pack found"
                      info:D(@"Texture Replacement needs your own copy of the BLACK HD Reimagined pack for PCSX2 (SLUS-21376). Choose its folder: BLACK checks it and links it without copying.")
                   buttons:@[@"Choose Folder…", @"Cancel"]
                      then:^(NSInteger choice) { if (choice == 0) [self chooseTexturePack]; }];
}
- (void)chooseTexturePack
{
    NSOpenPanel *panel = NSOpenPanel.openPanel;
    panel.canChooseDirectories = YES;
    panel.canChooseFiles = NO;
    panel.allowsMultipleSelection = NO;
    panel.message = D(@"Choose the folder of the BLACK HD Reimagined pack.");
    panel.prompt = D(@"Check");
    [panel beginSheetModalForWindow:menu_window completionHandler:^(NSModalResponse response) {
        if (response != NSModalResponseOK || !panel.URL) return;
        /* The sheet must be gone before the next one. */
        dispatch_async(dispatch_get_main_queue(), ^{ [self checkTexturePack:panel.URL.path]; });
    }];
}
/* Import: a profile file or a save folder on this Mac (a backup, another
 * copy of BLACK, a save copied off a hard disk). */
- (void)chooseImport
{
    NSOpenPanel *panel = NSOpenPanel.openPanel;
    panel.canChooseDirectories = YES; panel.canChooseFiles = YES; panel.allowsMultipleSelection = NO;
    panel.message = D(@"Choose a BLACK profile file, the save folder that holds it, or a zip of either.");
    fprintf(stderr, "[SAVES] import: Finder chooser opened\n");
    [NSApp activateIgnoringOtherApps:YES];
    panel.prompt = D(@"Import");
    [panel beginSheetModalForWindow:menu_window completionHandler:^(NSModalResponse response) {
        if (response != NSModalResponseOK || !panel.URL) return;
        NSString *path = [panel.URL.path copy];
        dispatch_async(dispatch_get_main_queue(), ^{ [self openImport:path]; [path release]; });
    }];
}
/* A file chosen in Finder: its profiles become the list to choose from;
 * with one profile it goes straight to the confirmation. */
- (void)openImport:(NSString *)path
{
    pick_count = 0;
    add_picks(path.fileSystemRepresentation, path.lastPathComponent.UTF8String);
    if (!pick_count) {
        fprintf(stderr, "[SAVES] import: no BLACK profile in %s\n", path.fileSystemRepresentation);
        set_note(@"THIS HAS NO BLACK PROFILE: CHOOSE A PROFILE FILE, ITS SAVE FOLDER OR A ZIP."); [self setNeedsDisplay:YES]; return;
    }
    fprintf(stderr, "[SAVES] import file chosen: %s\n", path.fileSystemRepresentation);
    if (pick_count == 1) { [self choosePick:0]; return; }
    black_menu_pick_rows = pick_count; page = PAGE_IMPORT_PICK; [self selectRow:ROW_PICK_FIRST];
}
- (void)choosePick:(int)at
{
    if (at < 0 || at >= pick_count) return;
    uint8_t source[BLACK_PROFILE_SIZE];
    memcpy(confirm_edited, profile_data, sizeof confirm_edited);
    if (!black_SaveReadImport(picks[at].path, source) || !black_profile_copy(confirm_edited, slot_open, source, picks[at].slot)) {
        set_note(@"THIS HAS NO BLACK PROFILE: CHOOSE A PROFILE FILE, ITS SAVE FOLDER OR A ZIP."); [self setNeedsDisplay:YES]; return;
    }
    const BlackProfileSlot *target = &profile_slots[slot_open];
    [self confirm:[NSString stringWithFormat:T(@"COPY %@ INTO SLOT %d?"), profile_label(&picks[at].info), slot_open + 1]
        detail:[NSString stringWithFormat:@"%@ %@", target->active ? [NSString stringWithFormat:T(@"IT REPLACES %@."), profile_label(target)] :
            T(@"THE SLOT IS EMPTY."), T(@"THE FILE AS IT IS NOW IS KEPT IN THE BACKUPS.")] reason:BLACK_SAVE_BEFORE_IMPORT];
}
/* The confirmation page: what the change does, Yes / No; No and Back return. */
- (void)confirm:(NSString *)question detail:(NSString *)detail reason:(int)reason
{
    [confirm_question release]; confirm_question = [question copy];
    [confirm_detail release]; confirm_detail = [detail copy];
    confirm_reason = reason; save_outcome = 0; confirm_kind = 0;
    black_menu_confirm_page = page; black_menu_confirm_row = selected;
    page = PAGE_CONFIRM; [self selectRow:ROW_CONFIRM_NO];
}
- (void)checkTexturePack:(NSString *)folder
{
    BlackTexturePackCheck check;
    int usable = black_TexturePackCheck(folder.fileSystemRepresentation, &check);
    void (^again)(NSInteger) = ^(NSInteger choice) { if (choice == 0) [self chooseTexturePack]; };
    if (!check.files) {
        [self texturePackAlert:@"No textures in this folder" info:D(@"It has no DDS or PNG files, in it or in its subfolders.")
                       buttons:@[@"Choose Another…", @"Cancel"] then:again];
        return;
    }
    if (!usable) {
        NSString *info = [NSString stringWithFormat:D(@"None of its %@ pictures is named in the texture map, so none would replace a texture. This needs the BLACK HD Reimagined pack."),
            @(check.files)];
        [self texturePackAlert:@"This is not the BLACK HD Reimagined pack" info:info
                       buttons:@[@"Choose Another…", @"Cancel"] then:again];
        return;
    }
    void (^link)(void) = ^{
        if (black_TexturePackLink(folder.fileSystemRepresentation)) [self useTexturePack:YES];
        else [self texturePackAlert:@"Could not link the folder" info:D(@"Check that BLACK may write to Application Support/BLACK/Textures.")
                            buttons:@[@"OK"] then:nil];
    };
    if (check.missing) {
        NSString *info = [NSString stringWithFormat:D(@"Files for %@ of %@ textures are there; the rest keep the game's own."),
            @(check.mapped), @(check.mapped + check.missing)];
        [self texturePackAlert:@"The HD pack is incomplete" info:info buttons:@[@"Use This Pack", @"Choose Another…", @"Cancel"]
                          then:^(NSInteger choice) { if (choice == 0) link(); else if (choice == 1) [self chooseTexturePack]; }];
        return;
    }
    link();
}
- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    if (save_waiting && black_SaveState() != BLACK_SAVE_PENDING) {
        /* The game's thread answered the edit request. */
        save_waiting = 0; save_outcome = black_SaveState();
        int done = save_outcome == BLACK_SAVE_DONE;
        set_note(done ? @"DONE. THE GAME'S PROFILE LIST SHOWS IT NOW." :
            save_outcome == BLACK_SAVE_NOT_MENU ? @"NOT CHANGED: OPEN THE GAME'S MAIN MENU FIRST." :
            save_outcome == BLACK_SAVE_GAME_CHANGED ? @"NOT CHANGED: THE GAME HAS UNSAVED PROGRESS OR THE FILE CHANGED. SAVE IN THE GAME, THEN TRY AGAIN." :
            @"NOT CHANGED: THE PROFILE COULD NOT BE WRITTEN.");
        /* Done: back to the slots (or the backups) with the result shown. */
        dispatch_async(dispatch_get_main_queue(), ^{
            if (!done) { [self selectRow:selected]; return; }
            int to = black_menu_confirm_page == PAGE_BACKUP ? PAGE_BACKUPS : PAGE_SAVES;
            [self openPage:to row:to == PAGE_SAVES ? ROW_SAVE_FIRST + slot_open : -1];
        });
    }
    NSSize size = self.bounds.size;
    /* The game shows through the margin and the rail band, as in its own
     * menus; the menu's column is dense enough that the game's options under
     * it do not read through (fill replaces, so each area gets its own alpha). */
    fill(self.bounds, BLACK_UI_BACKDROP, .6);
    {
        BlackPageGeometry g0 = page_geometry(size);
        CGFloat column = g0.ox + (g0.rail_x + g0.band) * g0.scale;
        fill(NSMakeRect(column, 0, size.width - column, size.height), BLACK_UI_BACKDROP, .94);
    }
    text_scale = 1.8;
    [NSGraphicsContext saveGraphicsState];
    CGFloat scale = MIN(size.width / 960.0, size.height / 600.0), ox = (size.width - 960 * scale) / 2, oy = (size.height - 600 * scale) / 2;
    NSAffineTransform *transform = [NSAffineTransform transform];
    [transform translateXBy:ox yBy:oy];
    [transform scaleBy:scale]; [transform concat];
    /* The game's page: rails on the left, heading in its large font, rows to the right. */
    BOOL still = NSWorkspace.sharedWorkspace.accessibilityDisplayShouldReduceMotion;
    BlackPageGeometry g = page_geometry(size);
    black_ui_rails_band(g.rail_x, g.band, -oy / scale, 600 + oy / scale, still ? -1 : fmod(CACurrentMediaTime() / 7, 1) * 1.3 - .15, .9);
    /* A slot's picture in the band, its top level with the first row, as the
     * game's own menus put a picture beside their list. */
    {
        int pictured = pictured_slot(page, selected);
        if (pictured >= 0) {
            NSImage *image = slot_pictures[pictured];
            /* Inside the band's rails: clear of the double rail on its right. */
            CGFloat w = g.band - 44, h = w * image.size.height / MAX(image.size.width, 1);
            NSRect frame = NSMakeRect(g.rail_x + 12, g.dy + ROWS_TOP * g.ky, w, h);
            fill(NSInsetRect(frame, -1, -1), HIGHLIGHT, .8);
            [image drawInRect:frame fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1 respectFlipped:YES hints:nil];
        }
    }
    /* The rows from here on, shifted right of the wider band. */
    NSAffineTransform *shift = [NSAffineTransform transform];
    [shift translateXBy:g.dx yBy:g.dy]; [shift scaleXBy:g.k yBy:g.ky]; [shift concat];
    text_aspect = g.k / g.ky;
    CGFloat rail = (g.rail_x + g.band - g.dx) / g.k, edge = (960 + ox / scale - g.dx) / g.k;
    /* The game's own nesting: the menu's name large, the way here under it
     * (SETTINGS > SAVES > PROFILE BACKUPS); Back climbs one level. */
    text(T(@"RECOMP SETTINGS"), 96, 18, 26, TEXT, 1);
    int tab = black_menu_tab(page);
    {
        NSMutableArray *trail = [NSMutableArray array];
        for (int p = page, guard = 0; p >= 0 && p != PAGE_ROOT && guard < 8; ++guard) {
            NSString *name = p == PAGE_BACKUP && backup_open < backup_count ? [NSString stringWithFormat:@"%@ %s", T(@"BACKUP"), backups[backup_open].when] :
                p == PAGE_SLOT ? [NSString stringWithFormat:@"%@ %d%@", T(@"SLOT"), slot_open + 1,
                profile_slots[slot_open].active ? [@": " stringByAppendingString:[@(profile_slots[slot_open].name) uppercaseString]] : @""] : T(page_names[p]);
            [trail insertObject:name atIndex:0]; p = black_menu_parent(p, NULL);
        }
        CGFloat x = 96;
        if (trail.count) {
            NSString *last = trail.lastObject; [trail removeLastObject];
            for (NSString *step in trail) { NSString *item = [step stringByAppendingString:@"  ›  "]; text(item, x, 72, 13, DIM, 0); x += text_width(item, 13, 0); }
            fitted_text(last, x, 72, 13, 600 - x, HEADING, 0);
            x += MIN(text_width(last, 13, 0), 600 - x);
        }
        trail_end = x;
        /* What the page is about, on the right: the game's revision, the device in use. */
        NSString *info = nil; unsigned info_color = DIM;
        if (page == PAGE_MODS) {
            int supported = black_ModsSupported();
            info = [NSString stringWithFormat:T(@"GAME: %s%@"), black_ModsRevision(), supported > 0 ? T(@" XBE") : supported < 0 ? @"" : T(@" - MODS OFF")];
            if (!supported) info_color = WARN;
        } else if (tab == 1) {
            BlackControllerMonitor monitor = black_ControllerMonitor();
            info = black_InputKeyboardActive() ? T(@"KEYBOARD AND MOUSE") : monitor.connected ? @(monitor.name) : T(@"NO CONTROLLER");
        }
        NSString *count = [NSString stringWithFormat:@"%02d / %02d", black_menu_index(page, selected) + 1, black_menu_count(page)];
        CGFloat count_x = 868 - text_width(count, 10, 0);
        text(count, count_x, 74, 10, DIM, 0);
        /* The page's status ends before the counter. */
        /* ...and after the way here; with too little room it is left out. */
        if (info) { CGFloat room = MIN(260, count_x - 24 - (trail_end + 24)), w = MIN(text_width(info, 10, 0), room);
            if (room >= 60) fitted_text(info, count_x - 24 - w, 74, 10, room, info_color, 0); }
        line(84, 112, 792, HIGHLIGHT, 1);
    }
    int start=visible_start(page,selected),end=MIN(start+MENU_TAB_MAX_ROWS,black_menu_count(page));
    CGFloat width=page==PAGE_DIAGNOSTICS?600:792,left=VALUE_LEFT;
    for(int position=start;position<end;++position){
        int index=black_menu_row(page,position);
        CGFloat y=row_y(page,index,selected);
        if(grouped(page)&&(position==start||group_of(index)!=group_of(black_menu_row(page,position-1)))){
            text(T(group_name(group_of(index))), 96, y - 17, 10, DIM, 0);
            line(96, y - 3, width - 24, HIGHLIGHT, 1);
        }
        BOOL focus = selected == index;
        /* The live sticks sit right of the controller rows: the line stops at their divider. */
        if (focus) black_ui_selector(NSMakeRect(84, y, width, 38), rail, page == PAGE_DIAGNOSTICS ? 700 : edge);
        if(black_menu_save(index)){
            NSString *name=@"",*value=@"";BOOL warn=NO,more=NO;
            if(index>=ROW_SAVE_FIRST&&index<=ROW_SAVE_LAST){int at=index-ROW_SAVE_FIRST;
                name=[NSString stringWithFormat:@"%@ %d",T(@"SLOT"),at+1];
                value=profile_ok?slot_text(&profile_slots[at]):T(@"NO PROFILE YET");more=YES;
                if(profile_ok&&slot_in_use(at))value=[value stringByAppendingFormat:@" · %@",T(@"IN GAME")];
                warn=profile_slots[at].damaged;
            }else if(index==ROW_SLOT_MISSIONS){name=T(@"MISSIONS");value=[NSString stringWithFormat:@"%d %@ 32",profile_slots[slot_open].completed,T(@"OF")];more=YES;
            }else if(index==ROW_SLOT_IMPORT){name=T(profile_slots[slot_open].active?@"REPLACE FROM A FILE":@"IMPORT HERE");more=YES;
            }else if(index==ROW_SLOT_EXPORT){name=T(@"EXPORT TO A FILE");
            }else if(index==ROW_SLOT_DELETE){name=T(profile_slots[slot_open].damaged?@"CLEAR THE SLOT":@"DELETE");
            }else if(index==ROW_SLOT_CHECKPOINT){name=T(@"CHECKPOINT");value=checkpoint_text(&slot_checkpoint);
            }else if(index==ROW_SLOT_CHECKPOINT_DELETE){name=T(@"DELETE THE CHECKPOINT");warn=YES;
            }else if(index==ROW_CHECKPOINTS){name=T(@"SAVE CHECKPOINTS");value=black_SettingsSaveCheckpoints()?@"ON":@"OFF";
            }else if(index>=ROW_MISSION_FIRST&&index<=ROW_MISSION_LAST){int m=index-ROW_MISSION_FIRST;
                const uint8_t *rec=profile_data+slot_open*BLACK_PROFILE_SLOT+BLACK_PROFILE_RECORDS+m*BLACK_PROFILE_RECORD;
                uint32_t result=rec[0]|rec[1]<<8|rec[2]<<16|(uint32_t)rec[3]<<24;
                static NSString *const difficulty[4]={@"EASY",@"NORMAL",@"HARD",@"BLACK OPS"};
                name=[NSString stringWithFormat:@"%@ · %@ %d",T(difficulty[m/8]),T(@"MISSION"),m%8+1];
                value=result<=2?T(result_labels[result]):rec[BLACK_PROFILE_AVAILABLE]==1?T(@"OPEN"):T(@"LOCKED");
            }else if(index>=ROW_BACKUP_FIRST&&index<=ROW_BACKUP_LAST){int at=index-ROW_BACKUP_FIRST;
                if(at<backup_count){NSMutableArray *names=[NSMutableArray array];
                    for(int i=0;i<BLACK_PROFILE_SLOTS;++i)if(backups[at].slots[i].active)[names addObject:[@(backups[at].slots[i].name) uppercaseString]];
                    name=[NSString stringWithFormat:@"%s · %@",backups[at].when,names.count?[names componentsJoinedByString:@", "]:T(@"EMPTY")];
                    value=backups[at].reason>=0?T(reason_labels[backups[at].reason]):@"";more=YES;
                }else name=T(@"NO BACKUPS YET");
            }else if(index>=ROW_PICK_FIRST&&index<=ROW_PICK_LAST){int at=index-ROW_PICK_FIRST;
                name=[NSString stringWithFormat:@"%@ · %d %@ 32",[@(picks[at].info.name) uppercaseString],picks[at].info.completed,T(@"OF")];
                value=@(picks[at].file);
            }else if(index==ROW_BACKUP_RESTORE){name=T(@"PUT THIS BACKUP BACK");
            }else if(index==ROW_BACKUP_DELETE){name=T(@"DELETE THIS BACKUP");
            }else if(index==ROW_BACKUPS_CLEAR){name=T(@"DELETE ALL BACKUPS");value=[NSString stringWithFormat:@"%d",backup_count];
            }else if(index==ROW_PICK_FOLDER){name=T(@"OPEN THE IMPORT FOLDER");
            }else if(index==ROW_PICK_FINDER){name=T(@"CHOOSE IN FINDER");
            }else if(index==ROW_CONFIRM_YES){name=T(save_waiting?@"APPLYING…":@"YES");warn=YES;
            }else if(index==ROW_CONFIRM_NO){name=T(@"NO");}
            if(row_disabled(index)||(index==ROW_CHECKPOINTS&&checkpoints_locked())){
                fitted_text(name,96,y+13,13,700,BLACK_UI_DISABLED,0);
                fitted_text(T(index==ROW_CHECKPOINTS?@"AUTOSAVE IS OFF":@"NOT WHILE IN GAME"),500,y+13,12,330,BLACK_UI_DISABLED,0);
                continue;
            }
            CGFloat value_x=index>=ROW_PICK_FIRST&&index<=ROW_PICK_LAST?560:500;
            fitted_text(name,96,y+13,13,value_x-110,focus?(warn&&index!=ROW_CONFIRM_YES?WARN:TEXT):warn?WARN:DIM,0);
            if(index==ROW_CHECKPOINTS){
                text(@"<",509,y+13,13,focus?TEXT:DIM,0);
                text(@">",846,y+13,13,focus?TEXT:DIM,0);
                fitted_text(value,536,y+13,12,274,focus?TEXT:DIM,0);
                continue;
            }
            if(value.length)fitted_text(value,value_x,y+13,12,more?330:370,focus?TEXT:DIM,0);
            if(more)text(@">",850,y+13,13,focus?TEXT:DIM,0);
            continue;
        }
        if(index==ROW_DEFAULTS||index==ROW_RESET_ALL){
            fitted_text(T(index==ROW_RESET_ALL?@"RESET ALL SETTINGS":@"RECOMMENDED FOR THIS PAGE"),96,y+13,13,700,index==ROW_RESET_ALL&&focus?WARN:focus?TEXT:DIM,0);
            continue;
        }
        fitted_text(T([row_labels()[index] uppercaseString]), 96, y + 13, 13,
            is_binding(index)||black_menu_pair(index) ? 490 : index==ROW_DEVICE||index==ROW_PROMPTS||index==ROW_EDIT_DEVICE ? 390 : left-108, focus ? TEXT : DIM, 0);
        if(black_menu_link(index)||index==ROW_QUIT){
            text(@">",850,y+13,13,focus?TEXT:DIM,0);continue;
        }
        if(index==ROW_DEFAULTS||index==ROW_RESET_ALL)continue;
        if(index==ROW_CLOSE_MENU)continue;
        if(black_menu_pair(index)){
            int mouse=index<=ROW_MOUSE_AIM_PAIR,axis=((index-ROW_MOUSE_LOOK_PAIR)%2)*2;
            for(int slot=0;slot<2;++slot){CGFloat x=600+slot*138;BOOL active=focus&&axis_slot==slot;
                fill(NSMakeRect(x,y+6,132,26),BUTTON,.9);
                if(active)line(x+6,y+29,120,axis_edit?TEXT:HEADING,1);
                int value=mouse?black_SettingsMouseSpeed(axis+slot):black_SettingsLookSpeed(axis+slot);
                text(@"<",x+6,y+13,11,active?TEXT:DIM,0);text(@">",x+117,y+13,11,active?TEXT:DIM,0);
                fitted_text([NSString stringWithFormat:@"%c %.2fX",slot?'Y':'X',value/50.],x+24,y+13,11,84,active?TEXT:DIM,0);
            }continue;
        }
        if (is_binding(index)) {
            for (int slot = 0; slot < 2; ++slot) {
                CGFloat x = 600 + slot * 138;
                BOOL active = focus && slot == binding_slot;
                fill(NSMakeRect(x,y+6,132,26),BUTTON,.9);
                if (active) line(x+6,y+29,120,TEXT,.9);
                NSString *key = active && binding_capture ? @"PRESS A KEY" :
                    icon_string(black_SettingsBinding(index-ROW_BIND_FIRST,slot)<0?0xE1FF:0xE200+black_SettingsBinding(index-ROW_BIND_FIRST,slot));
                fitted_text(T(key.uppercaseString),x+8,y+13,11,116,active ? TEXT : DIM,0);
            }
            continue;
        }
        if (index >= ROW_PAD_FIRST && index <= ROW_PAD_LAST) {
            fill(NSMakeRect(600,y+6,276,26),BUTTON,.9);
            if (focus) line(606,y+29,264,TEXT,.9);
            NSString *value = focus && pad_capture ? @"PRESS A BUTTON" :
                icon_string(black_SettingsPadBinding(index-ROW_PAD_FIRST)<0?0xE1FF:0xE100+(black_InputPromptFamily()==BLACK_PROMPTS_PLAYSTATION?16:0)+black_SettingsPadBinding(index-ROW_PAD_FIRST));
            BOOL symbol = value.length == 1 && [@"×○□△" rangeOfString:value].location != NSNotFound;
            fitted_text(T(value),608,y+(symbol?8:13),symbol?20:12,260,focus ? TEXT : DIM,0);
            continue;
        }
        NSString *value = index==ROW_EDIT_DEVICE?(page==PAGE_KEYS?@"KEYBOARD AND MOUSE":@"GAMEPAD"):index == ROW_DEVICE ? (@[@"AUTO",@"GAMEPAD",@"KEYBOARD AND MOUSE"])[black_SettingsInputDevice()] :
            index == ROW_PROMPTS ? (@[@"AUTO",@"XBOX",@"PLAYSTATION",@"KEYBOARD"])[black_SettingsButtonPrompts()] :
            index >= ROW_MOD_FIRST && index <= ROW_MOD_LAST ? mod_value(index - ROW_MOD_FIRST) :
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
            index == ROW_RUMBLE ? (black_SettingsVibration()?[NSString stringWithFormat:@"%d%%",black_SettingsRumbleIntensity()]:@"OFF") :
            index == ROW_LEFT_DZ ? dz_name(0) : index == ROW_RIGHT_DZ ? dz_name(1) :
            index == ROW_TRIGGER_DZ ? [NSString stringWithFormat:@"%d%%", black_SettingsTriggerDeadzone()] :
            index == ROW_VIBRATION ? (black_SettingsVibration() ? @"ON" : @"OFF") :
            index == ROW_HID ? (black_SettingsDualSenseHid() ? @"ON" : @"OFF") :
            index == ROW_MEASURE ? (black_ControllerMonitor().calibration == BLACK_CAL_MEASURING ? @"WAIT" : @"START") :
            index == ROW_RESET ? @"RESET" :
            index == ROW_FOV ? [NSString stringWithFormat:@"%d", black_SettingsFov()] :
            index == ROW_UPSCALE ? (black_SettingsUpscaler() ? @"METALFX" : @"OFF") :
            index == ROW_FPS ? (black_SettingsFpsCounter() ? @"ON" : @"OFF") :
            index == ROW_BRIGHTNESS ? brightness_name() : index == ROW_GAMMA ? gamma_name() :
            index == ROW_TEXTURES ? (black_SettingsTextureReplacement() ? @"ON" : @"OFF") :
            index == ROW_TEXTURE_SIZE ? [NSString stringWithFormat:@"%d", black_SettingsTextureMaxSize()] :
            index == ROW_GAME_LANG ? game_language_names[black_SettingsGameLanguage()] :
            index == ROW_MENU_LANG ? menu_language_names[black_SettingsMenuLanguage()] :
            index == ROW_AA ? aa_name(black_SettingsAntiAliasing()) :
            index == ROW_AF ? af_name(black_SettingsAniso()) :
            index == ROW_INTROS ? (black_SettingsSkipIntros() ? @"ON" : @"OFF") :
            index == ROW_MISSIONS ? (black_SettingsSkipMissionIntros() ? @"ON" : @"OFF") :
            index == ROW_ANY_FILM ? (black_SettingsSkipAnyFilm() ? @"ON" : @"OFF") :
            [NSString stringWithFormat:@"%dX", black_SettingsRenderScale()];
        CGFloat value_left = index == ROW_DEVICE || index == ROW_PROMPTS || index==ROW_EDIT_DEVICE ? 500 : left;
        CGFloat value_width = index == ROW_DEVICE || index == ROW_PROMPTS || index==ROW_EDIT_DEVICE ? 376 : VALUE_WIDTH;
        if (index != ROW_MEASURE && index != ROW_RESET) {
            text(@"<", value_left + 9, y + 13, 13, focus ? TEXT : DIM, 0);
            text(@">", value_left + value_width - 24, y + 13, 13, focus ? TEXT : DIM, 0);
        }
        fitted_text(T(value), value_left + 36, y + 13, 12, value_width - 72, focus ? TEXT : DIM, 0);
    }
    NSString *help, *detail;
    if(selected==ROW_DEFAULTS){
        help=@"PUTS THIS PAGE'S SETTINGS BACK TO THE RECOMMENDED ONES. YOU CONFIRM FIRST.";
        detail=@"ONE SETTING ALONE: SELECT IT AND PRESS △ / Y OR BACKSPACE.";
    }else if(selected==ROW_RESET_ALL){
        help=@"EVERY SETTING BACK TO THE RECOMMENDED ONE: GRAPHICS, CONTROLS, BINDINGS AND MODS.";
        detail=@"THE GAME'S LANGUAGE AND YOUR SAVES STAY. YOU CONFIRM FIRST.";
    }else if(selected==ROW_CLOSE_MENU){
        help=@"CLOSE THE SETTINGS AND GO BACK TO THE GAME.";
        detail=@"BACK, F1 OR HOME DO THE SAME.";
    }else if(selected==ROW_QUIT){
        help=@"QUIT TO THE GAME'S MAIN MENU OR QUIT BLACK.";
        detail=@"ENTER / A OPENS. HOLDING HOME, GUIDE OR PS OPENS IT TOO.";
    }else if(black_menu_save(selected)||selected==ROW_NAV_BACKUPS){
        BOOL allowed=black_SaveEditAllowed();
        detail=allowed?@"CHANGES APPLY AT ONCE AND KEEP THE FILE BEFORE THEM IN THE BACKUPS.":@"CHANGES APPLY ONLY ON THE GAME'S MAIN MENU, NOT IN A MISSION.";
        if(page==PAGE_CONFIRM){help=confirm_question;detail=save_note?:confirm_detail;}
        else if(save_note)help=save_note;
        else if(selected>=ROW_SAVE_FIRST&&selected<=ROW_SAVE_LAST){int at=selected-ROW_SAVE_FIRST;
            help=!profile_ok?@"NO PROFILE YET: THE GAME CREATES ONE WHEN IT FIRST SAVES.":slot_in_use(at)?@"THE PROFILE THE GAME PLAYS AND AUTOSAVES TO. ITS MISSIONS AND EXPORT ARE OPEN.":
                profile_slots[at].active?@"MISSIONS, REPLACE, EXPORT OR DELETE THIS PROFILE.":profile_slots[at].damaged?@"THE GAME WROTE THIS SLOT OFF AS DAMAGED: CLEAR IT OR IMPORT INTO IT.":@"AN EMPTY SLOT: IMPORT A PROFILE INTO IT, OR LET THE GAME MAKE ONE.";}
        else if(selected==ROW_NAV_BACKUPS)help=@"EVERY CHANGE KEEPS THE FILE BEFORE IT; ONE IS MADE AT LAUNCH TOO. PUT ANY BACK.";
        else if(selected==ROW_SLOT_MISSIONS)help=@"EACH DIFFICULTY'S MISSIONS: OPEN OR LOCKED, AND THEIR RESULTS.";
        else if(selected==ROW_SLOT_IMPORT||selected==ROW_SLOT_DELETE)help=slot_in_use(slot_open)?@"THE GAME PLAYS THIS PROFILE AND AUTOSAVES TO IT. LOAD ANOTHER PROFILE IN THE GAME FIRST.":
            selected==ROW_SLOT_DELETE?(profile_slots[slot_open].damaged?@"MAKES THE SLOT EMPTY AGAIN.":@"EMPTIES THIS SLOT. YOU CONFIRM FIRST."):
            @"COPY A PROFILE FROM A ZIP, SAVE FOLDER OR PROFILE FILE INTO THIS SLOT.";
        else if(selected==ROW_SLOT_EXPORT)help=@"SAVES THIS PROFILE AS A FILE OF ITS OWN, TO KEEP OR TO IMPORT ELSEWHERE.";
        else if(selected==ROW_SLOT_CHECKPOINT){help=@"WHERE CONTINUE MISSION RESUMES THIS PROFILE: DIFFICULTY, MISSION AND ITS CHECKPOINT.";detail=@"";}
        else if(selected==ROW_SLOT_CHECKPOINT_DELETE)help=@"REMOVES THIS PROFILE'S CHECKPOINT FILES. THE PROFILE AND ITS MISSIONS STAY.";
        else if(selected==ROW_CHECKPOINTS){help=@"WRITES THE GAME'S OWN CHECKPOINT BESIDE THE SAVES AS YOU PLAY. CONTINUE MISSION ON THE TITLE MENU RESUMES IT.";
            detail=checkpoints_locked()?@"NEEDS THE GAME'S AUTOSAVE: OPTIONS > PROFILE > AUTOSAVE ON. THE SAME ROW IS THERE TOO.":
                @"ON BY DEFAULT, WITH THE GAME'S AUTOSAVE. F9 OR BACK + Y RESTARTS FROM IT IN A MISSION, AFTER A QUESTION.";}
        else if(selected>=ROW_MISSION_FIRST&&selected<=ROW_MISSION_LAST){
            /* The mission's bests and last run, from the slot's record (mission_stats.h). */
            const BlackMissionBest *b=slot_info_ok[slot_open]?&slot_infos[slot_open].missions[selected-ROW_MISSION_FIRST]:NULL;
            if(b&&b->completions){
                help=[NSString stringWithFormat:@"%@ %@ · %@ %d · %@ %@ · %@ %d",T(@"BEST TIME"),clock_text(b->best_seconds),
                    T(@"FEWEST DEATHS"),b->fewest_deaths,T(@"BEST ACCURACY"),accuracy_text(b->best_accuracy),T(@"COMPLETIONS"),b->completions];
                detail=[NSString stringWithFormat:@"%@ %@ · %@ %d · %@ %d · %@ %@",T(@"LAST RUN"),clock_text(b->last_seconds),
                    T(@"DEATHS"),b->last_deaths,T(@"CHECKPOINTS"),b->last_checkpoints,T(@"ACCURACY"),accuracy_text(b->last_accuracy)];
            }else{help=@"AS THE PROFILE HAS IT. THE GAME CHANGES IT AS YOU PLAY.";detail=@"BEST RESULTS APPEAR ONCE THE MISSION IS COMPLETED WITH THIS VERSION.";}
        }
        else if(selected>=ROW_PICK_FIRST&&selected<=ROW_PICK_LAST)help=[NSString stringWithFormat:T(@"COPY THIS PROFILE INTO SLOT %d. THE FILE ITSELF IS NOT CHANGED."),slot_open+1];
        else if(selected==ROW_PICK_FOLDER)help=pick_count?@"PUT A ZIP, SAVE FOLDER OR PROFILE FILE THERE; ITS PROFILES ARE LISTED ABOVE.":@"NOTHING TO IMPORT YET: PUT A ZIP, SAVE FOLDER OR PROFILE FILE IN THIS FOLDER.";
        else if(selected==ROW_PICK_FINDER)help=@"CHOOSE A ZIP, SAVE FOLDER OR PROFILE FILE ANYWHERE ON THIS MAC.";
        else if(selected>=ROW_BACKUP_FIRST&&selected<=ROW_BACKUP_LAST)help=selected-ROW_BACKUP_FIRST<backup_count?@"PUT THIS BACKUP BACK OR DELETE IT.":@"BACKUPS APPEAR AT THE NEXT LAUNCH OR CHANGE.";
        else if(selected==ROW_BACKUP_RESTORE)help=@"PUTS ALL THREE SLOTS BACK AS THEY WERE THEN. YOU CONFIRM FIRST.";
        else if(selected==ROW_BACKUP_DELETE)help=@"MOVES THIS BACKUP TO THE TRASH. YOUR PROFILE IS NOT CHANGED.";
        else if(selected==ROW_BACKUPS_CLEAR)help=@"MOVES EVERY BACKUP TO THE TRASH. YOUR PROFILE IS NOT CHANGED.";
        else help=@"";
        if(page==PAGE_SLOT&&slot_in_use(slot_open)&&!save_note)
            detail=@"THE GAME PLAYS THIS PROFILE: REPLACE AND DELETE ARE OFF. LOAD ANOTHER PROFILE IN THE GAME TO USE THEM.";
    }else if(selected>=ROW_NAV_PORT&&selected<=ROW_NAV_SAVES){
        help=selected==ROW_NAV_PORT?@"PICTURE, LANGUAGE AND HD TEXTURES.":
            selected==ROW_NAV_CONTROLS?@"DEVICE, BUTTON PROMPTS, BINDINGS, MOUSE AND GAMEPAD.":
            selected==ROW_NAV_MODS?@"FILM SKIPPING, PLAYER HELPS AND UNLOCKS.":@"PROFILE SLOTS: DELETE, IMPORT, BACKUPS.";
        detail=@"ENTER / A OPENS. BACK RETURNS HERE; BACK HERE CLOSES THE SETTINGS.";
    }else if(black_menu_link(selected)){
        help=selected==ROW_NAV_BINDINGS?@"EDIT KEYBOARD OR GAMEPAD BINDINGS IN ONE PLACE.":
            selected==ROW_NAV_MOUSE?@"MOUSE LOOK, AIMING AND WALK SPEED.":
            selected==ROW_NAV_PAD?@"GAMEPAD LOOK, AIMING AND VIBRATION.":
            selected==ROW_NAV_ADVANCED?@"DEADZONES, AIM ASSIST AND DRIVER OPTIONS.":@"CHECK STICK INPUT AND MEASURE DRIFT.";
        detail=@"ENTER / A OPENS. BACK RETURNS HERE WITHOUT CHANGING YOUR DEVICE.";
    }else if(selected==ROW_EDIT_DEVICE){
        help=@"CHOOSE WHICH BINDINGS TO EDIT. THIS DOES NOT SWITCH GAMEPLAY INPUT.";
        detail=@"KEYBOARD HAS TWO KEY SLOTS; GAMEPAD USES ITS PHYSICAL BUTTONS.";
    }else if(black_menu_pair(selected)){
        help=axis_edit?@"LEFT / RIGHT ADJUST THE SELECTED AXIS. ENTER / BACK FINISHES.":@"LEFT / RIGHT CHOOSE X OR Y. ENTER / A EDITS THAT AXIS.";
        detail=@"LIVE. X / Y, MOUSE / GAMEPAD AND AIMING KEEP SEPARATE VALUES.";
    }else if (selected == ROW_DEVICE) {
        help = @"AUTO FOLLOWS DELIBERATE INPUT. MOUSE MOTION DOES NOT STEAL THE PAD.";
        detail = black_SettingsInputDevice() == BLACK_INPUT_GAMEPAD && !black_ControllerMonitor().connected ?
            @"NO GAMEPAD CONNECTED. KEYBOARD STILL OPENS SETTINGS; SELECT AUTO OR KEYBOARD." :
            [NSString stringWithFormat:T(@"ACTIVE: %@. SETTINGS STAY ACCESSIBLE WITH EITHER DEVICE."),
                T(black_InputKeyboardActive() ? @"KEYBOARD" : @"GAMEPAD")];
    } else if (selected == ROW_PROMPTS) {
        help = @"AUTO FOLLOWS THE ACTIVE DEVICE: XBOX, PLAYSTATION OR KEYBOARD.";
        detail = @"OVERRIDE THE SYMBOLS WITHOUT CHANGING WHICH DEVICE CONTROLS THE GAME.";
    } else if (selected >= ROW_PAD_FIRST && selected <= ROW_PAD_LAST) {
        help = pad_capture ? @"RELEASE ALL BUTTONS, THEN PRESS ONE. ESC CANCELS; DELETE CLEARS." :
            @"ENTER STARTS CAPTURE. LEFT / RIGHT CHOOSE A BUTTON. DELETE CLEARS.";
        detail = pad_disconnected ? @"CONTROLLER DISCONNECTED. BINDING KEPT. CONNECT AND TRY AGAIN." :
            @"DEFAULT GAME SCHEME. MENUS KEEP THEIR PHYSICAL BUTTONS; SAVED IMMEDIATELY.";
    } else if (selected >= ROW_MOD_FIRST && selected <= ROW_MOD_LAST) {
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
        help = @"YOUR OWN BLACK HD REIMAGINED PACK. TURNING IT ON CHECKS THE PACK OR ASKS FOR ITS FOLDER.";
        detail = black_TexturePackState() < 0 ? @"NO PACK WAS FOUND, SO THE GAME'S TEXTURES STAY. TURN IT OFF AND ON TO CHOOSE A FOLDER." :
            @"LIVE. PORTINGPETE'S MAP FINDS EACH FILE; MISSING ONES KEEP THE GAME'S TEXTURE.";
    } else if (selected == ROW_TEXTURE_SIZE) {
        help = @"LARGEST SIDE OF AN HD TEXTURE. LARGER FILES USE A SMALLER LEVEL.";
        detail = @"LIVE. 1024 USES ABOUT A QUARTER OF THE MEMORY OF 2048; MOST OF THE PACK IS 2048.";
    } else if (selected == ROW_BRIGHTNESS) {
        help = @"BLACK LEVEL OVER THE GAME'S OWN BRIGHTNESS. WHITE STAYS WHITE.";
        detail = @"LIVE. 0 KEEPS THE GAME'S PICTURE. ABOVE 0 LIFTS SHADOWS, BELOW DEEPENS THEM.";
    } else if (selected == ROW_GAMMA) {
        help = @"MIDTONES OVER THE GAME'S OWN BRIGHTNESS. BLACK AND WHITE STAY.";
        detail = @"LIVE. 1.00 KEEPS THE GAME'S PICTURE. ABOVE IT BRIGHTER, BELOW DARKER.";
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
        help = selected == ROW_MISSIONS ? @"SKIP THE BRIEFING FILM BEFORE EACH MISSION, WITHOUT A PRESS." :
            selected == ROW_ANY_FILM ? @"A, START OR ENTER SKIPS ANY FILM, ALSO ONE NOT YET UNLOCKED." :
            @"SKIP EA, CRITERION AND STARTUP FILMS, WITHOUT A PRESS.";
        detail = selected == ROW_MISSIONS ? @"MISSION PROGRESS AND ENDING ARE UNCHANGED." :
            selected == ROW_ANY_FILM ? @"A HINT SHOWS THE BUTTON. OFF: ONLY FILMS THE GAME ITSELF LETS YOU SKIP." :
            @"FILMS PLAY BRIEFLY BEFORE SKIPPING.";
    }
    /* The open slot's record under its rows: what the profile itself does
     * not say. Without one, what will add it. */
    if (page == PAGE_SLOT && profile_slots[slot_open].active) {
        CGFloat y = ROWS_TOP + black_menu_count(page) * ROW_STEP + 8;
        if (slot_info_ok[slot_open]) {
            const BlackSlotInfo *info = &slot_infos[slot_open];
            /* Two lines a row's half apart: the mission, then time and date. */
            NSString *names[2] = {T(@"LAST MISSION"), [NSString stringWithFormat:@"%@ · %@", T(@"TIME IN MISSIONS"), T(@"SAVED")]};
            NSString *values[2] = {info->level >= 0 ? mission_text((uint32_t)info->level, (uint32_t)info->difficulty) : @"—",
                [NSString stringWithFormat:@"%@ · %@", duration_text(info->mission_seconds), info->saved_at ? date_text(info->saved_at) : @"—"]};
            for (int i = 0; i < 2; ++i, y += 26) {
                text(names[i], 96, y, 10, DIM, 0);
                CGFloat w = text_width(values[i], 10, 0);
                text(values[i], 868 - w, y, 10, TEXT, 0);
            }
        } else fitted_text(T(@"THE NEXT SAVE OF THIS PROFILE ADDS ITS LAST MISSION, TIME AND A PICTURE."), 96, y, 10, 772, DIM, 0);
    }
    /* The description, a row's space under the list, its two lines apart. */
    line(84, 422, 792, HIGHLIGHT, 1);
    fitted_text(T(help), 96, 434, 11, 768, TEXT, 0);
    fitted_text(T(detail), 96, 462, 10, 768, DIM, 0);
    if (page == PAGE_DIAGNOSTICS) {
        BlackControllerMonitor monitor = black_ControllerMonitor();
        fill(NSMakeRect(704, 131, 1, 264), HEADING, .35);
        text(T(monitor.connected ? @"LIVE STICKS" : @"NO CONTROLLER"), 720, 137, 10, HEADING, 0);
        if (monitor.connected) fitted_text([NSString stringWithUTF8String:monitor.name], 720, 154, 9, 150, DIM, 0);
        for (int stick = 0; stick < 2; ++stick) {
            CGFloat cx = 750, cy = 209 + stick * 118;
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
        text([NSString stringWithFormat:@"LT %d%%  RT %d%%", monitor.mapped[4] * 100 / 255, monitor.mapped[5] * 100 / 255], 720, 394, 9, DIM, 0);
    }
    /* The game's prompts, as its own pages show them: select, confirm and back
     * in a column under the description, one row step apart and one step
     * below it, a size under the rows. The buttons sit centred in one column,
     * the words aligned after it; select is two triangles, or a cross where
     * left and right change the value. Default (Y / Backspace) stands on the
     * confirm line, in the values' column. */
    char accept[32], cancel[32];
    black_InputGlyphUTF8(black_InputButtonGlyph(0,1),accept,sizeof accept); black_InputGlyphUTF8(black_InputButtonGlyph(1,1),cancel,sizeof cancel);
    {
        BOOL adjust = (!black_menu_link(selected) && (!black_menu_save(selected) || selected == ROW_CHECKPOINTS) && selected != ROW_QUIT && selected != ROW_CLOSE_MENU &&
            selected != ROW_DEFAULTS && selected != ROW_MEASURE && selected != ROW_RESET);
        const CGFloat size = 12, wide = 1 / text_aspect;
        NSString *keys[2] = {@(accept), @(cancel)};
        CGFloat column = MAX(text_width(keys[0], size, 0), text_width(keys[1], size, 0));
        column = MAX(column, 26 * wide);
        CGFloat middle = 96 + column / 2, words_x = 96 + column + 14;
        /* The select mark, as tall as a key: a cross, or up and down. */
        {
            CGFloat cy = PROMPTS_TOP + 9, r = 11, t = 5;
            NSBezierPath *path = [NSBezierPath bezierPath];
            if (adjust) {
                [path moveToPoint:NSMakePoint(middle, cy - r)]; [path lineToPoint:NSMakePoint(middle + t * wide, cy - 3)]; [path lineToPoint:NSMakePoint(middle - t * wide, cy - 3)]; [path closePath];
                [path moveToPoint:NSMakePoint(middle, cy + r)]; [path lineToPoint:NSMakePoint(middle + t * wide, cy + 3)]; [path lineToPoint:NSMakePoint(middle - t * wide, cy + 3)]; [path closePath];
                [path moveToPoint:NSMakePoint(middle - r * wide, cy)]; [path lineToPoint:NSMakePoint(middle - 3 * wide, cy - t)]; [path lineToPoint:NSMakePoint(middle - 3 * wide, cy + t)]; [path closePath];
                [path moveToPoint:NSMakePoint(middle + r * wide, cy)]; [path lineToPoint:NSMakePoint(middle + 3 * wide, cy - t)]; [path lineToPoint:NSMakePoint(middle + 3 * wide, cy + t)]; [path closePath];
            } else {
                CGFloat w = 5 * wide, gap = 3 * wide, left = middle - gap / 2 - 2 * w, right = middle + gap / 2;
                [path moveToPoint:NSMakePoint(left + w, cy - 6)]; [path lineToPoint:NSMakePoint(left + 2 * w, cy + 6)]; [path lineToPoint:NSMakePoint(left, cy + 6)]; [path closePath];
                [path moveToPoint:NSMakePoint(right, cy - 6)]; [path lineToPoint:NSMakePoint(right + 2 * w, cy - 6)]; [path lineToPoint:NSMakePoint(right + w, cy + 6)]; [path closePath];
            }
            [black_ui_color(TEXT, 1) setFill]; [path fill];
        }
        CGFloat confirm_y = PROMPTS_TOP + PROMPT_STEP, back_y = PROMPTS_TOP + 2 * PROMPT_STEP;
        text(T(@"SELECT"), words_x, PROMPTS_TOP, size, TEXT, 0);
        text(keys[0], middle - text_width(keys[0], size, 0) / 2, confirm_y, size, TEXT, 0);
        text(T(@"CONFIRM"), words_x, confirm_y, size, TEXT, 0);
        if (reset_row(selected, 0)) {
            char reset[32];
            black_InputGlyphUTF8(black_InputPromptFamily() == BLACK_PROMPTS_KEYBOARD ? 0xE200 + 51 : black_InputButtonGlyph(3, 1), reset, sizeof reset);
            CGFloat key_x = VALUE_LEFT + 9;
            text(@(reset), key_x, confirm_y, size, TEXT, 0);
            text(T(@"DEFAULT"), key_x + text_width(@(reset), size, 0) + 14, confirm_y, size, TEXT, 0);
        }
        text(keys[1], middle - text_width(keys[1], size, 0) / 2, back_y, size, TEXT, 0);
        text(T(@"BACK"), words_x, back_y, size, TEXT, 0);
        back_hint_x = 88; back_hint_y = back_y - 8;
        back_hint_w = words_x - 88 + text_width(T(@"BACK"), size, 0) + 8;
        BOOL failed = black_SettingsSaveFailed();
        NSString *status = T(failed ? @"COULD NOT SAVE. SESSION ONLY." : changed ? @"SAVED" : @"");
        if (status.length) { CGFloat w = MIN(text_width(status, 11, 0), 240); fitted_text(status, 876 - w, back_y + 1, 11, 240, failed ? WARN : DIM, 0); }
    }
    text_scale = 1; text_aspect = 1;
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
{ (void)note; menu_view->binding_capture = NO; menu_view->pad_capture = NO; menu_view->axis_edit=NO; black_PadCaptureCancel(); black_ControllerCancelMeasure(); black_KeyReleaseAll(); [menu_view setNeedsDisplay:YES]; }
- (void)windowDidBecomeKey:(NSNotification *)note
{
    (void)note;
    [menu_window makeFirstResponder:black_SettingsPanelOpen() ? (NSView *)menu_view : game_view];
}
@end
static BlackMenuActions *actions;
static NSMenuItem *settings_item;
static BOOL reduced_motion(void)
{
    return NSWorkspace.sharedWorkspace.accessibilityDisplayShouldReduceMotion;
}

/* A page asked for at launch (BLACK_OPEN_SETTINGS), shown when the panel opens.
 * The row is an index resolved there: the Saves pages list what is on disk. */
static int requested_page = -1, requested_row;
static void request_page(const char *name, int row)
{
    int number = name[0] == '#' ? atoi(name + 1) : -1; /* "#3": by page number, for pages that share a name */
    for (int p = 0; p < MENU_PAGES && strcmp(name, "1"); ++p)
        if (p == number || !strcasecmp(page_names[p].UTF8String, name)) { requested_page = p; requested_row = row; break; }
}
static void set_open(int open)
{
    if (open) {
        closing = 0;
        if (!shown) {
            shown = 1; black_MenuSetOpen(1);
            /* The game may have saved or loaded since: list profiles afresh. */
            if (black_menu_tab(menu_view->page) == 3 && menu_view->page != PAGE_CONFIRM) {
                refresh_saves(); refresh_slot_rows(); if (menu_view->page == PAGE_IMPORT_PICK) refresh_picks();
                [menu_view selectRow:menu_view->selected];
            }
            if (capture_input) capture_input(1);
            menu_view.hidden = NO;
            menu_view.alphaValue = reduced_motion() ? 1 : 0;
            transition = CACurrentMediaTime();
            [menu_view setFrameSize:container.bounds.size];
            if (requested_page >= 0) {
                const int page = requested_page, index = requested_row;
                requested_page = -1;
                if (black_menu_tab(page) == 3 && page != PAGE_CONFIRM) {
                    refresh_saves(); refresh_slot_rows(); if (page == PAGE_IMPORT_PICK) refresh_picks();
                }
                [menu_view openPage:page row:black_menu_row(page, index)];
            }
            else if(black_menu_tab(menu_view->page)==1)[menu_view openPage:PAGE_CONTROLS row:ROW_DEVICE];
            else [menu_view selectRow:menu_view->selected];
            fprintf(stderr,"[MENU] opened %s\n",page_names[menu_view->page].UTF8String);
        }
    } else if (shown && !closing) {
        black_ControllerCancelMeasure(); menu_view->binding_capture = NO; menu_view->pad_capture = NO; black_PadCaptureCancel();
        closing = 1; transition = CACurrentMediaTime();
    }
}

/* While the game window has focus, ask GameController to hand Home / Guide /
 * PS to the game (tap: settings, hold: quit dialog) instead of the system
 * gesture (Game Center / Games). A preference of this process only: the
 * user's shortcut settings stay as they are and macOS may still keep the
 * button. Out of focus the system gesture is restored. */
static void home_priority(BOOL focused)
{
    static int last = -1;
    static NSUInteger seen = NSUIntegerMax;
    NSArray<GCController *> *controllers = GCController.controllers;
    if (last == focused && seen == controllers.count) return;
    last = focused; seen = controllers.count;
    unsigned exposed = 0;
    for (GCController *controller in controllers) {
        GCControllerButtonInput *home = controller.physicalInputProfile.buttons[GCInputButtonHome];
        if (!home) continue;
        ++exposed;
        home.preferredSystemGestureState = focused ? GCSystemGestureStateDisabled : GCSystemGestureStateEnabled;
    }
    if (exposed) fprintf(stderr, "[HOME] system gesture %s (%u controller%s)\n",
        focused ? "deferred to the game" : "restored", exposed, exposed == 1 ? "" : "s");
}

static void quit_close(void)
{
    if (!black_QuitDialogOpen()) return;
    /* The game resumes at once; the page fades over it (black_MenuPump). */
    quit_closing = 1; quit_closed = CACurrentMediaTime();
    black_QuitDialogSetOpen(0);
    if (!black_SettingsPanelOpen() && capture_input) capture_input(0);
    [menu_window makeFirstResponder:game_view];
    fprintf(stderr, "[HOME] quit dialog closed\n");
}
static void quit_open(void)
{
    if (black_QuitDialogOpen() || !quit_view) return;
    if (black_SettingsPanelOpen()) {
        /* Over the panel: the panel goes at once, the dialog takes its place. */
        menu_view.hidden = YES; shown = closing = 0; black_MenuSetOpen(0);
    }
    quit_count = 0;
    quit_options[quit_count++] = QUIT_CONTINUE;
    if (quit_restart) quit_options[quit_count++] = QUIT_RESTART;
    else {
        if (black_GameInMission()) quit_options[quit_count++] = QUIT_MAIN_MENU;
        quit_options[quit_count++] = QUIT_GAME;
    }
    quit_selected = 0;
    quit_closing = 0; quit_opened = quit_moved = CACurrentMediaTime();
    quit_from_y = quit_row_rect(0).origin.y;
    black_QuitDialogSetOpen(1);
    if (capture_input) capture_input(1);
    [quit_view setFrameSize:container.bounds.size];
    quit_view.hidden = NO; [quit_view setNeedsDisplay:YES];
    [menu_window makeFirstResponder:quit_view];
    if (quit_restart) fprintf(stderr, "[CHECKPOINT] restart question opened\n");
    else fprintf(stderr, "[HOME] quit dialog opened%s\n", black_GameInMission() ? " in a mission" : "");
}
static void quit_choose(int index)
{
    if (index < 0 || index >= quit_count) return;
    int option = quit_options[index];
    quit_close();
    if (option == QUIT_MAIN_MENU) {
        black_QuitToMenuRequest();
        if (!black_GamePaused()) black_InputPulseStart();
        fprintf(stderr, "[HOME] quit to main menu requested\n");
    } else if (option == QUIT_GAME) request_quit();
    else if (option == QUIT_RESTART) {
        black_CheckpointConfirmRestart();
        fprintf(stderr, "[CHECKPOINT] restart confirmed\n");
    }
}
/* Pad and keyboard commands while the dialog is up. */
static void quit_command(unsigned command)
{
    if (command & (BLACK_MENU_BACK | BLACK_MENU_TOGGLE)) quit_close();
    else if (command & (BLACK_MENU_UP | BLACK_MENU_DOWN)) {
        quit_from_y = quit_row_rect(quit_selected).origin.y; quit_moved = CACurrentMediaTime();
        quit_selected = (quit_selected + ((command & BLACK_MENU_DOWN) ? 1 : quit_count - 1)) % quit_count;
        [quit_view setNeedsDisplay:YES];
    } else if (command & BLACK_MENU_CHANGE) quit_choose(quit_selected);
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
    film_hint_view = [[BlackFilmHintView alloc] initWithFrame:container.bounds];
    film_hint_view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    film_hint_view.hidden = YES;
    [container addSubview:film_hint_view];
    [container addSubview:menu_view];
    quit_view = [[BlackQuitView alloc] initWithFrame:container.bounds];
    quit_view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    quit_view.hidden = YES;
    [container addSubview:quit_view];
    black_SubtitleAttach(container);
    [menu_window makeFirstResponder:game_view];
    event_monitor = [[NSEvent addLocalMonitorForEventsMatchingMask:
        NSEventMaskKeyDown | NSEventMaskKeyUp | NSEventMaskFlagsChanged | NSEventMaskLeftMouseDown |
        NSEventMaskRightMouseDown | NSEventMaskOtherMouseDown | NSEventMaskScrollWheel handler:^NSEvent *(NSEvent *event) {
        if (event.window != menu_window) return event;
        BOOL mouse = event.type == NSEventTypeLeftMouseDown || event.type == NSEventTypeRightMouseDown ||
            event.type == NSEventTypeOtherMouseDown || event.type == NSEventTypeScrollWheel;
        if (black_SettingsPanelOpen() && !(event.modifierFlags & NSEventModifierFlagCommand) &&
            ((event.type == NSEventTypeKeyDown && !event.isARepeat) ||
             event.type == NSEventTypeLeftMouseDown || event.type == NSEventTypeRightMouseDown || event.type == NSEventTypeOtherMouseDown ||
             (event.type == NSEventTypeScrollWheel && event.scrollingDeltaY != 0)))
            black_InputNoteMenuKeyboard();
        if (mouse) {
            if (black_SettingsPanelOpen() && menu_view->pad_capture) return nil;
            if (black_SettingsPanelOpen() && menu_view->binding_capture) {
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
        /* Other Cmd shortcuts are the menus' (Cmd+H, Cmd+M, Ctrl+Cmd+F...),
         * also with the settings open; Cmd+, stays the settings' toggle. */
        if ((event.modifierFlags & NSEventModifierFlagCommand) && code != 43 &&
            !(black_SettingsPanelOpen() && (menu_view->binding_capture || menu_view->pad_capture))) return event;
        if (black_SettingsPanelOpen() && menu_view->pad_capture) {
            if (down && !event.isARepeat && (code == 53 || code == 117 || code == 122 || code == 109)) {
                if (code == 117) black_SettingsSetPadBinding(menu_view->selected - ROW_PAD_FIRST,-1);
                menu_view->pad_capture = NO; black_PadCaptureCancel();
                [menu_view refreshValues]; [menu_view setNeedsDisplay:YES];
            }
            return nil;
        }
        if (black_SettingsPanelOpen() && menu_view->binding_capture) {
            if (event.modifierFlags & NSEventModifierFlagCommand) return nil;
            if (down && !event.isARepeat) [menu_view captureBinding:code == 53 ? -2 : code == 117 ? -1 : code];
            if (event.type == NSEventTypeFlagsChanged) {
                NSUInteger mask = code == 56 || code == 60 ? NSEventModifierFlagShift : code == 58 || code == 61 ? NSEventModifierFlagOption :
                    code == 59 || code == 62 ? NSEventModifierFlagControl : 0;
                if (event.modifierFlags & mask) [menu_view captureBinding:code];
            }
            return nil;
        }
        if (black_QuitDialogOpen()) {
            if (event.modifierFlags & NSEventModifierFlagCommand) return event;
            if (down && !event.isARepeat) {
                if (code == 53 || code == 11 || code == 122 || code == 109) black_MenuCommand(BLACK_MENU_BACK);
                else if (code == 36 || code == 76 || code == 49) black_MenuCommand(BLACK_MENU_CHANGE);
                else if (code == 126) black_MenuCommand(BLACK_MENU_UP);
                else if (code == 125) black_MenuCommand(BLACK_MENU_DOWN);
            }
            return nil;
        }
        BOOL shortcut = code == 122 || code == 109 || (code == 43 && (event.modifierFlags & NSEventModifierFlagCommand));
        if (shortcut) {
            if (down && !event.isARepeat) black_MenuCommand(BLACK_MENU_TOGGLE);
            return nil;
        }
        if (!black_SettingsPanelOpen()) return event;
        if (down && !event.isARepeat) {
            if (code == 117 && menu_view->selected >= ROW_PAD_FIRST && menu_view->selected <= ROW_PAD_LAST) {
                black_SettingsSetPadBinding(menu_view->selected-ROW_PAD_FIRST,-1);
                [menu_view refreshValues]; [menu_view setNeedsDisplay:YES];
            } else if (code == 117 && is_binding(menu_view->selected)) {
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
            else if (code == 51) black_MenuCommand(BLACK_MENU_RESET);
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
    /* What a Mac app's menus have: Hide (Cmd+H), Hide Others, Show All, and
     * a Window menu with Minimize (Cmd+M), Zoom, full screen (Ctrl+Cmd+F)
     * and Bring All to Front. Their actions go to NSApp and the key window. */
    if (![app_menu itemWithTitle:@"Hide BLACK"]) {
        NSInteger at = [app_menu indexOfItem:quit];
        [app_menu insertItem:NSMenuItem.separatorItem atIndex:at++];
        [app_menu insertItemWithTitle:@"Hide BLACK" action:@selector(hide:) keyEquivalent:@"h" atIndex:at++];
        NSMenuItem *others = [app_menu insertItemWithTitle:@"Hide Others" action:@selector(hideOtherApplications:) keyEquivalent:@"h" atIndex:at++];
        others.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagOption;
        [app_menu insertItemWithTitle:@"Show All" action:@selector(unhideAllApplications:) keyEquivalent:@"" atIndex:at++];
        [app_menu insertItem:NSMenuItem.separatorItem atIndex:at];
    }
    if (![main itemWithTitle:@"Window"]) {
        NSMenu *window_menu = [[[NSMenu alloc] initWithTitle:@"Window"] autorelease];
        [window_menu addItemWithTitle:@"Minimize" action:@selector(performMiniaturize:) keyEquivalent:@"m"];
        [window_menu addItemWithTitle:@"Zoom" action:@selector(performZoom:) keyEquivalent:@""];
        [window_menu addItem:NSMenuItem.separatorItem];
        NSMenuItem *full = [window_menu addItemWithTitle:@"Enter Full Screen" action:@selector(toggleFullScreen:) keyEquivalent:@"f"];
        full.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagControl;
        [window_menu addItem:NSMenuItem.separatorItem];
        [window_menu addItemWithTitle:@"Bring All to Front" action:@selector(arrangeInFront:) keyEquivalent:@""];
        NSMenuItem *item = [[[NSMenuItem alloc] initWithTitle:@"Window" action:nil keyEquivalent:@""] autorelease];
        item.submenu = window_menu; [main addItem:item];
        NSApp.windowsMenu = window_menu;
    }
    const char *open = getenv("BLACK_OPEN_SETTINGS");
    if (open && strchr(open, '@')) return; /* "...@seconds": black_MenuPump opens it later */
    if (open) {
        /* "1": the top menu; "PAGE NAME[/ROW]": that page and row (README
         * screenshots and checks; the page's English name, any case). */
        char name[64]; snprintf(name, sizeof name, "%s", open);
        char *slash = strchr(name, '/'); int row = slash ? atoi(slash + 1) : 0; if (slash) *slash = 0;
        request_page(name, row);
        black_MenuCommand(BLACK_MENU_TOGGLE);
    }
}

#include "window_state.h"
void black_MenuPump(void)
{
    black_WindowStatePump();
    {   /* BLACK_OPEN_SETTINGS="PAGE[/ROW]@seconds": open it that long after
         * the panel exists, once the game is past its title (screenshots). */
        static double opened_at; static int opened;
        const char *open = getenv("BLACK_OPEN_SETTINGS"), *at = open ? strchr(open, '@') : NULL;
        if (at && menu_view && !opened) {
            if (!opened_at) opened_at = CACurrentMediaTime();
            if (CACurrentMediaTime() - opened_at >= atof(at + 1)) {
                opened = 1;
                char name[64]; snprintf(name, sizeof name, "%.*s", (int)(at - open), open);
                char *slash = strchr(name, '/'); int row = slash ? atoi(slash + 1) : 0; if (slash) *slash = 0;
                request_page(name, row);
                black_MenuCommand(BLACK_MENU_TOGGLE);
            }
        }
    }
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
    /* Same kind of check for the Home hold: the dialog's own command, then its
     * Quit to Main Menu (or Quit Game outside a mission) a second later. */
    const char *test_home = getenv("BLACK_TEST_HOME_QUIT");
    static int home_step;
    if (test_home && menu_window && home_step < 2) {
        if (!test_start) test_start = CACurrentMediaTime();
        double at = atof(test_home), now = CACurrentMediaTime() - test_start;
        if (home_step == 0 && now >= at) { home_step = 1; black_MenuCommand(BLACK_MENU_QUIT); }
        else if (home_step == 1 && strstr(test_home, "stay")) home_step = 2;
        else if (home_step == 1 && now >= at + 1 && black_QuitDialogOpen()) {
            home_step = 2; quit_selected = quit_count - (quit_count == 3 ? 2 : 1);
            black_MenuCommand(BLACK_MENU_CHANGE);
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
    {
        /* A stalled game thread leaves no stale hint behind. */
        BOOL hint = atomic_load(&film_hint) && !shown && CACurrentMediaTime() - atomic_load(&film_hint_seen) < .5;
        static unsigned hint_glyph;
        unsigned glyph = black_InputButtonGlyph(0, 1);
        if (hint != !film_hint_view.hidden || (hint && glyph != hint_glyph)) {
            film_hint_view.hidden = !hint; hint_glyph = glyph;
            if (hint) [film_hint_view setNeedsDisplay:YES];
        }
    }
    if (menu_view->pad_capture) {
        int captured = black_PadCaptureTake();
        if (captured >= -1) {
            menu_view->pad_capture = NO; menu_view->pad_disconnected = captured == -1;
            if (captured >= 0) black_SettingsSetPadBinding(menu_view->selected - ROW_PAD_FIRST,captured);
            [menu_view refreshValues]; [menu_view setNeedsDisplay:YES];
        }
    }
    home_priority(NSApp.active && menu_window.keyWindow);
    unsigned command = black_MenuTakeCommands();
    /* A pack dialog or folder panel takes the pad: A answers, B cancels. */
    NSWindow *sheet = menu_window.attachedSheet;
    if (sheet) {
        if (pack_alert && sheet == pack_alert.window) {
            if (command & BLACK_MENU_CHANGE) [pack_alert.buttons.firstObject performClick:nil];
            else if (command & BLACK_MENU_BACK) [pack_alert.buttons.lastObject performClick:nil];
        } else if ((command & BLACK_MENU_BACK) && [sheet isKindOfClass:NSSavePanel.class]) [(NSSavePanel *)sheet cancel:nil];
        command = 0;
    }
    if(menu_view->pad_capture&&(command&BLACK_MENU_BACK)){
        menu_view->pad_capture=NO;black_PadCaptureCancel();[menu_view setNeedsDisplay:YES];return;
    }
    if (!quit_view.hidden) {
        double now = CACurrentMediaTime();
        if (quit_closing && now - quit_closed >= 0.14) { quit_view.hidden = YES; quit_closing = 0; }
        else if (quit_closing || now - quit_opened < 0.25 || now - quit_moved < 0.15) [quit_view setNeedsDisplay:YES];
    }
    if (command & BLACK_MENU_QUIT) { quit_restart = 0; quit_open(); command = 0; }
    if (command & BLACK_MENU_RESTART) { if (!black_SettingsPanelOpen()) { quit_restart = 1; quit_open(); } command &= ~(unsigned)BLACK_MENU_RESTART; }
    if (black_QuitDialogOpen()) { if (command) quit_command(command); command = 0; }
    if (command & BLACK_MENU_TOGGLE) set_open(!black_SettingsPanelOpen());
    else if (command & BLACK_MENU_BACK) {
        if(![menu_view navigateBack])set_open(0);
    }
    else if (black_SettingsPanelOpen() && !closing) {
        if (command & (BLACK_MENU_UP | BLACK_MENU_DOWN)) {
            [menu_view stepRow:(command & BLACK_MENU_DOWN) ? 1 : -1];
        } else if (command & (BLACK_MENU_PREV_TAB | BLACK_MENU_NEXT_TAB)) {
            [menu_view stepTab:(command & BLACK_MENU_NEXT_TAB) ? 1 : -1];
        } else if (command & BLACK_MENU_RESET) [menu_view resetSelected];
        else if (command & BLACK_MENU_CHANGE) [menu_view changeOption:nil];
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
