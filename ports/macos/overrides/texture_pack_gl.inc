/* HD texture replacement in the pinned gl/texture.c (texture_pack.h), included
 * by cmake/BlackGraphicsTrace.cmake. A texture the map names is first made
 * from the game's data as usual. Each pack file becomes one GL texture, read
 * and decoded on a loader thread the first time and then shared: it takes the
 * original's place in every binding the map sends to it, at once when it is
 * already on the GPU. The game remakes some textures every frame (a palette
 * that changes, which the key leaves out), so a file is never read twice while
 * its texture is kept. The game's own texture decides alpha (texture_pack.h):
 * where it is opaque the pack's is replaced by one, elsewhere it is scaled to
 * the game's highest, so a file has an opaque and a see-through GL texture.
 * Where the game's alpha varies, the file's must be the same mask once scaled
 * (black_TextureAlphaAgrees): a PS2 channel flat at 128 or cut to another
 * shape painted weapons white and the grenade crate black, so then the game's
 * texture stays. Any miss or failure keeps the game's own texture. */
#include "texture_pack.h"
#include "texture_dump.h"
#include "input_atlas.h"
#include "hud_reticle.h"
#include "reticle_shape.h"
#include "game_mods.h"
#include "port_menu.h"

#define BLACK_TP_GL_BC1 0x83F1  /* COMPRESSED_RGBA_S3TC_DXT1_EXT */
#define BLACK_TP_GL_BC2 0x83F2  /* COMPRESSED_RGBA_S3TC_DXT3_EXT */
#define BLACK_TP_GL_BC3 0x83F3  /* COMPRESSED_RGBA_S3TC_DXT5_EXT */
#define BLACK_TP_GL_BC7 0x8E8C  /* COMPRESSED_RGBA_BPTC_UNORM */
#define BLACK_TP_LOGGED 32
/* Pack textures no binding uses are kept up to this much, oldest dropped first. */
#define BLACK_TP_KEEP_BYTES ((size_t)1024 << 20)

/* A pack file's GL texture, shared by `refs` bindings; files[file * 2 + opaque]. */
typedef struct {
    GLuint texture; size_t bytes; unsigned refs; bool failed, graded; unsigned long long used;
    unsigned alpha_max; uint8_t grid[64]; /* the file's alpha, once read and graded */
} BlackTpFile;
/* A binding waiting for its file or holding its texture (files[variant]);
 * `graded` with the game's alpha grid when that alpha varies. */
typedef struct {
    TextureBinding *binding; int file, variant; unsigned alpha; bool pending, graded; uint8_t grid[64];
} BlackTpSlot;

static struct {
    BlackTpSlot *slots;
    unsigned count, capacity, pending;
    BlackTpFile *files;
    unsigned file_count;
    unsigned generation, reticle;
    bool checked, s3tc, bptc;
    unsigned long long eligible, hits, loads, misses, failed, mismatched, logged, clock;
    size_t resident;
    struct timespec summary_at;
} black_tp;

/* The aiming dot's HUD atlas at 4x (hud_reticle.h): one GL texture per atlas
 * and palette, built and uploaded once and then shared by every binding the
 * game makes of it, as a pack file is. A few are kept so that a binding still
 * holding one never loses it; an atlas the rebuild refused is not tried again. */
enum { BLACK_TP_RETICLE_TEXTURES = 4 };
static struct {
    struct { GLuint texture; unsigned refs, scale; uint64_t key, palette; } held[BLACK_TP_RETICLE_TEXTURES];
    uint64_t refused_key, refused_palette;
    bool refused;
} black_tp_reticle;

/* Opt-in, metadata-only audit. A new path per run avoids overwriting evidence;
 * no pixels or pack contents are written. Counts above include repeated binds,
 * whereas this records each (key, size, format) once. */
