/* Russian text on synthetic fonts and banks: Cyrillic records added, records
 * sorted with rebuilt buckets, Latin records unchanged, idempotent; bank IDs
 * kept with null entries left English; MainUS.bin switched both ways. */
#import "../../ports/macos/overrides/game_translation.h"
#include <stdio.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint16_t rd16(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return v; }
static int fail(int code, const char *what) { fprintf(stderr, "FAIL %d: %s\n", code, what); return code; }

/* A capitals font: 256x256 DXT3 atlas, Latin glyphs in a row at the top,
 * records sorted by (code & 127, code) with buckets, the default glyph after. */
static NSData *font(void)
{
    NSString *letters = @" ABCEHKMNOPTUXË";
    NSMutableArray *codes = [NSMutableArray array];
    for (NSUInteger i = 0; i < letters.length; ++i) {
        unichar c = [letters characterAtIndex:i];
        [codes addObject:@(c)];
        if (c != ' ') [codes addObject:@([[[NSString stringWithCharacters:&c length:1] lowercaseString] characterAtIndex:0])];
    }
    [codes sortUsingComparator:^NSComparisonResult(NSNumber *a, NSNumber *b) {
        int x = a.intValue, y = b.intValue, kx = ((x & 127) << 16) | x, ky = ((y & 127) << 16) | y;
        return kx < ky ? NSOrderedAscending : kx > ky ? NSOrderedDescending : NSOrderedSame;
    }];
    uint32_t head = 0x220 + 40 * (uint32_t)codes.count, tex = (head + 2 * 40 + 0x7F) & ~0x7Fu;
    NSMutableData *d = [NSMutableData dataWithLength:tex + 128 + 256 * 256];
    uint8_t *b = d.mutableBytes;
    memcpy(b, "4.1v", 4); memcpy(b + 4, &tex, 4); memcpy(b + 0x1C, &head, 4);
    for (int k = 0; k < 128; ++k) memcpy(b + 0x20 + 4 * k, &head, 4);
    for (NSUInteger i = 0; i < codes.count; ++i) {
        uint8_t *r = b + 0x220 + 40 * i; uint16_t c = (uint16_t)[codes[i] intValue];
        unichar up = [[[NSString stringWithCharacters:&c length:1] uppercaseString] characterAtIndex:0];
        NSUInteger slot = [letters rangeOfString:[NSString stringWithCharacters:&up length:1]].location;
        float f[7] = {c == ' ' ? -1 : slot * 16 / 256.f, 0, c == ' ' ? .03f : 14 / 256.f, c == ' ' ? 0 : 20 / 256.f, 0, 0, 15 / 256.f};
        memcpy(r, f, 28); memcpy(r + 28, &c, 2);
        uint32_t at = 0x220 + 40 * (uint32_t)i;
        if (rd32(b + 0x20 + 4 * (c & 127)) == head) memcpy(b + 0x20 + 4 * (c & 127), &at, 4);
    }
    uint32_t format = 0x0E << 8 | 8 << 20 | 8 << 24;
    memcpy(b + tex, "\1\0\4\0\x80\0\0\0", 8); memcpy(b + tex + 12, &format, 4);
    return d;
}

