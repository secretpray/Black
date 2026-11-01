/* HD texture replacement: the map, the pack's files, DDS/PNG decoding and the
 * loader threads (texture_pack.h). The renderer side is texture_pack_gl.inc. */
#include "texture_pack.h"
#include "port_menu.h"
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#include <errno.h>
#include <fts.h>
#include <limits.h>
#include <mach-o/dyld.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

/* The map's words are read from guest memory as stored: little-endian, like
 * the x86 hosts the map was made on. */
_Static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "the map's key reads little-endian words");

#define MAP_NAME "black-hd-reimagined.map"

size_t black_TexturePackTopBytes(unsigned format, unsigned width, unsigned height)
{
    size_t bytes;
    if (width < 4 || height < 4 || width > 4096 || height > 4096) return 0;
    switch (format) {
    case 0x0b: bytes = (size_t)width * height; break;
    case 0x0c: case 0x0e: case 0x0f:
        if (width % 4 || height % 4) return 0;
        bytes = (size_t)(width / 4) * (height / 4) * (format == 0x0c ? 8 : 16);
        break;
    default: return 0;
    }
    return bytes % 8 ? 0 : bytes;
}

/* portingpete's kelvin_gpu.c: FNV-1a with each 64-bit word as the unit. */
uint64_t black_TexturePackKey(const void *top, size_t bytes)
{
    const uint8_t *p = top;
    uint64_t hash = 0xcbf29ce484222325ull, word;
    for (size_t i = 0; i + 8 <= bytes; i += 8) {
        memcpy(&word, p + i, 8);
        hash = (hash ^ word) * 0x100000001b3ull;
    }
    return hash;
}

int black_TexturePackParseMapLine(const char *line, BlackTextureMapEntry *entry)
{
    unsigned long long key;
    char extra;
    while (*line == ' ' || *line == '\t') line++;
    if (*line == '#' || !*line) return 0;
    if (sscanf(line, "%llx %u %u %x %255s %c", &key, &entry->width, &entry->height, &entry->format,
               entry->file, &extra) != 5)
        return 0;
    entry->key = key;
    return entry->width && entry->height && entry->format <= 0xff;
}

/* ---- decoding ------------------------------------------------------------ */

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static size_t level_bytes(int format, unsigned width, unsigned height)
{
    if (format == BLACK_TEX_RGBA8) return (size_t)width * height * 4;
    return (size_t)((width + 3) / 4) * ((height + 3) / 4) * (format == BLACK_TEX_BC1 ? 8 : 16);
}

void black_TextureImageFree(BlackTextureImage *image)
{
    if (!image) return;
    free(image->storage);
    memset(image, 0, sizeof *image);
}

/* Lays out `levels` levels in one allocation; the caller fills them. */
static int image_alloc(BlackTextureImage *image, int format, unsigned width, unsigned height, unsigned levels)
{
    size_t total = 0, offset = 0;
    for (unsigned i = 0; i < levels; ++i) {
        unsigned w = width >> i ? width >> i : 1, h = height >> i ? height >> i : 1;
        total += level_bytes(format, w, h);
    }
    uint8_t *storage = malloc(total);
    if (!storage) return 0;
    image->format = format; image->width = width; image->height = height; image->levels = levels;
    image->storage = storage;
    for (unsigned i = 0; i < levels; ++i) {
        unsigned w = width >> i ? width >> i : 1, h = height >> i ? height >> i : 1;
        image->level[i] = storage + offset;
        image->level_size[i] = level_bytes(format, w, h);
        offset += image->level_size[i];
    }
    return 1;
}

