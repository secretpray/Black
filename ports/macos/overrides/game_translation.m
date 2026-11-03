/* Russian game text in the installed copy; see game_translation.h. */
#import "game_translation.h"
#import <CommonCrypto/CommonDigest.h>
#import <CoreText/CoreText.h>
#include <math.h>
#include <string.h>

static BOOL fail(NSError **e, NSString *s)
{ if (e) *e = [NSError errorWithDomain:@"BLACK.Translation" code:1 userInfo:@{NSLocalizedDescriptionKey:s}]; return NO; }
static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint16_t rd16(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return v; }
static float rdf(const uint8_t *p) { float v; memcpy(&v, p, 4); return v; }
static void wr32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }
static NSString *sha256(NSData *d)
{
    unsigned char h[CC_SHA256_DIGEST_LENGTH]; CC_SHA256(d.bytes, (CC_LONG)d.length, h);
    NSMutableString *s = [NSMutableString string]; for (int i = 0; i < 32; ++i) [s appendFormat:@"%02x", h[i]]; return s;
}

NSURL *black_RussianStrings(void)
{
    NSURL *url = [NSBundle.mainBundle URLForResource:@"russian-strings" withExtension:@"json"];
    const char *path = getenv("BLACK_RUSSIAN_STRINGS");
    if (!url && path) url = [NSURL fileURLWithPath:@(path)];
    return url && [NSFileManager.defaultManager isReadableFileAtPath:url.path] ? url : nil;
}

NSURL *black_RussianSubtitles(void)
{
    NSURL *url = [NSBundle.mainBundle URLForResource:@"russian-subtitles" withExtension:nil];
    if (!url) url = [black_RussianStrings().URLByDeletingLastPathComponent URLByAppendingPathComponent:@"subtitles"];
    return [NSFileManager.defaultManager fileExistsAtPath:[url URLByAppendingPathComponent:@"03_N.srt"].path] ? url : nil;
}

NSDictionary<NSString *, id> *black_TranslationLoad(NSURL *json, NSError **e)
{
    NSData *data = [NSData dataWithContentsOfURL:json options:0 error:e];
    if (!data) return nil;
    id object = [NSJSONSerialization JSONObjectWithData:data options:0 error:e];
    if (![object isKindOfClass:NSDictionary.class]) { fail(e, @"The translation is not a JSON object."); return nil; }
    NSCharacterSet *hex = [[NSCharacterSet characterSetWithCharactersInString:@"0123456789ABCDEF"] invertedSet];
    for (NSString *key in object) {
        id value = object[key];
        if (key.length != 8 || [key rangeOfCharacterFromSet:hex].location != NSNotFound ||
            !([value isKindOfClass:NSString.class] || value == NSNull.null) ||
            ([value isKindOfClass:NSString.class] && [value rangeOfString:@"\0"].location != NSNotFound)) {
            fail(e, [NSString stringWithFormat:@"Invalid translation entry %@.", key]); return nil;
        }
    }
    return object;
}

/* Bank: u32 count at +8, u32 table offset (16) at +12, per entry a u32 ID and
 * NUL-terminated UTF-8, entries 4-byte aligned but the last. */
NSData *black_TranslatedBank(NSData *english, NSDictionary<NSString *, id> *strings, NSError **e)
{
    const uint8_t *d = english.bytes; NSUInteger n = english.length;
    if (n < 16) { fail(e, @"The English string bank is truncated."); return nil; }
    uint32_t count = rd32(d + 8), table = rd32(d + 12);
    if (table != 16 || count > 100000 || table + 4ull * count > n) { fail(e, @"Invalid English string bank."); return nil; }
    NSMutableData *out = [NSMutableData dataWithBytes:d length:table + 4 * count];
    NSUInteger replaced = 0;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t start = rd32(d + table + 4 * i), end = i + 1 < count ? rd32(d + table + 4 * (i + 1)) : (uint32_t)n;
        if (start < table + 4 * count || start + 5 > end || end > n) { fail(e, @"Invalid English string bank entry."); return nil; }
        const uint8_t *text = d + start + 4, *nul = memchr(text, 0, end - start - 4);
        if (!nul) { fail(e, @"Unterminated English string."); return nil; }
        id value = strings[[NSString stringWithFormat:@"%08X", rd32(d + start)]];
        wr32((uint8_t *)out.mutableBytes + table + 4 * i, (uint32_t)out.length);
        [out appendBytes:d + start length:4];
        if ([value isKindOfClass:NSString.class]) { NSData *u = [value dataUsingEncoding:NSUTF8StringEncoding]; [out appendData:u]; ++replaced; }
        else [out appendBytes:text length:nul - text];
        [out appendBytes:"\0\0\0\0" length:1];
        if (i + 1 < count) [out appendBytes:"\0\0\0\0" length:(4 - out.length % 4) % 4];
    }
    if (!replaced) { fail(e, @"No translated string matches this game's string bank."); return nil; }
    return out;
}