int main(void)
{
    @autoreleasepool {
        NSError *e = nil;
        NSData *before = font(), *after = black_FontWithCyrillic(before, @"Helvetica-Bold", &e);
        if (!after) return fail(1, e.localizedDescription.UTF8String);
        const uint8_t *a = after.bytes, *o = before.bytes;
        uint32_t head = rd32(a + 0x1C), tex = rd32(a + 4), old_head = rd32(o + 0x1C), old_tex = rd32(o + 4);
        if ((head - 0x220) / 40 != (old_head - 0x220) / 40 + 66) return fail(2, "66 records added");
        if (tex % 0x80 || after.length - tex != before.length - old_tex) return fail(3, "texture aligned and whole");
        for (uint32_t at = 0x220; at + 40 < head; at += 40) {
            uint16_t x = rd16(a + at + 28), y = rd16(a + at + 68);
            if (((x & 127) << 16 | x) >= ((y & 127) << 16 | y)) return fail(4, "records sorted by (code & 127, code)");
        }
        for (int k = 0; k < 128; ++k) {
            uint32_t first = head;
            for (uint32_t at = 0x220; at < head; at += 40) if ((rd16(a + at + 28) & 127) == k) { first = at; break; }
            if (rd32(a + 0x20 + 4 * k) != first) return fail(5, "bucket points at its first record");
        }
        int cyrillic = 0, latin_same = 0;
        for (uint32_t at = 0x220; at < head; at += 40) {
            uint16_t c = rd16(a + at + 28);
            if ((c >= 0x410 && c <= 0x44F) || c == 0x401 || c == 0x451) ++cyrillic;
            for (uint32_t bt = 0x220; bt < old_head; bt += 40)
                if (rd16(o + bt + 28) == c) { if (memcmp(o + bt, a + at, 40)) return fail(6, "Latin record unchanged"); ++latin_same; }
        }
        if (cyrillic != 66 || latin_same != (int)(old_head - 0x220) / 40) return fail(7, "every code present once");
        if (black_FontWithCyrillic(after, @"Helvetica-Bold", &e) != after) return fail(8, "a font with Cyrillic is returned as is");

        /* Bank: three strings, one translated, one null, one missing. */
        NSMutableData *bank = [NSMutableData dataWithLength:16 + 12];
        uint32_t three = 3, sixteen = 16; memcpy((uint8_t *)bank.mutableBytes + 8, &three, 4); memcpy((uint8_t *)bank.mutableBytes + 12, &sixteen, 4);
        const char *texts[3] = {"press start", "Alex Ward", "%d of %d"};
        for (uint32_t i = 0; i < 3; ++i) {
            uint32_t at = (uint32_t)bank.length, id = 0xA0 + i; memcpy((uint8_t *)bank.mutableBytes + 16 + 4 * i, &at, 4);
            [bank appendBytes:&id length:4]; [bank appendBytes:texts[i] length:strlen(texts[i]) + 1];
            if (i < 2) [bank increaseLengthBy:(4 - bank.length % 4) % 4];
        }
        NSData *ru = black_TranslatedBank(bank, @{@"000000A0": @"НАЖМИТЕ СТАРТ", @"000000A1": NSNull.null}, &e);
        if (!ru) return fail(9, e.localizedDescription.UTF8String);
        const uint8_t *r = ru.bytes;
        if (rd32(r + 8) != 3 || strcmp((const char *)r + rd32(r + 16) + 4, "НАЖМИТЕ СТАРТ") ||
            strcmp((const char *)r + rd32(r + 20) + 4, "Alex Ward") || strcmp((const char *)r + rd32(r + 24) + 4, "%d of %d") ||
            rd32(r + rd32(r + 24)) != 0xA2 || rd32(r + 20) % 4 || rd32(r + 24) % 4) return fail(10, "bank IDs, order, alignment and null entries");
        if (black_TranslatedBank(bank, @{@"000000FF": @"X"}, &e)) return fail(11, "a translation for another game is refused");

        /* MainUS.bin is switched to Russian and back. */
        NSURL *game = [NSURL fileURLWithPath:[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString]];
        NSURL *strings = [game URLByAppendingPathComponent:@"language/strings"];
        [NSFileManager.defaultManager createDirectoryAtURL:strings withIntermediateDirectories:YES attributes:nil error:nil];
        [bank writeToURL:[strings URLByAppendingPathComponent:@"MainEn.bin"] atomically:YES];
        [ru writeToURL:[strings URLByAppendingPathComponent:@"MainRu.bin"] atomically:YES];
        [bank writeToURL:[strings URLByAppendingPathComponent:@"MainUS.bin"] atomically:YES];
        NSURL *us = [strings URLByAppendingPathComponent:@"MainUS.bin"];
        if (!black_ApplyGameText(game, YES, &e) || ![[NSData dataWithContentsOfURL:us] isEqual:ru]) return fail(12, "MainUS.bin made Russian");
        if (!black_ApplyGameText(game, NO, &e) || ![[NSData dataWithContentsOfURL:us] isEqual:bank]) return fail(13, "MainUS.bin made English again");
        [NSFileManager.defaultManager removeItemAtURL:game error:nil];
        puts("Russian text: Cyrillic font records, sorted buckets, Latin kept, idempotent; bank IDs and nulls; MainUS switch: PASS");
    }
    return 0;
}