int black_TextureDecodeDds(const uint8_t *data, size_t size, BlackTextureImage *image)
{
    memset(image, 0, sizeof *image);
    if (size < 128 || memcmp(data, "DDS ", 4) || le32(data + 4) != 124 || le32(data + 76) != 32) return 0;
    unsigned height = le32(data + 12), width = le32(data + 16), mips = le32(data + 28);
    uint32_t flags = le32(data + 80), fourcc = le32(data + 84), caps2 = le32(data + 112);
    size_t offset = 128;
    int format = 0, swap = 0, opaque = 0;
    if (caps2 & 0x200200u) return 0; /* cube map, volume */
    if (flags & 0x4u) {
        if (fourcc == 0x31545844u) format = BLACK_TEX_BC1;                               /* DXT1 */
        else if (fourcc == 0x32545844u || fourcc == 0x33545844u) format = BLACK_TEX_BC2; /* DXT2, DXT3 */
        else if (fourcc == 0x34545844u || fourcc == 0x35545844u) format = BLACK_TEX_BC3; /* DXT4, DXT5 */
        else if (fourcc == 0x30315844u) {                                                /* DX10 */
            if (size < 148 || le32(data + 132) != 3 || le32(data + 140) > 1) return 0;
            offset = 148;
            switch (le32(data + 128)) {
            case 70: case 71: case 72: format = BLACK_TEX_BC1; break;
            case 73: case 74: case 75: format = BLACK_TEX_BC2; break;
            case 76: case 77: case 78: format = BLACK_TEX_BC3; break;
            case 97: case 98: case 99: format = BLACK_TEX_BC7; break;
            case 28: case 29: format = BLACK_TEX_RGBA8; break;
            case 87: case 91: format = BLACK_TEX_RGBA8; swap = 1; break;
            default: return 0;
            }
        }
    } else if ((flags & 0x40u) && le32(data + 88) == 32) {
        uint32_t r = le32(data + 92), g = le32(data + 96), b = le32(data + 100);
        if (g != 0xff00u) return 0;
        if (r == 0xffu && b == 0xff0000u) swap = 0;
        else if (r == 0xff0000u && b == 0xffu) swap = 1;
        else return 0;
        format = BLACK_TEX_RGBA8;
        opaque = !(flags & 0x1u) || le32(data + 104) != 0xff000000u;
    }
    if (!format || !width || !height || width > 8192 || height > 8192) return 0;
    unsigned levels = mips ? mips : 1, fit = 0;
    if (levels > BLACK_TEX_MAX_LEVELS) levels = BLACK_TEX_MAX_LEVELS;
    size_t at = offset;
    for (; fit < levels; ++fit) {
        unsigned w = width >> fit ? width >> fit : 1, h = height >> fit ? height >> fit : 1;
        size_t need = level_bytes(format, w, h);
        if (need > size - at) break;
        at += need;
        if (w == 1 && h == 1) { ++fit; break; }
    }
    if (!fit || !image_alloc(image, format, width, height, fit)) return 0;
    memcpy(image->storage, data + offset, at - offset);
    if (format == BLACK_TEX_RGBA8 && (swap || opaque)) {
        uint8_t *p = image->storage;
        for (size_t i = 0; i < at - offset; i += 4) {
            if (swap) { uint8_t t = p[i]; p[i] = p[i + 2]; p[i + 2] = t; }
            if (opaque) p[i + 3] = 255;
        }
    }
    return 1;
}

/* ImageIO's own bytes when they are already 8-bit RGB(A) without
 * premultiplication, so no colour is changed on the way. */
static int png_raw(CGImageRef picture, uint8_t *out, size_t width, size_t height)
{
    CGBitmapInfo info = CGImageGetBitmapInfo(picture);
    CGImageAlphaInfo alpha = CGImageGetAlphaInfo(picture);
    CGBitmapInfo order = info & kCGBitmapByteOrderMask;
    size_t bpp = CGImageGetBitsPerPixel(picture), row = CGImageGetBytesPerRow(picture);
    CGColorSpaceRef space = CGImageGetColorSpace(picture);
    if (CGImageGetBitsPerComponent(picture) != 8 || (info & kCGBitmapFloatComponents) ||
        !space || CGColorSpaceGetModel(space) != kCGColorSpaceModelRGB ||
        (order != kCGBitmapByteOrderDefault && order != kCGBitmapByteOrder32Big))
        return 0;
    if (!((bpp == 32 && (alpha == kCGImageAlphaLast || alpha == kCGImageAlphaNoneSkipLast)) ||
          (bpp == 24 && alpha == kCGImageAlphaNone)))
        return 0;
    CFDataRef bytes = CGDataProviderCopyData(CGImageGetDataProvider(picture));
    if (!bytes) return 0;
    const uint8_t *src = CFDataGetBytePtr(bytes);
    int ok = (size_t)CFDataGetLength(bytes) >= row * (height - 1) + width * bpp / 8;
    for (size_t y = 0; ok && y < height; ++y) {
        const uint8_t *s = src + y * row;
        uint8_t *d = out + y * width * 4;
        for (size_t x = 0; x < width; ++x, s += bpp / 8, d += 4) {
            d[0] = s[0]; d[1] = s[1]; d[2] = s[2];
            d[3] = alpha == kCGImageAlphaLast ? s[3] : 255;
        }
    }
    CFRelease(bytes);
    return ok;
}

