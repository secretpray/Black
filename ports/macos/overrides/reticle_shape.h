#ifndef BLACK_RETICLE_SHAPE_INCLUDED
#define BLACK_RETICLE_SHAPE_INCLUDED
/* The aiming mark's shape (the Mods page's Reticle Shape). With every weapon
 * the title aims through two 32x32 cells of its 512x256 HUD atlas, drawn as
 * quads at the frame's centre: a bright dot (atlas 128..160 x 160..192) and a
 * dim glow (160..192 x 160..192). The same atlas already holds a ring, a ring
 * with four ticks, a cross and four chevrons. A chosen shape takes the dot
 * quad's place: its uv moves to the shape's cell and the quad grows around
 * the same centre; the glow quad collapses to nothing. The atlas itself is
 * not changed, so the cells keep their other uses (the cross is the HUD's
 * medkit, other chevrons mark damage), and the game's tint (white, green on
 * an ally, red on an enemy, black on a destructible object) still colours the
 * mark through the vertex colour. Pure functions on four floats each. */
#include <math.h>
#include <string.h>

enum { BLACK_RETICLE_DOT, BLACK_RETICLE_RING, BLACK_RETICLE_TICKS, BLACK_RETICLE_CROSS,
       BLACK_RETICLE_CHEVRONS, BLACK_RETICLE_SHAPES };
/* The Mods row's last value: the shape follows the weapon in hand
 * (black_reticle_auto). */
enum { BLACK_RETICLE_AUTO = BLACK_RETICLE_SHAPES };

/* A shape's atlas rectangle in texels and how much larger than the dot's quad
 * it is drawn. The ring and the chevrons fill their 64-texel cells and keep
 * the dot's texel density at 2x; the ring with ticks fills about two thirds of
 * its cell and the cross's plus a third of its own, so both are drawn at 2.5x
 * to read at a similar size in play. */
typedef struct { float x0, y0, x1, y1, grow; } BlackReticleShape;
static const BlackReticleShape black_reticle_shapes[BLACK_RETICLE_SHAPES] = {
    {128, 160, 160, 192, 1.0f}, /* the game's own dot */
    {0, 0, 64, 64, 2.0f},       /* ring */
    {0, 64, 64, 128, 2.5f},     /* ring with four ticks */
    {160, 32, 192, 64, 2.5f},   /* cross */
    {192, 0, 256, 64, 2.0f},    /* four chevrons */
};

#define BLACK_RETICLE_ATLAS_W 512.0f
#define BLACK_RETICLE_ATLAS_H 256.0f
/* The game draws the mark at the 640x480 HUD's centre; recoil and aim moves
 * stay well inside this. */
#define BLACK_RETICLE_CENTRE_X 320.0f
#define BLACK_RETICLE_CENTRE_Y 240.0f
#define BLACK_RETICLE_NEAR 48.0f
#define BLACK_RETICLE_MAX_SIDE 32.0f

/* 1 when uv (u0,v0,u1,v1, either corner order) is exactly the atlas cell
 * x0..x1 x y0..y1, within a hundredth of a texel. */
static inline int black_reticle_uv_is(const float uv[4], float x0, float y0, float x1, float y1)
{
    float eu = 0.01f / BLACK_RETICLE_ATLAS_W, ev = 0.01f / BLACK_RETICLE_ATLAS_H;
    float u_lo = fminf(uv[0], uv[2]), u_hi = fmaxf(uv[0], uv[2]);
    float v_lo = fminf(uv[1], uv[3]), v_hi = fmaxf(uv[1], uv[3]);
    return fabsf(u_lo - x0 / BLACK_RETICLE_ATLAS_W) < eu && fabsf(u_hi - x1 / BLACK_RETICLE_ATLAS_W) < eu &&
           fabsf(v_lo - y0 / BLACK_RETICLE_ATLAS_H) < ev && fabsf(v_hi - y1 / BLACK_RETICLE_ATLAS_H) < ev;
}

