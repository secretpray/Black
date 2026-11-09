/* Shared drawing for the native settings panel and the first-run setup:
 * the game's own '4.1v' menu fonts (read from the player's files, never
 * shipped), sRGB fills and text that falls back to a system font with the
 * same capital height when the game font cannot draw it. Flipped views. */
#pragma once
#import <Cocoa/Cocoa.h>

typedef struct {
    CGImageRef image[256]; /* Latin-1: the game's fonts carry its accented letters */
    float width[256], height[256], left[256], top[256], advance[256];
    float cap;
    unsigned atlas_width, atlas_height;
} BlackUIFont;

/* Parses a font file's bytes; leaves the font empty when they are not one. */
void black_ui_load_font(BlackUIFont *font, NSData *data);
void black_ui_free_font(BlackUIFont *font);

NSColor *black_ui_color(unsigned rgb, CGFloat alpha);
void black_ui_fill(NSRect r, unsigned rgb, CGFloat opacity);

/* `fallback` names the system font used when `font` lacks a glyph of `value`. */
void black_ui_text(const BlackUIFont *font, NSString *fallback, NSString *value,
    CGFloat x, CGFloat y, CGFloat size, unsigned rgb, CGFloat alpha);
CGFloat black_ui_text_width(const BlackUIFont *font, NSString *fallback, NSString *value, CGFloat size);

/* BLACK's own menu pages, sampled from the game's frames (difficulty and pause
 * menus, Metal 2x) and lifted by a third for a desktop window: a green-black
 * backdrop, grey headings and the selected row, green-grey rows. */
enum {
    BLACK_UI_BACKDROP = 0x0b1310, BLACK_UI_BAR = 0x070a08, BLACK_UI_TEXT = 0xbfbfbf,
    BLACK_UI_ROW = 0x4e6859, BLACK_UI_DISABLED = 0x37493d, BLACK_UI_RAIL = 0x878f83,
    BLACK_UI_TICK = 0x3d4540, BLACK_UI_WARN = 0xb86a52,
};
/* The rails down the left of every menu page: two light lines with ticks
 * outside, a dark band between them. `glow` 0-1 moves a faint light down the
 * band, as the game's backdrop does; pass a negative value for none. */
void black_ui_rails(CGFloat x, CGFloat top, CGFloat bottom, CGFloat glow);
/* As above with the band's width (black_ui_rails: 34). */
void black_ui_rails_band(CGFloat x, CGFloat band, CGFloat top, CGFloat bottom, CGFloat glow);
/* The selected row: a dark bar with light end caps and the selector line
 * running from `rail` to the bar and from the bar to `edge`. */
void black_ui_selector(NSRect bar, CGFloat rail, CGFloat edge);
/* The square brackets before an option in the game's lists. */
void black_ui_brackets(CGFloat x, CGFloat y, CGFloat size, unsigned rgb);
