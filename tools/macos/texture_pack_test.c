/* Texture replacement: the map's key, map lines, the shipped map, DDS/PNG
 * decoding and finding pack files by name under any folder layout. */
#include "../../ports/macos/overrides/texture_pack.h"
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void put32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

/* A DDS header; `fourcc` 0 makes an uncompressed 32-bit one with these masks. */
static size_t dds(uint8_t *out, unsigned w, unsigned h, unsigned mips, uint32_t fourcc,
                  uint32_t r, uint32_t b, uint32_t caps2, uint32_t dxgi)
{
    memset(out, 0, 148);
    memcpy(out, "DDS ", 4);
    put32(out + 4, 124); put32(out + 12, h); put32(out + 16, w); put32(out + 28, mips);
    put32(out + 76, 32); put32(out + 112, caps2);
    if (fourcc) { put32(out + 80, 0x4); put32(out + 84, fourcc); }
    else { put32(out + 80, 0x41); put32(out + 88, 32); put32(out + 92, r); put32(out + 96, 0xff00);
           put32(out + 100, b); put32(out + 104, 0xff000000u); }
    if (fourcc == 0x30315844u) { put32(out + 128, dxgi); put32(out + 132, 3); put32(out + 140, 1); return 148; }
    return 128;
}

static int write_file(const char *path, const void *data, size_t size)
{
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    int ok = fwrite(data, 1, size, f) == size;
    return fclose(f) == 0 && ok;
}

/* A 2x1 RGBA PNG through ImageIO: opaque red, then green at alpha 64. */
static int write_png(const char *path)
{
    static const uint8_t px[8] = {255, 0, 0, 255, 0, 200, 0, 64};
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CFDataRef data = CFDataCreate(NULL, px, sizeof px);
    CGDataProviderRef provider = CGDataProviderCreateWithCFData(data);
    CGImageRef image = CGImageCreate(2, 1, 8, 32, 8, space, kCGImageAlphaLast | kCGBitmapByteOrderDefault,
                                     provider, NULL, false, kCGRenderingIntentDefault);
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(NULL, (const UInt8 *)path, (CFIndex)strlen(path), false);
    CGImageDestinationRef dest = CGImageDestinationCreateWithURL(url, CFSTR("public.png"), 1, NULL);
    int ok = dest && image;
    if (ok) { CGImageDestinationAddImage(dest, image, NULL); ok = CGImageDestinationFinalize(dest); }
    if (dest) CFRelease(dest);
    CFRelease(url); CGImageRelease(image); CGDataProviderRelease(provider); CFRelease(data); CGColorSpaceRelease(space);
    return ok;
}

