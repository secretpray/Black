/* HD texture replacement in the pinned gl/texture.c (texture_pack.h), included
 * by cmake/BlackGraphicsTrace.cmake. A texture the map names is first made
 * from the game's data as usual. Each pack file becomes one GL texture, read
 * and decoded on a loader thread the first time and then shared: it takes the
 * original's place in every binding the map sends to it, at once when it is
 * already on the GPU. The game remakes some textures every frame (a palette
 * that changes, which the key leaves out), so a file is never read twice while
 * its texture is kept. The game's own texture decides alpha (texture_pack.h):
 * where it is opaque the pack's is replaced by one, elsewhere it is scaled to
 * the game's highest, so a file has an opaque and a see-through GL texture. Any
 * miss or failure keeps the game's own texture. */
#include "texture_pack.h"

#define BLACK_TP_GL_BC1 0x83F1  /* COMPRESSED_RGBA_S3TC_DXT1_EXT */
#define BLACK_TP_GL_BC2 0x83F2  /* COMPRESSED_RGBA_S3TC_DXT3_EXT */
#define BLACK_TP_GL_BC3 0x83F3  /* COMPRESSED_RGBA_S3TC_DXT5_EXT */
#define BLACK_TP_GL_BC7 0x8E8C  /* COMPRESSED_RGBA_BPTC_UNORM */
#define BLACK_TP_LOGGED 32
/* Pack textures no binding uses are kept up to this much, oldest dropped first. */
#define BLACK_TP_KEEP_BYTES ((size_t)1024 << 20)

/* A pack file's GL texture, shared by `refs` bindings; files[file * 2 + opaque]. */
typedef struct { GLuint texture; size_t bytes; unsigned refs; bool failed; unsigned long long used; } BlackTpFile;
/* A binding waiting for its file or holding its texture (files[variant]). */
typedef struct { TextureBinding *binding; int file, variant; unsigned alpha; bool pending; } BlackTpSlot;

static struct {
    BlackTpSlot *slots;
    unsigned count, capacity, pending;
    BlackTpFile *files;
    unsigned file_count;
    unsigned generation;
    bool checked, s3tc, bptc;
    unsigned long long eligible, hits, loads, misses, failed, logged, clock;
    size_t resident;
    struct timespec summary_at;
} black_tp;

static void black_tp_summary(const char *why)
{
    fprintf(stderr, "[TEXPACK] %s: eligible=%llu hits=%llu loads=%llu misses=%llu failed=%llu pending=%u resident=%zuMB\n",
            why, black_tp.eligible, black_tp.hits, black_tp.loads, black_tp.misses, black_tp.failed,
            black_tp.pending, black_tp.resident >> 20);
}

static BlackTpFile *black_tp_file(int file)
{
    if ((unsigned)file >= black_tp.file_count) {
        unsigned grown = MAX((unsigned)file + 1, black_tp.file_count * 2);
        BlackTpFile *files = realloc(black_tp.files, grown * sizeof *files);
        if (!files) return NULL;
        memset(files + black_tp.file_count, 0, (grown - black_tp.file_count) * sizeof *files);
        black_tp.files = files;
        black_tp.file_count = grown;
    }
    return &black_tp.files[file];
}

static void black_tp_drop(BlackTpFile *f)
{
    glDeleteTextures(1, &f->texture);
    black_tp.resident -= f->bytes;
    f->texture = 0;
    f->bytes = 0;
}

/* Over the budget, unused pack textures go, least recently used first. */
static void black_tp_trim(void)
{
    while (black_tp.resident > BLACK_TP_KEEP_BYTES) {
        BlackTpFile *oldest = NULL;
        for (unsigned i = 0; i < black_tp.file_count; ++i) {
            BlackTpFile *f = &black_tp.files[i];
            if (f->texture && !f->refs && (!oldest || f->used < oldest->used)) oldest = f;
        }
        if (!oldest) return;
        black_tp_drop(oldest);
    }
}

static void black_tp_remove(unsigned index)
{
    black_tp.slots[index] = black_tp.slots[--black_tp.count];
}

/* texture_binding_destroy: true when the binding's texture is a pack texture,
 * which stays with the pack instead of being deleted. */
static bool black_tp_forget(TextureBinding *binding)
{
    for (unsigned i = 0; i < black_tp.count; ++i) {
        BlackTpSlot *slot = &black_tp.slots[i];
        if (slot->binding != binding) continue;
        bool shared = !slot->pending;
        if (slot->pending) {
            black_TexturePackRelease(slot->file);
            black_tp.pending--;
        } else {
            black_tp.files[slot->variant].refs--;
            black_tp.files[slot->variant].used = ++black_tp.clock;
        }
        black_tp_remove(i);
        if (shared) black_tp_trim();
        return shared;
    }
    return false;
}