/* ---- fonts ---- */
static NSString *const same = @"АВЕКМНОРСТХЁ", *const latin = @"ABEKMHOPCTXË";
static NSString *const drawn = @"БГДЖЗИЙЛПУФЦЧШЩЪЫЬЭЮЯ";
enum { RECORDS = 0x220, RECORD = 40, BUCKETS = 0x20, DEFAULT_GLYPH = 0x1C };

typedef struct { int width, height, left, top; float advance; uint8_t *alpha; } Glyph; /* atlas pixels, 4-bit alpha */

/* One character drawn with `font` at the game's capital height `cap`, narrowed
 * by `sx`; left/top are the ink box from the pen at the capital line. */
static Glyph draw_glyph(CTFontRef font, unichar ch, CGFloat cap, CGFloat sx)
{
    Glyph g = {0};
    CGGlyph glyph; if (!CTFontGetGlyphsForCharacters(font, &ch, &glyph, 1)) return g;
    int w = (int)ceil(cap * 4), h = (int)ceil(cap * 3);
    CGFloat pen = cap, baseline = cap;          /* bitmap units, from the left and the bottom */
    uint8_t *px = calloc(w * h, 1);
    CGContextRef ctx = CGBitmapContextCreate(px, w, h, 8, w, NULL, (CGBitmapInfo)kCGImageAlphaOnly);
    CGContextSetShouldAntialias(ctx, true); CGContextSetAllowsFontSmoothing(ctx, false);
    CGContextScaleCTM(ctx, sx, 1);
    CGPoint at = CGPointMake(pen / sx, baseline);
    CTFontDrawGlyphs(font, &glyph, &at, 1, ctx);
    CGContextRelease(ctx);
    int x0 = w, y0 = h, x1 = -1, y1 = -1;
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) if (px[y * w + x] >= 9) {
        x0 = MIN(x0, x); x1 = MAX(x1, x); y0 = MIN(y0, y); y1 = MAX(y1, y);
    }
    CGSize advance; CTFontGetAdvancesForGlyphs(font, kCTFontOrientationHorizontal, &glyph, &advance, 1);
    if (x1 >= x0) {
        g.width = x1 - x0 + 1; g.height = y1 - y0 + 1; g.alpha = calloc(g.width * g.height, 1);
        for (int y = 0; y < g.height; ++y) for (int x = 0; x < g.width; ++x)
            g.alpha[y * g.width + x] = (px[(y0 + y) * w + x0 + x] + 8) / 17;
        CGFloat cap_row = h - baseline - CTFontGetCapHeight(font);   /* memory rows run top down */
        g.left = (int)lround(x0 - pen); g.top = (int)lround(y0 - cap_row);
    }
    g.advance = advance.width * sx;
    free(px);
    return g;
}