/* Anything else (grey, palette, 16-bit): drawn as premultiplied RGBA, then
 * divided back. */
static int png_drawn(CGImageRef picture, uint8_t *out, size_t width, size_t height)
{
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGContextRef context = space ? CGBitmapContextCreate(out, width, height, 8, width * 4, space,
        kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big) : NULL;
    if (space) CGColorSpaceRelease(space);
    if (!context) return 0;
    CGContextSetBlendMode(context, kCGBlendModeCopy);
    CGContextDrawImage(context, CGRectMake(0, 0, width, height), picture);
    CGContextRelease(context);
    for (size_t i = 0; i < width * height * 4; i += 4) {
        unsigned a = out[i + 3];
        if (a && a < 255)
            for (int c = 0; c < 3; ++c) {
                unsigned v = (out[i + c] * 255u + a / 2) / a;
                out[i + c] = v > 255 ? 255 : (uint8_t)v;
            }
    }
    return 1;
}

int black_TextureDecodePng(const uint8_t *data, size_t size, BlackTextureImage *image)
{
    memset(image, 0, sizeof *image);
    if (size < 8 || memcmp(data, "\x89PNG\r\n\x1a\n", 8)) return 0;
    CFDataRef bytes = CFDataCreateWithBytesNoCopy(NULL, data, (CFIndex)size, kCFAllocatorNull);
    CGImageSourceRef source = bytes ? CGImageSourceCreateWithData(bytes, NULL) : NULL;
    CGImageRef picture = source ? CGImageSourceCreateImageAtIndex(source, 0, NULL) : NULL;
    int ok = 0;
    if (picture) {
        size_t width = CGImageGetWidth(picture), height = CGImageGetHeight(picture);
        if (width && height && width <= 8192 && height <= 8192 &&
            image_alloc(image, BLACK_TEX_RGBA8, (unsigned)width, (unsigned)height, 1)) {
            ok = png_raw(picture, image->storage, width, height) ||
                 png_drawn(picture, image->storage, width, height);
            if (!ok) black_TextureImageFree(image);
        }
        CGImageRelease(picture);
    }
    if (source) CFRelease(source);
    if (bytes) CFRelease(bytes);
    return ok;
}

int black_TextureLoadFile(const char *path, BlackTextureImage *image)
{
    memset(image, 0, sizeof *image);
    FILE *file = fopen(path, "rb");
    struct stat info;
    if (!file) return 0;
    uint8_t *data = NULL;
    size_t size = 0;
    if (fstat(fileno(file), &info) == 0 && info.st_size > 0 && info.st_size <= (256 << 20) &&
        (data = malloc((size_t)info.st_size)) != NULL)
        size = fread(data, 1, (size_t)info.st_size, file);
    fclose(file);
    int ok = 0;
    if (data && size == (size_t)info.st_size)
        ok = size >= 4 && !memcmp(data, "DDS ", 4) ? black_TextureDecodeDds(data, size, image)
                                                    : black_TextureDecodePng(data, size, image);
    free(data);
    return ok;
}

/* ---- the pack and its map ----------------------------------------------- */

typedef struct { char *path; const char *name; } PackFile;
typedef struct { uint64_t key; uint32_t width, height, format; int file; } PackEntry;
struct BlackTexturePack {
    PackFile *files;
    unsigned count, mapped, missing;
    PackEntry *map;
};

static int has_suffix(const char *name, const char *suffix)
{
    size_t n = strlen(name), s = strlen(suffix);
    return n > s && !strcasecmp(name + n - s, suffix);
}