static GLenum black_tp_gl_format(int format)
{
    switch (format) {
    case BLACK_TEX_BC1: return black_tp.s3tc ? BLACK_TP_GL_BC1 : 0;
    case BLACK_TEX_BC2: return black_tp.s3tc ? BLACK_TP_GL_BC2 : 0;
    case BLACK_TEX_BC3: return black_tp.s3tc ? BLACK_TP_GL_BC3 : 0;
    case BLACK_TEX_BC7: return black_tp.bptc ? BLACK_TP_GL_BC7 : 0;
    case BLACK_TEX_RGBA8: return GL_RGBA8;
    default: return 0;
    }
}

/* A new GL texture with every level of `image`: alpha one when `opaque`,
 * otherwise scaled to reach `alpha` (0 leaves it). 0 with `restore` bound again
 * on the active unit when that fails. */
static GLuint black_tp_upload(const BlackTextureImage *image, bool opaque, unsigned alpha, GLuint restore,
                              size_t *bytes)
{
    GLenum format = black_tp_gl_format(image->format);
    if (!format) return 0;
    BlackTextureImage copy;
    if (!opaque && alpha && image->alpha_max && abs((int)image->alpha_max - (int)alpha) > 3 &&
        (image->format == BLACK_TEX_BC2 || image->format == BLACK_TEX_BC3 || image->format == BLACK_TEX_RGBA8)) {
        /* The decoded file is shared with other requests: scale a copy. */
        size_t total = 0;
        for (unsigned level = 0; level < image->levels; ++level) total += image->level_size[level];
        copy = *image;
        if ((copy.storage = malloc(total)) != NULL) {
            memcpy(copy.storage, image->storage, total);
            for (unsigned level = 0; level < image->levels; ++level)
                copy.level[level] = (const uint8_t *)copy.storage + (image->level[level] - (const uint8_t *)image->storage);
            black_TextureScaleAlpha(&copy, image->alpha_max, alpha);
            GLuint texture = black_tp_upload(&copy, false, 0, restore, bytes);
            free(copy.storage);
            return texture;
        }
    }
    while (glGetError() != GL_NO_ERROR) {}
    GLint alignment = 4;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    unsigned levels = image->levels;
    *bytes = 0;
    for (unsigned level = 0; level < image->levels; ++level) {
        GLsizei w = MAX(1, image->width >> level), h = MAX(1, image->height >> level);
        if (image->format == BLACK_TEX_RGBA8)
            glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, image->level[level]);
        else
            glCompressedTexImage2D(GL_TEXTURE_2D, level, format, w, h, 0,
                                   (GLsizei)image->level_size[level], image->level[level]);
        *bytes += image->level_size[level];
    }
    if (levels == 1 && (image->width > 1 || image->height > 1) && image->format == BLACK_TEX_RGBA8) {
        /* A PNG has one level; the game's samplers expect a full chain. */
        glGenerateMipmap(GL_TEXTURE_2D);
        for (unsigned side = MAX(image->width, image->height); side > 1; side >>= 1) levels++;
        *bytes += *bytes / 3;
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, levels - 1);
    if (opaque) glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_ONE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        glDeleteTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, restore);
        fprintf(stderr, "[TEXPACK] GL error 0x%04x uploading %ux%u format %d; the game's texture stays\n",
                error, image->width, image->height, image->format);
        return 0;
    }
    return texture;
}

/* The pack texture replaces the binding's own, bound on the active unit. */
static void black_tp_assign(TextureBinding *binding, BlackTpFile *f)
{
    glDeleteTextures(1, &binding->gl_texture);
    binding->gl_texture = f->texture;
    glBindTexture(GL_TEXTURE_2D, f->texture);
    /* Make apply_texture_parameters set filters and wrapping on it again. */
    binding->min_filter = binding->mag_filter = 0xFFFFFFFF;
    binding->lod_bias = 0xFFFFFFFF;
    binding->addru = binding->addrv = binding->addrp = 0xFFFFFFFF;
    binding->border_color_set = false;
    f->refs++;
    f->used = ++black_tp.clock;
    black_tp.hits++;
}

/* The slot's file, if the loader is done with it. */
static void black_tp_complete(unsigned index)
{
    BlackTpSlot *slot = &black_tp.slots[index];
    const BlackTextureImage *image = NULL;
    int file = slot->file;
    int state = black_TexturePackPoll(file, &image);
    if (!state) return;
    BlackTpFile *f = &black_tp.files[slot->variant];
    bool opaque = slot->variant & 1;
    if (state > 0 && !f->texture) {
        size_t bytes = 0;
        GLuint texture = black_tp_upload(image, opaque, slot->alpha, slot->binding->gl_texture, &bytes);
        if (texture) {
            f->texture = texture;
            f->bytes = bytes;
            black_tp.resident += bytes;
            black_tp.loads++;
            if (black_tp.loads <= BLACK_TP_LOGGED)
                fprintf(stderr, "[TEXPACK] load %llu: %s %ux%u, %u levels, alpha %u -> %s%u\n", black_tp.loads,
                        black_TexturePackName(file), image->width, image->height, image->levels,
                        image->alpha_max, opaque ? "opaque " : "", opaque ? 255 : slot->alpha);
        }
    }
    black_TexturePackRelease(file);
    black_tp.pending--;
    if (f->texture) {
        slot->pending = false;
        black_tp_assign(slot->binding, f);
        black_tp_trim();
    } else {
        f->failed = true;
        black_tp.failed++;
        black_tp_remove(index);
    }
}