NSData *black_FontWithCyrillic(NSData *input, NSString *systemFont, NSError **e)
{
    NSMutableData *font = [[input mutableCopy] autorelease];
    uint8_t *b = font.mutableBytes; NSUInteger n = font.length;
    if (n < RECORDS + 128 || memcmp(b, "4.1v", 4)) { fail(e, @"Not a game font."); return nil; }
    uint32_t tex = rd32(b + 4), head = rd32(b + DEFAULT_GLYPH), format = tex + 128 <= n ? rd32(b + tex + 12) : 0;
    unsigned W = 1u << ((format >> 20) & 15), H = 1u << ((format >> 24) & 15);
    if (((format >> 8) & 0xFF) != 0x0E || head < RECORDS || head > tex || (head - RECORDS) % RECORD ||
        (uint64_t)tex + 128 + W * H > n || W > 1024 || H > 1024) { fail(e, @"Unsupported game font layout."); return nil; }
    NSUInteger glyphs = (head - RECORDS) / RECORD;
    int (^find)(unichar) = ^int(unichar c) {
        for (NSUInteger i = 0; i < glyphs; ++i) if (rd16(b + RECORDS + i * RECORD + 28) == c) return (int)i;
        return -1;
    };
    if (find(0x410) >= 0) return input;
    int H_ = find('H');
    if (H_ < 0) { fail(e, @"The game font has no H."); return nil; }
    const uint8_t *hr = b + RECORDS + H_ * RECORD;
    CGFloat cap = rdf(hr + 12) * H, cap_top = rdf(hr + 20);
    CTFontRef probe = CTFontCreateWithName((CFStringRef)systemFont, 100, NULL);
    CTFontRef ct = CTFontCreateCopyWithAttributes(probe, 100 * cap / CTFontGetCapHeight(probe), NULL, NULL);
    CFRelease(probe);
    /* Width of the game's letters against the system font's at the same capitals. */
    CGFloat ratios[4]; int nr = 0;
    for (NSString *c in @[@"H", @"N", @"O", @"U"]) {
        Glyph g = draw_glyph(ct, [c characterAtIndex:0], cap, 1);
        int at = find([c characterAtIndex:0]);
        if (g.width && at >= 0) ratios[nr++] = rdf(b + RECORDS + at * RECORD + 8) * W / g.width;
        free(g.alpha);
    }
    for (int i = 0; i < nr; ++i) for (int j = i + 1; j < nr; ++j) if (ratios[j] < ratios[i]) { CGFloat t = ratios[i]; ratios[i] = ratios[j]; ratios[j] = t; }
    CGFloat sx = nr ? ratios[nr / 2] : 1;
    /* Free atlas space: every glyph cell with a 2-pixel margin is taken. */
    uint8_t *taken = calloc(W * H, 1);
    for (uint32_t at = RECORDS; at + RECORD <= tex; at += RECORD) {
        float u = rdf(b + at), v = rdf(b + at + 4), w = rdf(b + at + 8), h = rdf(b + at + 12);
        if (!(u >= 0) || !(w > 0) || !(h > 0)) continue;
        int x0 = (int)(u * W) - 2, y0 = (int)(v * H) - 2, x1 = (int)ceil((u + w) * W) + 2, y1 = (int)ceil((v + h) * H) + 2;
        for (int y = MAX(0, y0); y < MIN((int)H, y1); ++y) for (int x = MAX(0, x0); x < MIN((int)W, x1); ++x) taken[y * W + x] = 1;
    }
    uint8_t *atlas = b + tex + 128;
    NSMutableData *records = [NSMutableData data];
    void (^record)(const float *, unichar) = ^(const float *f, unichar code) {
        uint8_t r[RECORD] = {0};
        for (int k = 0; k < 7; ++k) memcpy(r + 4 * k, &f[k], 4);
        memcpy(r + 28, &code, 2);
        [records appendBytes:r length:RECORD];
    };
    for (NSUInteger i = 0; i < same.length; ++i) {
        int at = find([latin characterAtIndex:i]);
        if (at < 0) { free(taken); CFRelease(ct); fail(e, @"The game font lacks a Latin letter Cyrillic shares."); return nil; }
        float f[7]; for (int k = 0; k < 7; ++k) f[k] = rdf(b + RECORDS + at * RECORD + 4 * k);
        unichar c = [same characterAtIndex:i];
        record(f, c); record(f, [[[NSString stringWithCharacters:&c length:1] lowercaseString] characterAtIndex:0]);
    }
    for (NSUInteger i = 0; i < drawn.length; ++i) {
        unichar c = [drawn characterAtIndex:i];
        Glyph g = draw_glyph(ct, c, cap, sx);
        int px = -1, py = -1;
        for (int y = 2; y + g.height + 2 <= (int)H && px < 0; ++y)
            for (int x = 2; x + g.width + 2 <= (int)W; ++x) {
                BOOL free_ = YES;
                for (int yy = y - 1; yy < y + g.height + 1 && free_; ++yy)
                    for (int xx = x - 1; xx < x + g.width + 1; ++xx) if (taken[yy * W + xx]) { free_ = NO; break; }
                if (free_) { px = x; py = y; break; }
            }
        if (!g.width || px < 0) { free(g.alpha); free(taken); CFRelease(ct); fail(e, @"No room in the game font's atlas."); return nil; }
        for (int yy = py - 2; yy < py + g.height + 2; ++yy) for (int xx = px - 2; xx < px + g.width + 2; ++xx)
            if (yy >= 0 && xx >= 0 && yy < (int)H && xx < (int)W) taken[yy * W + xx] = 1;
        /* DXT3: 4-bit alpha first in each 16-byte block, then a colour block made white. */
        for (int y = 0; y < g.height; ++y) for (int x = 0; x < g.width; ++x) {
            int X = px + x, Y = py + y, i4 = (Y % 4) * 4 + X % 4;
            uint8_t *blk = atlas + ((Y / 4) * (W / 4) + X / 4) * 16 + i4 / 2;
            *blk = (uint8_t)((*blk & ((i4 & 1) ? 0x0F : 0xF0)) | (g.alpha[y * g.width + x] << ((i4 & 1) * 4)));
        }
        for (int by = (py - 1) / 4; by <= (py + g.height) / 4; ++by) for (int bx = (px - 1) / 4; bx <= (px + g.width) / 4; ++bx)
            if (by >= 0 && bx >= 0 && by < (int)H / 4 && bx < (int)W / 4)
                memcpy(atlas + (by * (W / 4) + bx) * 16 + 8, "\xff\xff\xff\xff\0\0\0\0", 8);
        float f[7] = {(float)px / W, (float)py / H, (float)g.width / W, (float)g.height / H,
                      (float)g.left / W, (float)(cap_top + (double)g.top / H), (float)(g.advance / W)};
        record(f, c); record(f, [[[NSString stringWithCharacters:&c length:1] lowercaseString] characterAtIndex:0]);
        free(g.alpha);
    }
    free(taken); CFRelease(ct);
    /* Records sorted by (code & 127, code) up to the default glyph; buckets point at each run. */
    NSUInteger total = glyphs + records.length / RECORD;
    uint8_t *sorted = malloc(total * RECORD);
    memcpy(sorted, b + RECORDS, glyphs * RECORD);
    memcpy(sorted + glyphs * RECORD, records.bytes, records.length);
    qsort_b(sorted, total, RECORD, ^int(const void *a, const void *c) {
        uint16_t x = rd16((const uint8_t *)a + 28), y = rd16((const uint8_t *)c + 28);
        int kx = ((x & 127) << 16) | x, ky = ((y & 127) << 16) | y; return kx - ky;
    });
    for (NSUInteger i = 1; i < total; ++i)
        if (rd16(sorted + i * RECORD + 28) == rd16(sorted + (i - 1) * RECORD + 28)) { free(sorted); fail(e, @"Duplicate glyph code."); return nil; }
    NSMutableData *out = [NSMutableData dataWithBytes:b length:RECORDS];
    [out appendBytes:sorted length:total * RECORD];
    uint32_t new_head = (uint32_t)out.length;
    [out appendBytes:b + head length:tex - head];
    [out increaseLengthBy:(0x80 - out.length % 0x80) % 0x80];      /* the texture stays 128-byte aligned */
    uint32_t new_tex = (uint32_t)out.length;
    [out appendBytes:b + tex length:n - tex];
    uint8_t *o = out.mutableBytes;
    wr32(o + 4, new_tex); wr32(o + DEFAULT_GLYPH, new_head);
    for (int k = 0; k < 128; ++k) wr32(o + BUCKETS + 4 * k, new_head);
    for (NSUInteger i = total; i-- > 0;) wr32(o + BUCKETS + 4 * (rd16(sorted + i * RECORD + 28) & 127), RECORDS + (uint32_t)(i * RECORD));
    free(sorted);
    return out;
}