/* 1 when a quad (x0,y0,x1,y1) is a small sprite around the HUD's centre. */
static inline int black_reticle_at_centre(const float xy[4])
{
    float cx = (xy[0] + xy[2]) * 0.5f, cy = (xy[1] + xy[3]) * 0.5f;
    return fabsf(cx - BLACK_RETICLE_CENTRE_X) <= BLACK_RETICLE_NEAR &&
           fabsf(cy - BLACK_RETICLE_CENTRE_Y) <= BLACK_RETICLE_NEAR &&
           fabsf(xy[2] - xy[0]) <= BLACK_RETICLE_MAX_SIDE && fabsf(xy[3] - xy[1]) <= BLACK_RETICLE_MAX_SIDE;
}

/* One coordinate pair from a to b, given the corner order of the original:
 * a lower first value keeps a lower first value. */
static inline void black_reticle_span(float *first, float *second, float lo, float hi)
{
    if (*first <= *second) { *first = lo; *second = hi; }
    else { *first = hi; *second = lo; }
}

/* Rewrites a quad of the aiming mark for `shape`: the dot quad becomes the
 * shape's cell, grown around its centre; the glow quad collapses to its
 * centre, so it draws nothing. Returns 1 when the quad was changed, 0 for any
 * other quad, the dot shape itself or an unknown shape. */
static inline int black_reticle_reshape(float xy[4], float uv[4], int shape)
{
    if (shape <= BLACK_RETICLE_DOT || shape >= BLACK_RETICLE_SHAPES || !black_reticle_at_centre(xy)) return 0;
    const BlackReticleShape *dot = &black_reticle_shapes[BLACK_RETICLE_DOT];
    float cx = (xy[0] + xy[2]) * 0.5f, cy = (xy[1] + xy[3]) * 0.5f;
    if (black_reticle_uv_is(uv, 160, 160, 192, 192)) {
        xy[0] = xy[2] = cx;
        xy[1] = xy[3] = cy;
        return 1;
    }
    if (!black_reticle_uv_is(uv, dot->x0, dot->y0, dot->x1, dot->y1)) return 0;
    const BlackReticleShape *s = &black_reticle_shapes[shape];
    float hw = fabsf(xy[2] - xy[0]) * 0.5f * s->grow, hh = fabsf(xy[3] - xy[1]) * 0.5f * s->grow;
    black_reticle_span(&xy[0], &xy[2], cx - hw, cx + hw);
    black_reticle_span(&xy[1], &xy[3], cy - hh, cy + hh);
    black_reticle_span(&uv[0], &uv[2], s->x0 / BLACK_RETICLE_ATLAS_W, s->x1 / BLACK_RETICLE_ATLAS_W);
    black_reticle_span(&uv[1], &uv[3], s->y0 / BLACK_RETICLE_ATLAS_H, s->y1 / BLACK_RETICLE_ATLAS_H);
    return 1;
}
/* A weapon table key (the title's twelve characters of " -/0-9A-Z_" in base
 * 40) as text, trailing spaces dropped: 0x... -> "BG1_SNR". */
static inline void black_reticle_weapon_name(unsigned long long key, char out[13])
{
    for (int i = 11; i >= 0; i--) {
        int c = (int)(key % 40);
        key /= 40;
        out[i] = c == 0 ? ' ' : c == 1 ? '-' : c == 2 ? '/' : c < 13 ? (char)('0' + c - 3) :
            c < 39 ? (char)('A' + c - 13) : '_';
    }
    out[12] = 0;
    for (int i = 11; i >= 0 && out[i] == ' '; i--) out[i] = 0;
}

/* Reticle Shape's Auto: a ring for the shotguns (SHG, the Remington RM*) and
 * the launchers (GRL, RPG), whose spread or arc a ring frames; the game's own
 * dot for the sniper rifle (SN*), whose scope is its own; a cross for every
 * other weapon, and for a name it does not know. The level's bank prefix
 * (BG1_) is skipped. */
static inline int black_reticle_auto(const char *name)
{
    if (!name || !*name) return BLACK_RETICLE_CROSS;
    const char *code = name;
    for (const char *p = name; *p; ++p) if (*p == '_') code = p + 1;
    if (!strncmp(code, "SN", 2)) return BLACK_RETICLE_DOT;
    if (!strncmp(code, "SHG", 3) || !strncmp(code, "RM", 2) || !strncmp(code, "GRL", 3) ||
        !strncmp(code, "RPG", 3))
        return BLACK_RETICLE_RING;
    return BLACK_RETICLE_CROSS;
}
#endif /* BLACK_RETICLE_SHAPE_INCLUDED */
