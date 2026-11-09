/* Shared native drawing in the look of BLACK's menus; see black_ui.h. */
#import <QuartzCore/QuartzCore.h>
#import "black_ui.h"
#include "input_atlas.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

static uint32_t read32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static float read_float(const uint8_t *p) { float v; memcpy(&v, p, 4); return v; }

void black_ui_free_font(BlackUIFont *font)
{
    for (int i = 0; i < 256; ++i) if (font->image[i]) CGImageRelease(font->image[i]);
    memset(font, 0, sizeof *font);
}

void black_ui_load_font(BlackUIFont *font, NSData *data)
{
    if (data.length < 0x220) return;
    const uint8_t *b = data.bytes;
    if (memcmp(b, "4.1v", 4)) return;
    uint32_t texture = read32(b + 4);
    if ((uint64_t)texture + 128 > data.length) return;
    uint32_t format = read32(b + texture + 12);
    unsigned width = 1u << ((format >> 20) & 15), height = 1u << ((format >> 24) & 15);
    if (((format >> 8) & 0xFF) != 0x0E || width < 4 || height < 4 ||
        width > 1024 || height > 4096 || (uint64_t)texture + 128 + width * height > data.length) return;
    /* Xbox DXT3 blocks: explicit 4-bit alpha followed by the color block.
     * The original glyph atlas is white; only alpha is needed for tinting. */
    NSMutableData *alpha = [NSMutableData dataWithLength:width * height * 4];
    uint8_t *pixels = alpha.mutableBytes;
    const uint8_t *blocks = b + texture + 128;
    for (unsigned by = 0; by < height / 4; ++by)
        for (unsigned bx = 0; bx < width / 4; ++bx) {
            const uint8_t *block = blocks + (by * (width / 4) + bx) * 16;
            for (unsigned i = 0; i < 16; ++i) {
                unsigned pixel = ((by * 4 + i / 4) * width + bx * 4 + i % 4) * 4;
                pixels[pixel] = pixels[pixel + 1] = pixels[pixel + 2] = 255;
                pixels[pixel + 3] = ((block[i / 2] >> ((i & 1) * 4)) & 15) * 17;
            }
        }
    /* Private guest fonts use host atlas coordinates; source fonts do not. */
    int custom=0;uint32_t last=read32(b+0x1C);
    if(last<0x220||last>texture||(last-0x220)%40)return;
    for(uint32_t at=0x220;at+40<=last;at+=40){uint16_t c;memcpy(&c,b+at+28,2);if(c==0xE100)custom=1;}
    BlackInputAtlas input;
    if(custom&&black_InputAtlasFind(blocks,width*height,width,height,14,&input)) {
        height=input.height;alpha=[NSMutableData dataWithBytes:input.rgba length:width*height*4];
    }
    CGDataProviderRef provider = CGDataProviderCreateWithCFData((CFDataRef)alpha);
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGImageRef atlas = CGImageCreate(width, height, 8, 32, width * 4, space,
        (CGBitmapInfo)kCGImageAlphaLast, provider, NULL, false, kCGRenderingIntentDefault);
    CGColorSpaceRelease(space);
    CGDataProviderRelease(provider);
    if (!atlas) return;
    /* Scan the glyph pool rather than trusting the file's hash-chain offsets. */
    for (uint32_t at = 0x220; (uint64_t)at + 40 <= texture; at += 40) {
        uint16_t code; memcpy(&code, b + at + 28, 2);
        if (code >= 256) continue;
        float u = read_float(b + at), v = read_float(b + at + 4);
        float w = read_float(b + at + 8), h = read_float(b + at + 12);
        float advance = read_float(b + at + 24);
        if (!isfinite(u) || !isfinite(v) || !isfinite(w) || !isfinite(h) ||
            !isfinite(advance) || advance < 0 || advance > 1) continue;
        font->advance[code] = advance * width;
        font->width[code] = w * width; font->height[code] = h * height;
        font->left[code] = read_float(b + at + 16) * width;
        font->top[code] = read_float(b + at + 20) * height;
        if (w > 0 && h > 0 && u >= 0 && v >= 0 && u + w <= 1.001 && v + h <= 1.001) {
            if (font->image[code]) CGImageRelease(font->image[code]);
            font->image[code] = CGImageCreateWithImageInRect(atlas,
                CGRectMake(round(u * width), round(v * height), round(w * width), round(h * height)));
        }
    }
    font->cap = font->height['A'];
    font->atlas_width = width; font->atlas_height = height;
    CGImageRelease(atlas);
}