static int file_order(const void *a, const void *b)
{
    const PackFile *x = a, *y = b;
    int order = strcasecmp(x->name, y->name);
    return order ? order : strcmp(x->path, y->path);
}

static int entry_order(const void *a, const void *b)
{
    const PackEntry *x = a, *y = b;
    if (x->key != y->key) return x->key < y->key ? -1 : 1;
    if (x->width != y->width) return x->width < y->width ? -1 : 1;
    if (x->height != y->height) return x->height < y->height ? -1 : 1;
    if (x->format != y->format) return x->format < y->format ? -1 : 1;
    return x->file - y->file;
}

/* The first file of that name, in path order. */
static int find_name(const BlackTexturePack *pack, const char *name)
{
    unsigned lo = 0, hi = pack->count;
    while (lo < hi) {
        unsigned mid = lo + (hi - lo) / 2;
        if (strcasecmp(pack->files[mid].name, name) < 0) lo = mid + 1; else hi = mid;
    }
    return lo < pack->count && !strcasecmp(pack->files[lo].name, name) ? (int)lo : -1;
}

void black_TexturePackClose(BlackTexturePack *pack)
{
    if (!pack) return;
    for (unsigned i = 0; i < pack->count; ++i) free(pack->files[i].path);
    free(pack->files);
    free(pack->map);
    free(pack);
}

BlackTexturePack *black_TexturePackOpen(const char *replacement, const char *map_path)
{
    BlackTexturePack *pack = calloc(1, sizeof *pack);
    char *roots[] = {(char *)replacement, NULL};
    unsigned capacity = 0, map_capacity = 0;
    if (!pack) return NULL;
    /* FTS_LOGICAL follows a linked pack folder on another volume. */
    FTS *walk = fts_open(roots, FTS_LOGICAL | FTS_NOCHDIR, NULL);
    for (FTSENT *node; walk && (node = fts_read(walk)) != NULL;) {
        if (node->fts_info != FTS_F || !strncmp(node->fts_name, "._", 2) ||
            !(has_suffix(node->fts_name, ".dds") || has_suffix(node->fts_name, ".png")))
            continue;
        if (pack->count == capacity) {
            unsigned grown = capacity ? capacity * 2 : 1024;
            PackFile *files = realloc(pack->files, grown * sizeof *files);
            if (!files) break;
            pack->files = files; capacity = grown;
        }
        char *path = strdup(node->fts_path);
        if (!path) break;
        pack->files[pack->count].path = path;
        pack->files[pack->count].name = path + strlen(path) - node->fts_namelen;
        pack->count++;
    }
    if (walk) fts_close(walk);
    FILE *map = pack->count ? fopen(map_path, "r") : NULL;
    if (!map) { black_TexturePackClose(pack); return NULL; }
    qsort(pack->files, pack->count, sizeof *pack->files, file_order);
    char line[512];
    BlackTextureMapEntry entry;
    while (fgets(line, sizeof line, map)) {
        if (!black_TexturePackParseMapLine(line, &entry)) continue;
        int file = find_name(pack, entry.file);
        if (file < 0) { pack->missing++; continue; }
        if (pack->mapped == map_capacity) {
            unsigned grown = map_capacity ? map_capacity * 2 : 1024;
            PackEntry *entries = realloc(pack->map, grown * sizeof *entries);
            if (!entries) break;
            pack->map = entries; map_capacity = grown;
        }
        pack->map[pack->mapped++] = (PackEntry){entry.key, entry.width, entry.height, entry.format, file};
    }
    fclose(map);
    if (pack->mapped) qsort(pack->map, pack->mapped, sizeof *pack->map, entry_order);
    return pack;
}

int black_TexturePackFind(const BlackTexturePack *pack, uint64_t key, unsigned width,
                          unsigned height, unsigned format)
{
    if (!pack) return -1;
    PackEntry want = {key, width, height, format, -1};
    unsigned lo = 0, hi = pack->mapped;
    while (lo < hi) {
        unsigned mid = lo + (hi - lo) / 2;
        if (entry_order(&pack->map[mid], &want) < 0) lo = mid + 1; else hi = mid;
    }
    if (lo >= pack->mapped) return -1;
    const PackEntry *e = &pack->map[lo];
    return e->key == key && e->width == width && e->height == height && e->format == format ? e->file : -1;
}