static void black_tp_audit(uint64_t key, TextureShape s, int file)
{
    enum { AUDIT_SLOTS = 16384 };
    typedef struct { uint64_t key; unsigned width, height, format; } Seen;
    static bool started;
    static FILE *out;
    static Seen *seen;
    if (!started) {
        started = true;
        const char *path = getenv("BLACK_TEXTURE_AUDIT");
        if (!path || !*path) return;
        out = fopen(path, "wx");
        if (!out || !(seen = calloc(AUDIT_SLOTS, sizeof *seen))) {
            if (out) fclose(out);
            out = NULL;
            fprintf(stderr, "[TEXPACK] cannot create texture audit %s\n", path);
            return;
        }
        setvbuf(out, NULL, _IOLBF, 0);
        fprintf(out, "key,width,height,format,file\n");
    }
    if (!out) return;
    unsigned slot = (unsigned)(key ^ (key >> 32) ^ s.width ^ (s.height << 12) ^ s.color_format);
    for (unsigned probe = 0; probe < AUDIT_SLOTS; ++probe) {
        Seen *p = &seen[(slot + probe) % AUDIT_SLOTS];
        if (!p->width) {
            *p = (Seen){key, s.width, s.height, s.color_format};
            fprintf(out, "%016llx,%u,%u,%02x,%s\n", (unsigned long long)key,
                    s.width, s.height, s.color_format, file >= 0 ? black_TexturePackName(file) : "");
            return;
        }
        if (p->key == key && p->width == s.width && p->height == s.height && p->format == s.color_format)
            return;
    }
}