NSColor *black_ui_color(unsigned rgb, CGFloat alpha)
{
    return [NSColor colorWithSRGBRed:((rgb >> 16) & 255) / 255.0
        green:((rgb >> 8) & 255) / 255.0 blue:(rgb & 255) / 255.0 alpha:alpha];
}
/* NSRectFill copies, alpha included: a translucent fill replaces what is under it. */
void black_ui_fill(NSRect r, unsigned rgb, CGFloat opacity) { [black_ui_color(rgb, opacity) setFill]; NSRectFill(r); }

/* Text the game's font cannot draw (Cyrillic, or no game font) uses a system
 * font sized so its capitals match `size`, the game font's cap height. */
static int game_glyphs(const BlackUIFont *font, NSString *value)
{
    if (!font || !(font->cap > 0)) return 0;
    for (NSUInteger i = 0; i < value.length; ++i) {
        unichar c = [value characterAtIndex:i];
        if (c >= 256 || (!font->image[c] && c != ' ' && !(font->advance[c] > 0))) return 0;
    }
    return 1;
}
static NSFont *system_font(NSString *name, CGFloat size)
{
    NSFont *probe = [NSFont fontWithName:name size:100] ?: [NSFont boldSystemFontOfSize:100];
    CGFloat points = size * 100 / (probe.capHeight > 0 ? probe.capHeight : 72);
    return [NSFont fontWithName:probe.fontName size:points] ?: [NSFont boldSystemFontOfSize:points];
}
void black_ui_text(const BlackUIFont *font, NSString *fallback, NSString *value,
    CGFloat x, CGFloat y, CGFloat size, unsigned rgb, CGFloat alpha)
{
    if (!game_glyphs(font, value)) {
        NSFont *system = system_font(fallback, size);
        /* Flipped view: the line box starts at the ascender; put the capitals' top at y. */
        [value drawAtPoint:NSMakePoint(x, y - (system.ascender - system.capHeight)) withAttributes:@{
            NSFontAttributeName:system, NSForegroundColorAttributeName:black_ui_color(rgb, alpha)}];
        return;
    }
    CGContextRef ctx = NSGraphicsContext.currentContext.CGContext;
    CGFloat scale = size / font->cap;
    for (NSUInteger i = 0; i < value.length; ++i) {
        unichar c = [value characterAtIndex:i];
        if (font->image[c]) {
            CGRect r = CGRectMake(x + font->left[c] * scale, y + font->top[c] * scale,
                font->width[c] * scale, font->height[c] * scale);
            CGContextSaveGState(ctx);
            CGContextTranslateCTM(ctx, r.origin.x, r.origin.y + r.size.height);
            CGContextScaleCTM(ctx, 1, -1);
            CGRect local = CGRectMake(0, 0, r.size.width, r.size.height);
            CGContextClipToMask(ctx, local, font->image[c]);
            CGContextSetFillColorWithColor(ctx, black_ui_color(rgb, alpha).CGColor);
            CGContextFillRect(ctx, local);
            CGContextRestoreGState(ctx);
        }
        x += (font->advance[c] > 0 ? font->advance[c] : font->cap * 0.4) * scale;
    }
}

CGFloat black_ui_text_width(const BlackUIFont *font, NSString *fallback, NSString *value, CGFloat size)
{
    CGFloat advance = 0;
    if (game_glyphs(font, value)) {
        for (NSUInteger i = 0; i < value.length; ++i) {
            unichar c = [value characterAtIndex:i];
            advance += (font->advance[c] > 0 ? font->advance[c] : font->cap * .4) * size / font->cap;
        }
    } else advance = [value sizeWithAttributes:@{NSFontAttributeName:system_font(fallback, size)}].width;
    return advance;
}

