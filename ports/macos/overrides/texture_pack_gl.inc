/* HD texture replacement in the pinned gl/texture.c (texture_pack.h), included
 * by cmake/BlackGraphicsTrace.cmake. A texture the map names is first made
 * from the game's data as usual; its pack file is read on a loader thread and,
 * once decoded, uploaded to a new GL texture that takes the original's place
 * in the binding. Any miss or failure keeps the game's own texture. */
#include "texture_pack.h"

#define BLACK_TP_GL_BC1 0x83F1  /* COMPRESSED_RGBA_S3TC_DXT1_EXT */
#define BLACK_TP_GL_BC2 0x83F2  /* COMPRESSED_RGBA_S3TC_DXT3_EXT */
#define BLACK_TP_GL_BC3 0x83F3  /* COMPRESSED_RGBA_S3TC_DXT5_EXT */
#define BLACK_TP_GL_BC7 0x8E8C  /* COMPRESSED_RGBA_BPTC_UNORM */
#define BLACK_TP_LOGGED 32

/* A binding waiting for its file (file >= 0) or holding a replacement (bytes). */
typedef struct { TextureBinding *binding; int file; size_t bytes; } BlackTpSlot;

static struct {
    BlackTpSlot *slots;
    unsigned count, capacity, pending;
    unsigned generation;
    bool checked, s3tc, bptc;
    unsigned long long eligible, hits, misses, failed, logged;
    size_t resident;
    struct timespec summary_at;
} black_tp;

static void black_tp_summary(const char *why)
{
    fprintf(stderr, "[TEXPACK] %s: eligible=%llu hits=%llu misses=%llu failed=%llu pending=%u resident=%zuMB\n",
            why, black_tp.eligible, black_tp.hits, black_tp.misses, black_tp.failed, black_tp.pending,
            black_tp.resident >> 20);
}

static void black_tp_remove(unsigned index)
{
    black_tp.slots[index] = black_tp.slots[--black_tp.count];
}

/* texture_binding_destroy, before the GL texture goes. */
static void black_tp_forget(TextureBinding *binding)
{
    for (unsigned i = 0; i < black_tp.count; ++i) {
        if (black_tp.slots[i].binding != binding) continue;
        if (black_tp.slots[i].file >= 0) {
            black_TexturePackRelease(black_tp.slots[i].file);
            black_tp.pending--;
        } else {
            black_tp.resident -= black_tp.slots[i].bytes;
        }
        black_tp_remove(i);
        return;
    }
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

/* A new GL texture with every level of `image`; the binding keeps its own if
 * anything fails. Leaves the new texture bound on the active unit. */
static bool black_tp_upload(TextureBinding *binding, const BlackTextureImage *image, size_t *bytes)
{
    GLenum format = black_tp_gl_format(image->format);
    if (!format || binding->gl_target != GL_TEXTURE_2D) return false;
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
    glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        glDeleteTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, binding->gl_texture);
        fprintf(stderr, "[TEXPACK] GL error 0x%04x uploading %ux%u format %d; the game's texture stays\n",
                error, image->width, image->height, image->format);
        return false;
    }
    glDeleteTextures(1, &binding->gl_texture);
    binding->gl_texture = texture;
    /* Make apply_texture_parameters set filters and wrapping on the new one. */
    binding->min_filter = binding->mag_filter = 0xFFFFFFFF;
    binding->lod_bias = 0xFFFFFFFF;
    binding->addru = binding->addrv = binding->addrp = 0xFFFFFFFF;
    binding->border_color_set = false;
    return true;
}

/* The slot's file, if the loader is done with it. */
static void black_tp_complete(unsigned index)
{
    BlackTpSlot *slot = &black_tp.slots[index];
    const BlackTextureImage *image = NULL;
    int state = black_TexturePackPoll(slot->file, &image);
    if (!state) return;
    size_t bytes = 0;
    bool ok = state > 0 && black_tp_upload(slot->binding, image, &bytes);
    int file = slot->file;
    if (ok) {
        black_tp.hits++;
        if (black_tp.hits <= BLACK_TP_LOGGED)
            fprintf(stderr, "[TEXPACK] hit %llu: %s %ux%u, %u levels\n", black_tp.hits,
                    black_TexturePackName(file), image->width, image->height, image->levels);
    }
    black_TexturePackRelease(file);
    black_tp.pending--;
    if (ok) {
        slot->file = -1;
        slot->bytes = bytes;
        black_tp.resident += bytes;
    } else {
        black_tp.failed++;
        black_tp_remove(index);
    }
}

/* After generate_texture, on the active unit, for a texture that was not a
 * render target. */
static void black_tp_created(TextureBinding *binding, TextureShape s, const uint8_t *data, size_t length)
{
    if (!black_tp.generation || s.cubemap || s.dimensionality != 2 || s.border) return;
    size_t top = black_TexturePackTopBytes(s.color_format, s.width, s.height);
    if (!top || top > length) return;
    black_tp.eligible++;
    int file = black_TexturePackLookup(black_TexturePackKey(data, top), s.width, s.height, s.color_format);
    if (file < 0) { black_tp.misses++; return; }
    if (black_tp.count == black_tp.capacity) {
        unsigned grown = black_tp.capacity ? black_tp.capacity * 2 : 256;
        BlackTpSlot *slots = realloc(black_tp.slots, grown * sizeof *slots);
        if (!slots) return;
        black_tp.slots = slots;
        black_tp.capacity = grown;
    }
    black_tp.slots[black_tp.count++] = (BlackTpSlot){binding, file, 0};
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
        black_tp_summary(generation ? "on, remaking textures" : "off, remaking textures");
        black_tp.generation = generation;
        black_tp_flush(d);
    }
    if (black_tp.pending && black_TexturePackTakeFinished())
        for (unsigned i = black_tp.count; i-- > 0;)
            if (black_tp.slots[i].file >= 0) black_tp_complete(i);
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