const char *black_TexturePackPath(const BlackTexturePack *pack, int file)
{ return pack && file >= 0 && (unsigned)file < pack->count ? pack->files[file].path : NULL; }
unsigned black_TexturePackFiles(const BlackTexturePack *pack) { return pack ? pack->count : 0; }
unsigned black_TexturePackMapped(const BlackTexturePack *pack) { return pack ? pack->mapped : 0; }
unsigned black_TexturePackMissing(const BlackTexturePack *pack) { return pack ? pack->missing : 0; }

/* ---- the running pack ---------------------------------------------------- */

enum { FILE_IDLE, FILE_QUEUED, FILE_READY, FILE_FAILED };
enum { LOADERS = 2 };

static struct {
    pthread_once_t once;
    pthread_mutex_t lock;
    pthread_cond_t wake;
    BlackTexturePack *pack;      /* read-only once ready is 1 */
    atomic_int ready;            /* 0 reading, 1 ready, -1 none */
    atomic_int finished;
    unsigned char *state;
    unsigned *wanted;
    BlackTextureImage *image;
    int *queue;
    unsigned head, tail;
} R = {.once = PTHREAD_ONCE_INIT, .lock = PTHREAD_MUTEX_INITIALIZER, .wake = PTHREAD_COND_INITIALIZER};

static int readable(const char *path)
{
    struct stat info;
    return path && *path && stat(path, &info) == 0 && S_ISREG(info.st_mode);
}

/* <textures>/black-hd-reimagined.map lets a user replace the map; otherwise
 * BLACK_TEXTURE_MAP (developer runs) or the copy in the app's Resources. */
static int find_map(const char *textures, char *out, size_t size)
{
    snprintf(out, size, "%s/" MAP_NAME, textures);
    if (readable(out)) return 1;
    const char *env = getenv("BLACK_TEXTURE_MAP");
    if (readable(env)) { snprintf(out, size, "%s", env); return 1; }
    char exe[PATH_MAX], real[PATH_MAX];
    uint32_t length = sizeof exe;
    if (_NSGetExecutablePath(exe, &length) == 0 && realpath(exe, real)) {
        char *slash = strrchr(real, '/');
        if (slash) {
            *slash = 0;
            snprintf(out, size, "%s/../Resources/" MAP_NAME, real);
            if (readable(out)) return 1;
        }
    }
    return 0;
}

static void *loader(void *unused)
{
    (void)unused;
    unsigned count = black_TexturePackFiles(R.pack);
    pthread_mutex_lock(&R.lock);
    for (;;) {
        while (R.head == R.tail) pthread_cond_wait(&R.wake, &R.lock);
        int file = R.queue[R.head];
        R.head = (R.head + 1) % (count + 1);
        if (!R.wanted[file]) { R.state[file] = FILE_IDLE; continue; }
        pthread_mutex_unlock(&R.lock);
        BlackTextureImage image;
        int ok = black_TextureLoadFile(black_TexturePackPath(R.pack, file), &image);
        if (!ok)
            fprintf(stderr, "[TEXPACK] cannot read %s; the game's texture stays\n",
                    black_TexturePackPath(R.pack, file));
        pthread_mutex_lock(&R.lock);
        if (!R.wanted[file]) { black_TextureImageFree(&image); R.state[file] = FILE_IDLE; }
        else { R.image[file] = image; R.state[file] = ok ? FILE_READY : FILE_FAILED; }
        atomic_store(&R.finished, 1);
    }
    return NULL;
}