static void black_tp_summary(const char *why)
{
    fprintf(stderr, "[TEXPACK] %s: eligible=%llu hits=%llu loads=%llu misses=%llu failed=%llu alpha_mismatch=%llu pending=%u resident=%zuMB\n",
            why, black_tp.eligible, black_tp.hits, black_tp.loads, black_tp.misses, black_tp.failed, black_tp.mismatched,
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
    for (unsigned i = 0; i < BLACK_TP_RETICLE_TEXTURES; ++i)
        if (black_tp_reticle.held[i].texture && binding->gl_texture == black_tp_reticle.held[i].texture) {
            black_tp_reticle.held[i].refs--;
            return true;
        }
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
    if (state > 0 && !opaque && image->alpha_graded && !f->graded) {
        /* Kept so later bindings are judged without reading the file again. */
        f->graded = true;
        f->alpha_max = image->alpha_max;
        memcpy(f->grid, image->alpha_grid, sizeof f->grid);
    }
    if (state > 0 && slot->graded && f->graded &&
        !black_TextureAlphaAgrees(slot->grid, slot->alpha, f->grid, f->alpha_max)) {
        black_tp.mismatched++;
        if (black_tp.mismatched <= BLACK_TP_LOGGED)
            fprintf(stderr, "[TEXPACK] keep the game's texture: %s alpha is another mask than the game's\n",
                    black_TexturePackName(file));
        black_TexturePackRelease(file);
        black_tp.pending--;
        black_tp_remove(index);
        return;
    }
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

/* The game's HUD atlas rebuilt (hud_reticle.h): at 4x with the HD setting, at
 * 2x with a Reticle Shape that needs the painted cells, in place of the
 * binding's texture; 0 leaves the game's own. On by its setting alone, so it
 * works without a texture pack, and no pack file is looked up for this key. */
static int black_tp_reticle_created(TextureBinding *binding, TextureShape s, const uint8_t *data,
                                    size_t top, const uint8_t *palette, size_t palette_length)
{
    if (!palette || s.color_format != 0x0B || s.width != BLACK_HUD_RETICLE_W ||
        s.height != BLACK_HUD_RETICLE_H || binding->gl_target != GL_TEXTURE_2D)
        return 0;
    uint64_t key = black_TexturePackKey(data, top);
    if (!black_HudReticleWanted(key)) return 0;
    uint64_t palette_key = black_TexturePackKey(palette, palette_length);
    if (black_tp_reticle.refused && black_tp_reticle.refused_key == key &&
        black_tp_reticle.refused_palette == palette_key)
        return 0;
    unsigned scale = black_SettingsHdReticle() ? BLACK_HUD_RETICLE_SCALE : BLACK_HUD_RETICLE_PLAIN_SCALE;
    int found = -1, spare = -1;
    for (int i = 0; i < BLACK_TP_RETICLE_TEXTURES; ++i) {
        if (black_tp_reticle.held[i].texture && black_tp_reticle.held[i].key == key &&
            black_tp_reticle.held[i].palette == palette_key && black_tp_reticle.held[i].scale == scale)
            found = i;
        else if (spare < 0 && !black_tp_reticle.held[i].refs)
            spare = i;
    }
    if (found < 0) {
        /* Every kept texture is still in use by a binding: the game's stays. */
        if (spare < 0) return 0;
        BlackHudReticleImage image;
        if (!black_HudReticleBuild(s.width, s.height, s.color_format, data, top, palette, palette_length,
                                   scale, &image)) {
            black_tp_reticle.refused = true;
            black_tp_reticle.refused_key = key;
            black_tp_reticle.refused_palette = palette_key;
            fprintf(stderr, "[RETICLE] atlas %016llx does not match the expected aiming dot; the game's texture stays\n",
                    (unsigned long long)key);
            return 0;
        }
        /* An error left by an earlier draw must not look like this upload's. */
        while (glGetError() != GL_NO_ERROR) {}
        GLint alignment = 4, row_length = 0;
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
        glGetIntegerv(GL_UNPACK_ROW_LENGTH, &row_length);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        GLuint texture = 0;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        for (unsigned level = 0; level < image.levels; ++level)
            glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA8, (GLsizei)MAX(1, image.width >> level),
                         (GLsizei)MAX(1, image.height >> level), 0, GL_RGBA, GL_UNSIGNED_BYTE, image.level[level]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, image.levels - 1);
        glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, row_length);
        GLenum error = glGetError();
        if (error != GL_NO_ERROR) {
            glDeleteTextures(1, &texture);
            glBindTexture(GL_TEXTURE_2D, binding->gl_texture);
            fprintf(stderr, "[RETICLE] GL error 0x%04x uploading %ux%u; the game's texture stays\n",
                    error, image.width, image.height);
            black_HudReticleFree(&image);
            return 0;
        }
        fprintf(stderr, "[RETICLE] atlas %ux%u -> %ux%u, %u levels, dot %.2f,%.2f r %.2f, painted cross\n",
                BLACK_HUD_RETICLE_W, BLACK_HUD_RETICLE_H, image.width, image.height, image.levels,
                image.dot_x, image.dot_y, image.dot_radius);
        black_HudReticleFree(&image);
        if (black_tp_reticle.held[spare].texture) glDeleteTextures(1, &black_tp_reticle.held[spare].texture);
        black_tp_reticle.held[spare].texture = texture;
        black_tp_reticle.held[spare].refs = 0;
        black_tp_reticle.held[spare].scale = scale;
        black_tp_reticle.held[spare].key = key;
        black_tp_reticle.held[spare].palette = palette_key;
        found = spare;
    }
    /* The shared texture takes the binding's place, bound on the active unit. */
    glDeleteTextures(1, &binding->gl_texture);
    binding->gl_texture = black_tp_reticle.held[found].texture;
    glBindTexture(GL_TEXTURE_2D, binding->gl_texture);
    /* Make apply_texture_parameters set filters and wrapping on it again. */
    binding->min_filter = binding->mag_filter = 0xFFFFFFFF;
    binding->lod_bias = 0xFFFFFFFF;
    binding->addru = binding->addrv = binding->addrp = 0xFFFFFFFF;
    binding->border_color_set = false;
    black_tp_reticle.held[found].refs++;
    return 1;
}

/* After generate_texture, on the active unit, for a texture that was not a
 * render target. */