int main(void)
{
    /* Top level sizes: palette 1 byte a texel, DXT1 8 and DXT3/5 16 a block. */
    if (black_TexturePackTopBytes(0x0b, 128, 128) != 16384 || black_TexturePackTopBytes(0x0c, 128, 64) != 4096 ||
        black_TexturePackTopBytes(0x0e, 256, 256) != 65536 || black_TexturePackTopBytes(0x0f, 32, 32) != 1024)
        return 1;
    if (black_TexturePackTopBytes(0x06, 128, 128) || black_TexturePackTopBytes(0x0c, 6, 8) ||
        black_TexturePackTopBytes(0x0c, 0, 64) || black_TexturePackTopBytes(0x0b, 4, 2))
        return 2;

    /* FNV-1a over little-endian 64-bit words (portingpete kelvin_gpu.c), not bytes. */
    uint8_t ramp[32];
    for (int i = 0; i < 32; ++i) ramp[i] = (uint8_t)i;
    if (black_TexturePackKey(ramp, 32) != 0x052dc82d2f436945ull || black_TexturePackKey(ramp, 0) != 0xcbf29ce484222325ull)
        return 3;
    uint8_t zero[8] = {0};
    if (black_TexturePackKey(zero, 8) != 0xaf63bd4c8601b7dfull) return 4;

    BlackTextureMapEntry e;
    if (!black_TexturePackParseMapLine("0052f8ad3cae1e40 128 128 0c 246583a16e3f3998-7c7ae0eee0947394-00001dd4.dds\n", &e) ||
        e.key != 0x0052f8ad3cae1e40ull || e.width != 128 || e.height != 128 || e.format != 0x0c ||
        strcmp(e.file, "246583a16e3f3998-7c7ae0eee0947394-00001dd4.dds"))
        return 5;
    if (black_TexturePackParseMapLine("# comment\n", &e) || black_TexturePackParseMapLine("\n", &e) ||
        black_TexturePackParseMapLine("zz 128 128 0c a.dds\n", &e) ||
        black_TexturePackParseMapLine("01 128 128 0c a.dds extra\n", &e) ||
        black_TexturePackParseMapLine("01 128 0c a.dds\n", &e))
        return 6;

    /* The shipped map: 1161 textures in four formats, 1007 distinct files. */
    FILE *map = fopen(BLACK_TEXTURE_MAP_FILE, "r");
    if (!map) return 7;
    char line[512], (*names)[256] = calloc(2000, 256);
    unsigned entries = 0, distinct = 0;
    while (fgets(line, sizeof line, map)) {
        if (!black_TexturePackParseMapLine(line, &e)) continue;
        if (e.format != 0x0b && e.format != 0x0c && e.format != 0x0e && e.format != 0x0f) return 8;
        if (!black_TexturePackTopBytes(e.format, e.width, e.height)) return 9;
        unsigned i = 0;
        while (i < distinct && strcmp(names[i], e.file)) ++i;
        if (i == distinct && distinct < 2000) strcpy(names[distinct++], e.file);
        ++entries;
    }
    fclose(map); free(names);
    if (entries != 1161 || distinct != 1007) { fprintf(stderr, "map %u/%u\n", entries, distinct); return 10; }

    /* DDS: DXT5 with a truncated mip chain keeps the levels that are there. */
    uint8_t buf[4096];
    BlackTextureImage image;
    size_t at = dds(buf, 8, 8, 4, 0x35545844u, 0, 0, 0, 0);
    memset(buf + at, 0xab, 64 + 16 + 8);
    if (!black_TextureDecodeDds(buf, at + 64 + 16, &image) || image.format != BLACK_TEX_BC3 ||
        image.levels != 2 || image.level_size[0] != 64 || image.level_size[1] != 16 || image.level[1][0] != 0xab)
        return 11;
    black_TextureImageFree(&image);
    at = dds(buf, 4, 4, 1, 0x30315844u, 0, 0, 0, 98);
    memset(buf + at, 1, 16);
    if (!black_TextureDecodeDds(buf, at + 16, &image) || image.format != BLACK_TEX_BC7 || image.levels != 1) return 12;
    black_TextureImageFree(&image);
    at = dds(buf, 2, 1, 1, 0, 0xff0000u, 0xffu, 0, 0); /* B8G8R8A8 */
    memcpy(buf + at, (uint8_t[]){1, 2, 3, 4, 5, 6, 7, 8}, 8);
    if (!black_TextureDecodeDds(buf, at + 8, &image) || image.format != BLACK_TEX_RGBA8 ||
        memcmp(image.level[0], (uint8_t[]){3, 2, 1, 4, 7, 6, 5, 8}, 8))
        return 13;
    black_TextureImageFree(&image);
    at = dds(buf, 8, 8, 1, 0x31545844u, 0, 0, 0x200, 0); /* cube map */
    if (black_TextureDecodeDds(buf, at + 384, &image) || black_TextureDecodeDds(buf, 100, &image)) return 14;
    at = dds(buf, 8, 8, 1, 0x31545844u, 0, 0, 0, 0);
    if (black_TextureDecodeDds(buf, at + 31, &image)) return 15; /* level 0 needs 32 bytes */

    /* The game's top level: its alpha range; opaque only when no texel can be see-through. */
    unsigned lo, hi;
    uint8_t dxt1[8] = {0x00, 0xf8, 0x1f, 0x00, 0, 0, 0, 0}; /* c0 > c1: four colours */
    if (!black_TextureTopAlpha(0x0c, 4, 4, dxt1, NULL, 0, &lo, &hi) || lo != 255 || hi != 255) return 40;
    memcpy(dxt1, (uint8_t[]){0x1f, 0x00, 0x00, 0xf8, 0x55, 0x55, 0x55, 0x55}, 8); /* 3 colours, index 1 */
    if (!black_TextureTopAlpha(0x0c, 4, 4, dxt1, NULL, 0, &lo, &hi) || lo != 255) return 41;
    dxt1[7] = 0xc0; /* one texel at index 3: transparent */
    if (!black_TextureTopAlpha(0x0c, 4, 4, dxt1, NULL, 0, &lo, &hi) || lo != 0 || hi != 255) return 42;
    uint8_t dxt3[16];
    memset(dxt3, 0xff, 16);
    if (!black_TextureTopAlpha(0x0e, 4, 4, dxt3, NULL, 0, &lo, &hi) || lo != 255) return 43;
    memset(dxt3, 0x88, 8); /* every texel 8 * 17 */
    if (!black_TextureTopAlpha(0x0e, 4, 4, dxt3, NULL, 0, &lo, &hi) || lo != 136 || hi != 136) return 44;
    uint8_t dxt5[16] = {255, 255};
    if (!black_TextureTopAlpha(0x0f, 4, 4, dxt5, NULL, 0, &lo, &hi) || lo != 255) return 45;
    dxt5[1] = 0; dxt5[2] = 1; /* first texel takes a1 = 0 */
    if (!black_TextureTopAlpha(0x0f, 4, 4, dxt5, NULL, 0, &lo, &hi) || lo != 0 || hi != 255) return 46;
    uint8_t texels[16] = {0}, palette[8] = {1, 2, 3, 0xff, 4, 5, 6, 0x00};
    if (!black_TextureTopAlpha(0x0b, 4, 4, texels, palette, 8, &lo, &hi) || lo != 255 ||
        black_TextureTopAlpha(0x0b, 4, 4, texels, NULL, 0, &lo, &hi))
        return 47;
    texels[9] = 1;
    if (!black_TextureTopAlpha(0x0b, 4, 4, texels, palette, 8, &lo, &hi) || lo != 0 || hi != 255) return 48;
    texels[9] = 2; /* past the palette: unknown */
    if (black_TextureTopAlpha(0x0b, 4, 4, texels, palette, 8, &lo, &hi)) return 39;

    /* The pack's alpha is scaled to the game's highest: PS2 128 and 157 reach 255. */
    at = dds(buf, 4, 4, 1, 0x35545844u, 0, 0, 0, 0);
    memset(buf + at, 0, 16);
    buf[at] = 128; /* every texel index 0: a0 = 128 */
    if (!black_TextureDecodeDds(buf, at + 16, &image) || black_TextureAlphaMax(&image) != 128) return 49;
    black_TextureScaleAlpha(&image, 128, 255);
    if (image.level[0][0] != 255 || image.level[0][1] != 0) return 49;
    black_TextureImageFree(&image);
    /* every texel index 1 (a1 = 156); both ends opaque once scaled: indices go to the first */
    memcpy(buf + at, (uint8_t[]){157, 156, 0x49, 0x92, 0x24, 0x49, 0x92, 0x24}, 8);
    if (!black_TextureDecodeDds(buf, at + 16, &image) || black_TextureAlphaMax(&image) != 156) return 50;
    black_TextureScaleAlpha(&image, 156, 255);
    if (image.level[0][0] != 255 || image.level[0][1] != 255 || image.level[0][2] || image.level[0][7]) return 50;
    black_TextureImageFree(&image);
    at = dds(buf, 2, 1, 1, 0, 0xffu, 0xff0000u, 0, 0);
    memcpy(buf + at, (uint8_t[]){9, 9, 9, 100, 9, 9, 9, 64}, 8);
    if (!black_TextureDecodeDds(buf, at + 8, &image) || black_TextureAlphaMax(&image) != 100) return 52;
    black_TextureScaleAlpha(&image, 100, 200);
    if (image.level[0][3] != 200 || image.level[0][7] != 128 || image.level[0][0] != 9) return 52;
    black_TextureScaleAlpha(&image, 100, 255);
    if (image.level[0][3] != 255) return 53; /* clamped */
    black_TextureImageFree(&image);
    at = dds(buf, 4, 4, 1, 0x32545844u, 0, 0, 0, 0); /* DXT3 */
    memset(buf + at, 0x88, 8); memset(buf + at + 8, 0, 8);
    if (!black_TextureDecodeDds(buf, at + 16, &image) || black_TextureAlphaMax(&image) != 136) return 54;
    black_TextureScaleAlpha(&image, 136, 255);
    if (image.level[0][0] != 0xff) return 54;
    black_TextureImageFree(&image);
    at = dds(buf, 4, 4, 1, 0x31545844u, 0, 0, 0, 0);
    if (!black_TextureDecodeDds(buf, at + 8, &image) || black_TextureAlphaMax(&image) != 255) return 55;
    black_TextureImageFree(&image);

    /* Size limit: block formats drop top levels, RGBA8 is halved, one level stays. */
    at = dds(buf, 16, 16, 5, 0x35545844u, 0, 0, 0, 0); /* 16, 8, 4, 2, 1: 256+64+16+16+16 */
    for (int i = 0; i < 368; ++i) buf[at + i] = (uint8_t)(i < 256 ? 1 : i < 320 ? 2 : 3);
    if (!black_TextureDecodeDds(buf, at + 368, &image) || image.levels != 5 || black_TextureLimitSize(&image, 16) ||
        !black_TextureLimitSize(&image, 8) || image.levels != 4 || image.width != 8 || image.height != 8 ||
        image.level[0] != image.storage || image.level_size[0] != 64 || image.level[0][0] != 2 || image.level[1][0] != 3)
        return 56;
    black_TextureImageFree(&image);
    at = dds(buf, 8, 8, 1, 0x35545844u, 0, 0, 0, 0);
    memset(buf + at, 4, 64);
    if (!black_TextureDecodeDds(buf, at + 64, &image) || black_TextureLimitSize(&image, 4) || image.width != 8) return 57;
    black_TextureImageFree(&image);
    at = dds(buf, 4, 2, 1, 0, 0xffu, 0xff0000u, 0, 0);
    for (int i = 0; i < 32; ++i) buf[at + i] = (uint8_t)(i < 16 ? (i % 8 < 4 ? 10 : 30) : (i % 8 < 4 ? 50 : 70));
    if (!black_TextureDecodeDds(buf, at + 32, &image) || !black_TextureLimitSize(&image, 2) || image.width != 2 ||
        image.height != 1 || image.levels != 1 || image.level[0][0] != 40 || image.level[0][4] != 40)
        return 58;
    black_TextureImageFree(&image);

    /* A pack in PCSX2's layout, names in other case, macOS ._ files and a link. */
    char root[] = "/tmp/black-texture-pack.XXXXXX", path[1024];
    if (!mkdtemp(root)) return 16;
    snprintf(path, sizeof path, "%s/pack", root); mkdir(path, 0755);
    snprintf(path, sizeof path, "%s/pack/SLUS-21376", root); mkdir(path, 0755);
    snprintf(path, sizeof path, "%s/pack/SLUS-21376/replacements", root); mkdir(path, 0755);
    snprintf(path, sizeof path, "%s/replacement", root); mkdir(path, 0755);
    snprintf(path, sizeof path, "%s/replacement/hd", root);
    char target[1024]; snprintf(target, sizeof target, "%s/pack", root);
    if (symlink(target, path)) return 17;
    at = dds(buf, 8, 8, 2, 0x31545844u, 0, 0, 0, 0);
    memset(buf + at, 7, 32 + 8);
    snprintf(path, sizeof path, "%s/pack/SLUS-21376/replacements/aaaa-bbbb-00001dd4.dds", root);
    if (!write_file(path, buf, at + 40)) return 18;
    snprintf(path, sizeof path, "%s/pack/SLUS-21376/replacements/._aaaa-bbbb-00001dd4.dds", root);
    if (!write_file(path, "junk", 4)) return 18;
    snprintf(path, sizeof path, "%s/pack/SLUS-21376/replacements/CCCC-DDDD-00006213.PNG", root);
    if (!write_png(path)) return 19;
    snprintf(path, sizeof path, "%s/pack/notes.txt", root);
    if (!write_file(path, "x", 1)) return 18;
    char map_path[1024];
    snprintf(map_path, sizeof map_path, "%s/test.map", root);
    const char *lines = "# test\n"
        "00000000000000aa 128 128 0c aaaa-bbbb-00001dd4.dds\n"
        "00000000000000bb 256 256 0b cccc-dddd-00006213.png\n"
        "00000000000000cc 64 64 0f not-in-this-pack.dds\n";
    if (!write_file(map_path, lines, strlen(lines))) return 18;
    char extra_path[1024];
    snprintf(extra_path, sizeof extra_path, "%s/black-hd-reimagined-extra.map", root);
    const char *extra = "00000000000000dd 128 128 0c aaaa-bbbb-00001dd4.dds\n"
                        "00000000000000aa 128 128 0c cccc-dddd-00006213.png\n";
    if (!write_file(extra_path, extra, strlen(extra))) return 18;
    snprintf(path, sizeof path, "%s/replacement", root);
    BlackTexturePack *pack = black_TexturePackOpen(path, map_path);
    if (!pack || black_TexturePackFiles(pack) != 2 || black_TexturePackMapped(pack) != 3 ||
        black_TexturePackMissing(pack) != 1)
        return 20;
    int dxt = black_TexturePackFind(pack, 0xaa, 128, 128, 0x0c), png = black_TexturePackFind(pack, 0xbb, 256, 256, 0x0b);
    if (dxt < 0 || png < 0 || !strstr(black_TexturePackPath(pack, dxt), "/hd/SLUS-21376/replacements/aaaa") ||
        !strstr(black_TexturePackPath(pack, png), "CCCC-DDDD"))
        return 21;
    if (black_TexturePackFind(pack, 0xdd, 128, 128, 0x0c) != dxt) return 27;
    /* Size and format are part of the lookup; a missing file finds nothing. */
    if (black_TexturePackFind(pack, 0xaa, 64, 128, 0x0c) >= 0 || black_TexturePackFind(pack, 0xaa, 128, 128, 0x0f) >= 0 ||
        black_TexturePackFind(pack, 0xcc, 64, 64, 0x0f) >= 0 || black_TexturePackFind(pack, 0xab, 128, 128, 0x0c) >= 0)
        return 22;
    if (!black_TextureLoadFile(black_TexturePackPath(pack, dxt), &image) || image.format != BLACK_TEX_BC1 ||
        image.levels != 2 || image.level_size[0] != 32 || image.level_size[1] != 8)
        return 23;
    black_TextureImageFree(&image);
    /* PNG alpha stays unpremultiplied: green 200 at alpha 64 comes back as 200. */
    if (!black_TextureLoadFile(black_TexturePackPath(pack, png), &image) || image.format != BLACK_TEX_RGBA8 ||
        image.width != 2 || image.height != 1 ||
        memcmp(image.level[0], (uint8_t[]){255, 0, 0, 255, 0, 200, 0, 64}, 8)) {
        if (image.storage) fprintf(stderr, "png %u %u %u %u\n", image.level[0][4], image.level[0][5],
                                   image.level[0][6], image.level[0][7]);
        return 24;
    }
    black_TextureImageFree(&image);
    black_TexturePackClose(pack);
    snprintf(path, sizeof path, "%s/missing", root);
    if (black_TexturePackOpen(path, map_path)) return 25;
    char command[1200];
    snprintf(command, sizeof command, "rm -rf '%s'", root);
    if (system(command)) return 26;
    puts("texture pack: key, map, DDS/PNG and pack lookup PASS");
    return 0;
}