static void *open_pack(void *unused)
{
    (void)unused;
    char textures[PATH_MAX], replacement[PATH_MAX], map[PATH_MAX];
    const char *env = getenv("BLACK_TEXTURE_DIR"), *home = getenv("HOME");
    if (env && *env) snprintf(textures, sizeof textures, "%s", env);
    else snprintf(textures, sizeof textures, "%s/Library/Application Support/BLACK/Textures", home ? home : "");
    snprintf(replacement, sizeof replacement, "%s/replacement", textures);
    /* Shows where a pack goes; nothing else is written. */
    if (mkdir(textures, 0755) == 0 || errno == EEXIST) mkdir(replacement, 0755);
    BlackTexturePack *pack = NULL;
    if (!find_map(textures, map, sizeof map))
        fprintf(stderr, "[TEXPACK] no " MAP_NAME " in %s, BLACK_TEXTURE_MAP or the app; replacement stays off\n", textures);
    else if (!(pack = black_TexturePackOpen(replacement, map)))
        fprintf(stderr, "[TEXPACK] no DDS or PNG files under %s; replacement stays off\n", replacement);
    unsigned count = black_TexturePackFiles(pack);
    if (pack) {
        R.state = calloc(count, 1);
        R.wanted = calloc(count, sizeof *R.wanted);
        R.image = calloc(count, sizeof *R.image);
        R.queue = calloc(count + 1, sizeof *R.queue);
        if (!R.state || !R.wanted || !R.image || !R.queue) { black_TexturePackClose(pack); pack = NULL; }
    }
    if (!pack || !black_TexturePackMapped(pack)) {
        if (pack) fprintf(stderr, "[TEXPACK] %u files under %s, none of them in %s\n", count, replacement, map);
        atomic_store(&R.ready, -1);
        return NULL;
    }
    R.pack = pack;
    fprintf(stderr, "[TEXPACK] %u files under %s; map %s: %u textures, %u name files this pack does not have\n",
            count, replacement, map, black_TexturePackMapped(pack), black_TexturePackMissing(pack));
    for (int i = 0; i < LOADERS; ++i) {
        pthread_t thread;
        if (pthread_create(&thread, NULL, loader, NULL) == 0) pthread_detach(thread);
    }
    atomic_store_explicit(&R.ready, 1, memory_order_release);
    return NULL;
}

static void start(void)
{
    pthread_t thread;
    if (pthread_create(&thread, NULL, open_pack, NULL) == 0) pthread_detach(thread);
    else atomic_store(&R.ready, -1);
}

unsigned black_TexturePackGeneration(void)
{
    if (!black_SettingsTextureReplacement()) return 0;
    pthread_once(&R.once, start);
    return atomic_load_explicit(&R.ready, memory_order_acquire) == 1;
}

int black_TexturePackLookup(uint64_t key, unsigned width, unsigned height, unsigned format)
{
    if (atomic_load_explicit(&R.ready, memory_order_acquire) != 1) return -1;
    return black_TexturePackFind(R.pack, key, width, height, format);
}

const char *black_TexturePackName(int file)
{
    const char *path = black_TexturePackPath(R.pack, file), *slash = path ? strrchr(path, '/') : NULL;
    return slash ? slash + 1 : path;
}

void black_TexturePackRequest(int file)
{
    pthread_mutex_lock(&R.lock);
    if (R.wanted[file]++ == 0 && R.state[file] == FILE_IDLE) {
        R.state[file] = FILE_QUEUED;
        R.queue[R.tail] = file;
        R.tail = (R.tail + 1) % (black_TexturePackFiles(R.pack) + 1);
        pthread_cond_signal(&R.wake);
    }
    pthread_mutex_unlock(&R.lock);
}

int black_TexturePackPoll(int file, const BlackTextureImage **image)
{
    pthread_mutex_lock(&R.lock);
    int state = R.state[file];
    *image = state == FILE_READY ? &R.image[file] : NULL;
    pthread_mutex_unlock(&R.lock);
    return state == FILE_READY ? 1 : state == FILE_FAILED ? -1 : 0;
}

void black_TexturePackRelease(int file)
{
    pthread_mutex_lock(&R.lock);
    if (R.wanted[file] && --R.wanted[file] == 0 && R.state[file] == FILE_READY) {
        black_TextureImageFree(&R.image[file]);
        R.state[file] = FILE_IDLE;
    }
    pthread_mutex_unlock(&R.lock);
}

int black_TexturePackTakeFinished(void) { return atomic_exchange(&R.finished, 0); }