static void over(NSRect r, unsigned rgb, CGFloat opacity)
{ [black_ui_color(rgb, opacity) setFill]; NSRectFillUsingOperation(r, NSCompositingOperationSourceOver); }

void black_ui_rails(CGFloat x, CGFloat top, CGFloat bottom, CGFloat glow)
{ black_ui_rails_band(x, 48, top, bottom, glow, .9); }

/* The game's rail band, as its own menus draw it: black between a broad pale
 * rail on the left (a thin line beside it) and two thin lines and a broad
 * rail on the right; `band` spans both rails. A warm light passes down it and
 * the scales outside both rails creep downwards; still when glow < 0. */
void black_ui_rails_band(CGFloat x, CGFloat band, CGFloat top, CGFloat bottom, CGFloat glow, CGFloat fill)
{
    over(NSMakeRect(x, top, band, bottom - top), 0x010201, fill);
    if (glow >= 0) {
        CGFloat center = top + (bottom - top) * glow, reach = 90;
        NSGradient *light = [[[NSGradient alloc] initWithColorsAndLocations:
            black_ui_color(0xd9a54a, 0), 0.0, black_ui_color(0xf0c86a, .42), 0.5, black_ui_color(0xd9a54a, 0), 1.0, nil] autorelease];
        [light drawInRect:NSMakeRect(x + 6, center - reach, band - 24, reach * 2) angle:90];
    }
    over(NSMakeRect(x, top, 6, bottom - top), BLACK_UI_RAIL, .95);
    over(NSMakeRect(x + 10, top, 1.5, bottom - top), BLACK_UI_RAIL, .7);
    over(NSMakeRect(x + band - 17, top, 1.5, bottom - top), BLACK_UI_RAIL, .8);
    over(NSMakeRect(x + band - 12, top, 1.5, bottom - top), BLACK_UI_RAIL, .8);
    over(NSMakeRect(x + band - 5, top, 5, bottom - top), BLACK_UI_RAIL, .95);
    CGFloat creep = glow >= 0 ? fmod(CACurrentMediaTime() * 6, 16) : 0;
    for (CGFloat y = top + 6 - 16 + creep; y < bottom; y += 16) {
        if (y < top) continue;
        over(NSMakeRect(x - 7, y, 6, 1), BLACK_UI_RAIL, .5);
        over(NSMakeRect(x + band + 1, y, 6, 1), BLACK_UI_RAIL, .5);
    }
}

void black_ui_selector(NSRect bar, CGFloat rail, CGFloat edge)
{
    CGFloat mid = NSMidY(bar);
    over(bar, BLACK_UI_BAR, .88);
    over(NSMakeRect(NSMinX(bar) - 1, NSMinY(bar), 2, NSHeight(bar)), BLACK_UI_TEXT, .9);
    over(NSMakeRect(NSMaxX(bar) - 1, NSMinY(bar), 2, NSHeight(bar)), BLACK_UI_TEXT, .9);
    if (NSMinX(bar) > rail) over(NSMakeRect(rail, mid, NSMinX(bar) - rail, 1.5), BLACK_UI_TEXT, .8);
    if (edge > NSMaxX(bar)) over(NSMakeRect(NSMaxX(bar), mid, edge - NSMaxX(bar), 1.5), BLACK_UI_TEXT, .8);
}

void black_ui_brackets(CGFloat x, CGFloat y, CGFloat size, unsigned rgb)
{
    CGFloat arm = size * .32, t = MAX(1.5, size * .12);
    for (int corner = 0; corner < 4; ++corner) {
        CGFloat cx = corner & 1 ? x + size - arm : x, cy = corner & 2 ? y + size - t : y;
        over(NSMakeRect(cx, cy, arm, t), rgb, 1);
        over(NSMakeRect(corner & 1 ? x + size - t : x, corner & 2 ? y + size - arm : y, t, arm), rgb, 1);
    }
}
