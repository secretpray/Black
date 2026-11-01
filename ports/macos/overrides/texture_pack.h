#ifndef BLACK_TEXTURE_PACK_H
#define BLACK_TEXTURE_PACK_H
/* Optional HD texture replacement from the PCSX2 "BLACK HD Reimagined" pack.
 * Its files are named by PS2 GS hashes; portingpete's map
 * (resources/textures/black-hd-reimagined.map, MIT) names, for each Xbox
 * texture, the pack file that replaces it. A texture is found by the map's key:
 * 64-bit FNV-1a over its top level's bytes as they are in guest memory, taken
 * as little-endian 64-bit words, plus its width, height and NV2A colour format
 * (0b I8 palette, 0c DXT1, 0e DXT3, 0f DXT5). The palette is not part of it.
 * Pack files are looked up by bare file name anywhere under
 * <textures>/replacement, <textures> being BLACK_TEXTURE_DIR or
 * ~/Library/Application Support/BLACK/Textures. */
#include <stddef.h>
#include <stdint.h>

enum { BLACK_TEX_BC1 = 1, BLACK_TEX_BC2, BLACK_TEX_BC3, BLACK_TEX_BC7, BLACK_TEX_RGBA8 };
enum { BLACK_TEX_MAX_LEVELS = 16 };

/* A decoded pack file: its levels, largest first, inside one allocation. */
typedef struct {
    int format;
    unsigned width, height, levels;
    const uint8_t *level[BLACK_TEX_MAX_LEVELS];
    size_t level_size[BLACK_TEX_MAX_LEVELS];
    unsigned alpha_max;          /* highest alpha of level 0, set by the loader */
    void *storage;
} BlackTextureImage;

typedef struct {
    uint64_t key;
    unsigned width, height, format;
    char file[256];
} BlackTextureMapEntry;

typedef struct BlackTexturePack BlackTexturePack;

/* Bytes of the top level the key covers; 0 for a texture the map cannot name. */
size_t black_TexturePackTopBytes(unsigned format, unsigned width, unsigned height);
uint64_t black_TexturePackKey(const void *top, size_t bytes);
/* 1 for an entry, 0 for a comment or a malformed line. */
int black_TexturePackParseMapLine(const char *line, BlackTextureMapEntry *entry);

/* Alpha: PCSX2 packs keep the PS2's, on which 128 is opaque, and PS2 pictures
 * of opaque surfaces carry alpha the Xbox would read. So the game's own texture
 * decides: where it is opaque the pack's alpha is not used, elsewhere the pack's
 * is scaled so its highest value is the game's (128 -> 255 on the PS2 scale).
 * Lowest and highest alpha of the game's top level (I8 through its A8R8G8B8
 * palette); 0 when it cannot tell. */
int black_TextureTopAlpha(unsigned format, unsigned width, unsigned height, const uint8_t *top,
                          const uint8_t *palette, size_t palette_bytes, unsigned *low, unsigned *high);
/* Highest alpha of a picture's top level; 255 for BC1 and BC7, which are not read. */
unsigned black_TextureAlphaMax(const BlackTextureImage *image);
/* Every level's alpha times to/from, clamped (BC2, BC3 and RGBA8). */
void black_TextureScaleAlpha(BlackTextureImage *image, unsigned from, unsigned to);

/* Largest side `limit`: block formats drop their top levels while a smaller one
 * is there (a single-level file stays as it is), RGBA8 is halved by a box
 * filter. 1 when the picture changed. */
int black_TextureLimitSize(BlackTextureImage *image, unsigned limit);

/* DDS: BC1, BC2, BC3, BC7 (also as DX10) and 32-bit RGBA; PNG through ImageIO.
 * 1 on success; black_TextureImageFree releases either. */
int black_TextureDecodeDds(const uint8_t *data, size_t size, BlackTextureImage *image);
int black_TextureDecodePng(const uint8_t *data, size_t size, BlackTextureImage *image);
int black_TextureLoadFile(const char *path, BlackTextureImage *image);
void black_TextureImageFree(BlackTextureImage *image);

/* The pack's files under `replacement` and the map's entries for which a file
 * exists. NULL when the folder has no DDS or PNG files or the map is unreadable. */
BlackTexturePack *black_TexturePackOpen(const char *replacement, const char *map);
void black_TexturePackClose(BlackTexturePack *pack);
/* The file for a texture, or -1. */
int black_TexturePackFind(const BlackTexturePack *pack, uint64_t key, unsigned width,
                          unsigned height, unsigned format);
const char *black_TexturePackPath(const BlackTexturePack *pack, int file);
unsigned black_TexturePackFiles(const BlackTexturePack *pack);
unsigned black_TexturePackMapped(const BlackTexturePack *pack);
unsigned black_TexturePackMissing(const BlackTexturePack *pack);

/* Renderer side, all on the GL thread. The setting and the pack in the
 * background: the value changes whenever the replaced textures should be made
 * again (on/off, pack ready, size limit); 0 while off or still being read. */
unsigned black_TexturePackGeneration(void);
/* The running pack's file for a texture, or -1 (also while off). */
int black_TexturePackLookup(uint64_t key, unsigned width, unsigned height, unsigned format);
const char *black_TexturePackName(int file);
/* Asks for a file to be read on a loader thread. Poll until it returns an
 * image (1) or failure (-1); 0 is still loading. Every request needs its
 * Release, which frees the image when no one else waits for it. */
void black_TexturePackRequest(int file);
int black_TexturePackPoll(int file, const BlackTextureImage **image);
void black_TexturePackRelease(int file);
/* Set by the loader when a request finished; the renderer takes it. */
int black_TexturePackTakeFinished(void);
#endif