/* ---- the installed game ---- */
static NSDictionary *retail(void)
{
    return @{@"Big.bin": @[@"119ede7ec211ddebde76b46e4dcbea86e486f1b13549fc1fe58ecb5ed59611c5", @"Impact", @"HelveticaNeue-CondensedBlack"],
             @"Small.bin": @[@"f194f96f9389ba7f4cb126ac02e2f619f27df0339ab97df6fe7a740e5a1fca35", @"Arial-Black", @"Helvetica-Bold"]};
}
static BOOL installed(NSString *face)
{
    CTFontRef f = CTFontCreateWithName((CFStringRef)face, 12, NULL);
    CFStringRef name = CTFontCopyPostScriptName(f);
    BOOL found = CFStringCompare(name, (CFStringRef)face, 0) == kCFCompareEqualTo;
    CFRelease(name); CFRelease(f);
    return found;
}
static BOOL write_if_changed(NSData *data, NSURL *url, NSError **e)
{
    NSData *old = [NSData dataWithContentsOfURL:url];
    return [old isEqualToData:data] || [data writeToURL:url options:NSDataWritingAtomic error:e];
}

BOOL black_PrepareRussian(NSURL *game, NSURL *strings, NSError **e)
{
    NSFileManager *fm = NSFileManager.defaultManager;
    NSURL *fonts = [game URLByAppendingPathComponent:@"language/fonts"], *banks = [game URLByAppendingPathComponent:@"language/strings"];
    NSURL *english = [banks URLByAppendingPathComponent:@"MainEn.bin"], *us = [banks URLByAppendingPathComponent:@"MainUS.bin"];
    /* The English bank as installed, kept before MainUS.bin is ever replaced. */
    if (![fm fileExistsAtPath:english.path] && ![fm copyItemAtURL:us toURL:english error:e]) return NO;
    NSDictionary *strings_ = strings ? black_TranslationLoad(strings, e) : nil;
    if (!strings_) return strings ? NO : YES;
    for (NSString *name in retail()) {
        NSURL *path = [fonts URLByAppendingPathComponent:name];
        NSData *data = [NSData dataWithContentsOfURL:path options:0 error:e];
        if (!data) return NO;
        NSString *hash = sha256(data);
        NSArray *known = retail()[name];
        NSURL *kept = [fonts URLByAppendingPathComponent:[name.stringByDeletingPathExtension stringByAppendingString:@".retail.bin"]];
        if ([hash isEqual:known[0]]) {
            NSString *face = installed(known[1]) ? known[1] : known[2];
            NSData *patched = black_FontWithCyrillic(data, face, e);
            if (!patched) return NO;
            if (![fm fileExistsAtPath:kept.path] && ![data writeToURL:kept options:NSDataWritingAtomic error:e]) return NO;
            if (![patched writeToURL:path options:NSDataWritingAtomic error:e]) return NO;
            fprintf(stderr, "[LANG] %s: Cyrillic added from %s\n", name.UTF8String, face.UTF8String);
        } else if (![fm fileExistsAtPath:kept.path])
            return fail(e, [NSString stringWithFormat:@"%@ is not the retail game font; Russian needs it.", name]);
    }
    NSData *bank = black_TranslatedBank([NSData dataWithContentsOfURL:english], strings_, e);
    return bank && write_if_changed(bank, [banks URLByAppendingPathComponent:@"MainRu.bin"], e);
}

BOOL black_ApplyGameText(NSURL *game, BOOL russian, NSError **e)
{
    NSURL *banks = [game URLByAppendingPathComponent:@"language/strings"];
    NSData *bank = [NSData dataWithContentsOfURL:[banks URLByAppendingPathComponent:russian ? @"MainRu.bin" : @"MainEn.bin"]];
    if (!bank) return russian ? fail(e, @"The Russian string bank is not prepared.") : YES;
    return write_if_changed(bank, [banks URLByAppendingPathComponent:@"MainUS.bin"], e);
}