static void black_tp_created(TextureBinding *binding, TextureShape s, const uint8_t *data, size_t length,
                             const uint8_t *palette, size_t palette_length)
{
    if (s.cubemap || s.dimensionality != 2 || s.border ||
        binding->gl_target != GL_TEXTURE_2D)
        return;
    BlackInputAtlas input;
    if(black_InputAtlasFind(data,length,s.width,s.height,s.color_format,&input)) {
        glBindTexture(GL_TEXTURE_2D,binding->gl_texture);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,input.width,input.height,0,GL_RGBA,GL_UNSIGNED_BYTE,input.rgba);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_BASE_LEVEL,0);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,0);
        fprintf(stderr,"[PROMPTS] host atlas %ux%u installed (guest %ux%u)\n",input.width,input.height,s.width,s.height);
        return;
    }
    black_TextureDump(s.color_format, s.width, s.height, data, length, palette, palette_length);
    size_t top = black_TexturePackTopBytes(s.color_format, s.width, s.height);
    if (!top || top > length) return;
    /* Its own setting, before the pack: no pack file is looked up for the
     * atlas. Without HD the rebuild still runs for a Reticle Shape other than
     * the dot, whose painted cells only exist in the rebuilt atlas. */
    if ((black_SettingsHdReticle() || black_SettingsMod(BLACK_MOD_RETICLE) != BLACK_RETICLE_DOT) &&
        black_tp_reticle_created(binding, s, data, top, palette, palette_length))
        return;
    if (!black_tp.generation) return;
    black_tp.eligible++;
    uint64_t key = black_TexturePackKey(data, top);
    int file = black_TexturePackLookup(key, s.width, s.height, s.color_format);
    black_tp_audit(key, s, file);
    if (file < 0) { black_tp.misses++; return; }
    unsigned low = 0, high = 0;
    bool known = black_TextureTopAlpha(s.color_format, s.width, s.height, data, palette, palette_length, &low, &high);
    bool opaque = known && low == 255;
    BlackTpSlot slot = {binding, file, file * 2 + opaque, known && !opaque ? high : 0, true, false, {0}};
    slot.graded = known && low != high && black_TextureTopAlphaGrid(s.color_format, s.width, s.height, data, slot.grid);
    BlackTpFile *f = black_tp_file(slot.variant);
    if (!f || f->failed) return;
    if (slot.graded && f->graded && !black_TextureAlphaAgrees(slot.grid, high, f->grid, f->alpha_max)) {
        black_tp.mismatched++;
        return;
    }
    if (black_tp.count == black_tp.capacity) {
        unsigned grown = black_tp.capacity ? black_tp.capacity * 2 : 256;
        BlackTpSlot *slots = realloc(black_tp.slots, grown * sizeof *slots);
        if (!slots) return;
        black_tp.slots = slots;
        black_tp.capacity = grown;
    }
    if (f->texture) {
        slot.pending = false;
        black_tp.slots[black_tp.count++] = slot;
        black_tp_assign(binding, f);
        return;
    }
    black_tp.slots[black_tp.count++] = slot;
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
    /* With neither HD nor a painted shape nothing asks for the rebuilt atlas:
     * free what no binding holds. */
    if (!black_tp.reticle)
        for (unsigned i = 0; i < BLACK_TP_RETICLE_TEXTURES; ++i)
            if (black_tp_reticle.held[i].texture && !black_tp_reticle.held[i].refs) {
                glDeleteTextures(1, &black_tp_reticle.held[i].texture);
                black_tp_reticle.held[i].texture = 0;
            }
}

/* Start of pgraph_gl_bind_textures, before any unit is bound. */
static void black_tp_poll(NV2AState *d)
{
    unsigned generation = black_TexturePackGeneration();
    /* Bit 0: the HD setting. Bit 1: a Reticle Shape that needs the painted
     * cells, which asks for the rebuilt atlas even without HD. */
    unsigned reticle = (black_SettingsHdReticle() ? 1u : 0u) |
                       (black_SettingsMod(BLACK_MOD_RETICLE) != BLACK_RETICLE_DOT ? 2u : 0u);
    bool remake = false;
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
        remake = true;
    }
    if (reticle != black_tp.reticle) {
        black_tp.reticle = reticle;
        fprintf(stderr, "[RETICLE] %s, remaking textures\n",
                reticle & 1 ? "HD" : reticle & 2 ? "Original with the painted reticle cells" : "Original");
        remake = true;
    }
    if (remake) black_tp_flush(d);
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
