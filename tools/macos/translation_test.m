extern char *black_InputGameView(const char *);
extern void black_InputGameViewRemove(const char *);
/* Russian text on synthetic fonts and banks: Cyrillic records added, records
 * sorted with rebuilt buckets, Latin records unchanged, idempotent; bank IDs
 * kept with null entries left English; MainUS.bin switched both ways. */
#import "../../ports/macos/overrides/game_translation.h"
#import <Cocoa/Cocoa.h>
#import "../../ports/macos/overrides/input_glyphs.h"
#include "../../ports/macos/overrides/input_atlas.h"
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

int main(int argc,char **argv)
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

        NSData *symbols = black_FontWithInputGlyphs(before,&e);
        if (!symbols) return fail(14,e.localizedDescription.UTF8String);
        const uint8_t *sy=symbols.bytes;
        unsigned added=0;
        for(unsigned code=0xE100;code<0xE307;++code)if(black_InputGlyphValid(code))++added;
        if (rd32(sy+0x1C)!=old_head+added*40 || black_FontWithInputGlyphs(symbols,&e)!=symbols)
            return fail(15,"complete input artwork, idempotent");
        NSData *combined = black_FontWithInputGlyphs(after,&e);
        if (!combined || rd32((const uint8_t *)combined.bytes+0x1C)!=head+added*40)
            return fail(16,"artwork coexists with Cyrillic");
        for (uint32_t at=0x220;at<old_head;at+=40) {
            int found=0;
            for(uint32_t bt=0x220;bt<rd32(sy+0x1C);bt+=40) if(rd16(sy+bt+28)==rd16(o+at+28)) {
                BlackInputAtlas image;
                if(!black_InputAtlasFind(sy+rd32(sy+4)+128,256*256,256,256,14,&image))return fail(26,"host atlas registered");
                unsigned newH=image.height;
                for(int k=0;k<7;++k){float old,new;memcpy(&old,o+at+k*4,4);memcpy(&new,sy+bt+k*4,4);
                    if(k==1||k==3||k==5)new*=newH/256.f;
                    if(fabsf(old-new)>0.000001)return fail(17,"original glyph pixel metrics kept");
                }
                if(memcmp(sy+bt+28,o+at+28,12))return fail(17,"original glyph flags kept"); ++found;
            }
            if(found!=1) return fail(18,"original glyph appears once");
        }

        if(symbols.length-rd32(sy+4)!=before.length-old_tex)return fail(27,"guest texture allocation unchanged");
        if(memcmp(sy+rd32(sy+4)+128,o+old_tex+128,256*256))return fail(22,"original DXT blocks preserved");
        BlackInputAtlas host;
        if(!black_InputAtlasFind(sy+rd32(sy+4)+128,256*256,256,256,14,&host))return fail(26,"host atlas lookup");
        if(black_InputAtlasFind(sy+rd32(sy+4)+128,256*256,256,256,15,&host) ||
           black_InputAtlasFind(sy+rd32(sy+4)+128,16,256,256,14,&host))return fail(29,"unrelated format/short texture refused");
        float oldScale,newScale;memcpy(&oldScale,o+12,4);memcpy(&newScale,sy+12,4);
        if(fabsf(oldScale*host.height/256-newScale)>.00001)return fail(30,"font geometry scale tracks host height");
        for(uint32_t at=0x220;at<rd32(sy+0x1C);at+=40){
            unsigned code=rd16(sy+at+28);if(!black_InputGlyphValid(code))continue;
            uint32_t first=rd32(sy+0x20+(code&127)*4);if(first>at)return fail(23,"artwork bucket lookup");
            float u,v,w,h;memcpy(&u,sy+at,4);memcpy(&v,sy+at+4,4);memcpy(&w,sy+at+8,4);memcpy(&h,sy+at+12,4);
            if(u<0||v<0||w<=0||h<=0||u+w>1||v+h>1)return fail(24,"artwork UV bounds");
        }
        if(argc==3){
            NSString *folder=@(argv[2]);[NSFileManager.defaultManager createDirectoryAtPath:folder withIntermediateDirectories:YES attributes:nil error:nil];
            for(NSString *name in @[@"Big.bin",@"Small.bin"]){
                NSData *data=[NSData dataWithContentsOfFile:[@(argv[1]) stringByAppendingPathComponent:name]];
                NSData *patched=black_InputGlyphFont(data,&e);if(!patched)return fail(25,e.description.UTF8String);
                [patched writeToFile:[folder stringByAppendingPathComponent:name] atomically:YES];
                const uint8_t *pb=patched.bytes;unsigned pt=rd32(pb+4),fmt=rd32(pb+pt+12),w=1u<<((fmt>>20)&15),h=1u<<((fmt>>24)&15);BlackInputAtlas atlas;
                if(!black_InputAtlasFind(pb+pt+128,w*h,w,h,14,&atlas))return fail(28,"retail host atlas registered");
                unsigned char *planes[]={(unsigned char *)atlas.rgba};
                NSBitmapImageRep *bitmap=[[[NSBitmapImageRep alloc] initWithBitmapDataPlanes:planes pixelsWide:atlas.width pixelsHigh:atlas.height bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:atlas.width*4 bitsPerPixel:32] autorelease];
                [[bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:[folder stringByAppendingPathComponent:[name stringByAppendingString:@".png"]] atomically:YES];
            }
            /* A contact sheet from the production renderer, for visual review. */
            NSBitmapImageRep *rep=[[[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL pixelsWide:960 pixelsHigh:440 bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:0 bitsPerPixel:0] autorelease];
            [NSGraphicsContext saveGraphicsState];[NSGraphicsContext setCurrentContext:[NSGraphicsContext graphicsContextWithBitmapImageRep:rep]];
            [[NSColor colorWithCalibratedRed:.043 green:.075 blue:.063 alpha:1]setFill];NSRectFill(NSMakeRect(0,0,960,440));
            for(unsigned family=0;family<2;++family)for(unsigned button=0;button<16;++button){
                CGImageRef image=black_InputGlyphCreate(0xE100+family*16+button,48);
                CGContextDrawImage(NSGraphicsContext.currentContext.CGContext,CGRectMake(20+(button%8)*115,370-family*120-(button/8)*55,CGImageGetWidth(image),48),image);CGImageRelease(image);
            }
            unsigned keys[]={36,53,49,13,0,1,2,15,256,257,258,261,262,48,56,123};
            for(unsigned i=0;i<16;++i){CGImageRef image=black_InputGlyphCreate(0xE200+keys[i],40);
                CGContextDrawImage(NSGraphicsContext.currentContext.CGContext,CGRectMake(20+(i%8)*115,100-(i/8)*55,CGImageGetWidth(image),40),image);CGImageRelease(image);}
            [NSGraphicsContext restoreGraphicsState];
            [[rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:[folder stringByAppendingPathComponent:@"input-artwork.png"] atomically:YES];
        }

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
        NSURL *originals=[game URLByAppendingPathComponent:@"original-fonts"], *fonts=[game URLByAppendingPathComponent:@"language/fonts"];
        NSFileManager *fm=NSFileManager.defaultManager;
        [fm createDirectoryAtURL:originals withIntermediateDirectories:YES attributes:nil error:nil];
        for(NSString *name in @[@"Big.bin",@"Small.bin"]) [before writeToURL:[originals URLByAppendingPathComponent:name] atomically:YES];
        [fm createSymbolicLinkAtURL:fonts withDestinationURL:originals error:nil];
        char *view=black_InputGameView(game.path.fileSystemRepresentation);
        if(!view) return fail(19,"private font view through directory symlink");
        NSURL *fontView=[NSURL fileURLWithPath:@(view)];
        for(NSString *name in @[@"Big.bin",@"Small.bin"]) {
            if(![[NSData dataWithContentsOfURL:[originals URLByAppendingPathComponent:name]] isEqual:before]) return fail(20,"source font unchanged");
            NSURL *patched=[fontView URLByAppendingPathComponent:[@"language/fonts" stringByAppendingPathComponent:name]];
            if(![[NSData dataWithContentsOfURL:patched] isEqual:symbols]) return fail(21,"private font has exactly the new symbols");
        }
        black_InputGameViewRemove(view); free(view);
        [NSFileManager.defaultManager removeItemAtURL:game error:nil];
        puts("Russian text: Cyrillic font records, sorted buckets, Latin kept, idempotent; bank IDs and nulls; MainUS switch: PASS");
    }
    return 0;
}