/* After generate_texture, on the active unit, for a texture that was not a
 * render target. */
static void black_tp_created(TextureBinding *binding, TextureShape s, const uint8_t *data, size_t length,
                             const uint8_t *palette, size_t palette_length)
{
    if (!black_tp.generation || s.cubemap || s.dimensionality != 2 || s.border ||
        binding->gl_target != GL_TEXTURE_2D)
        return;
    size_t top = black_TexturePackTopBytes(s.color_format, s.width, s.height);
    if (!top || top > length) return;
    black_tp.eligible++;
    int file = black_TexturePackLookup(black_TexturePackKey(data, top), s.width, s.height, s.color_format);
    if (file < 0) { black_tp.misses++; return; }
    unsigned low = 0, high = 0;
    bool known = black_TextureTopAlpha(s.color_format, s.width, s.height, data, palette, palette_length, &low, &high);
    bool opaque = known && low == 255;
    int variant = file * 2 + opaque;
    BlackTpFile *f = black_tp_file(variant);
    if (!f || f->failed) return;
    if (black_tp.count == black_tp.capacity) {
        unsigned grown = black_tp.capacity ? black_tp.capacity * 2 : 256;
        BlackTpSlot *slots = realloc(black_tp.slots, grown * sizeof *slots);
        if (!slots) return;
        black_tp.slots = slots;
        black_tp.capacity = grown;
    }
    if (f->texture) {
        black_tp.slots[black_tp.count++] = (BlackTpSlot){binding, file, variant, 0, false};
        black_tp_assign(binding, f);
        return;
    }
    black_tp.slots[black_tp.count++] = (BlackTpSlot){binding, file, variant, known && !opaque ? high : 0, true};
    black_tp.pending++;
    black_TexturePackRequest(file);
    black_tp_complete(black_tp.count - 1);
}

/* Every texture is made again: with replacements after the pack is ready or
 * the option is turned on, without them after it is turned off. */
static void black_tp_flush(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHGLState *r = pg->gl_renderer_state;
    for (int i = 0; i < NV2A_MAX_TEXTURES; i++) {
        if (r->texture_binding[i]) {
            texture_binding_destroy(r->texture_binding[i]);
            r->texture_binding[i] = NULL;
        }
        pg->texture_dirty[i] = true;
    }
    lru_flush(&r->texture_cache);
    for (unsigned i = 0; i < black_tp.file_count; ++i)
        if (black_tp.files[i].texture && !black_tp.files[i].refs) black_tp_drop(&black_tp.files[i]);
}

/* Start of pgraph_gl_bind_textures, before any unit is bound. */
static void black_tp_poll(NV2AState *d)
{
    unsigned generation = black_TexturePackGeneration();
    if (generation != black_tp.generation) {
        if (generation && !black_tp.checked) {
            black_tp.checked = true;
            black_tp.s3tc = glo_check_extension("GL_EXT_texture_compression_s3tc") ||
                (glo_check_extension("GL_EXT_texture_compression_dxt1") &&
                 glo_check_extension("GL_ANGLE_texture_compression_dxt3") &&
                 glo_check_extension("GL_ANGLE_texture_compression_dxt5"));
            black_tp.bptc = glo_check_extension("GL_EXT_texture_compression_bptc") ||
                glo_check_extension("GL_ARB_texture_compression_bptc");
            fprintf(stderr, "[TEXPACK] GPU formats: BC1-BC3 %s, BC7 %s\n",
                    black_tp.s3tc ? "yes" : "no", black_tp.bptc ? "yes" : "no");
        }
        if (generation) fprintf(stderr, "[TEXPACK] size limit %u\n", generation);
        black_tp_summary(generation ? "on, remaking textures" : "off, remaking textures");
        black_tp.generation = generation;
        black_tp_flush(d);
    }
    if (black_tp.pending && black_TexturePackTakeFinished())
        for (unsigned i = black_tp.count; i-- > 0;)
            if (black_tp.slots[i].pending) black_tp_complete(i);
    /* Every five seconds while anything changes: a run may end on a signal. */
    unsigned long long seen = black_tp.eligible + black_tp.hits + black_tp.failed;
    if (generation && seen != black_tp.logged) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec - black_tp.summary_at.tv_sec >= 5) {
            black_tp.summary_at = now;
            black_tp.logged = seen;
            black_tp_summary("progress");
        }
    }
}
