/* Widescreen 2D for BLACK in this runtime's native C ABI, after TGP's
 * src/game/hud.cpp (USA XBE addresses). With the widescreen video flag
 * (video_flags.h) the title draws its 3D anamorphic, but its own 2D layer is
 * still laid out at 640x480 for the whole output, so the presenter stretched
 * the HUD. Each screen-space point is narrowed to 4:3 and moved to the anchor
 * of its HUD component; films keep their shape. BLACK_STRETCH_HUD=1 leaves the
 * original 2D alone for A/B checks. */
#include "recomp_types.h"
#include "display_aspect.h"
#include "hud_layout.h"
#include "reticle_trace.h"
#include "reticle_shape.h"
#include "game_mods.h"
#include "port_menu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void black_Original2DBegin(void);
extern void black_Original2DEnd(void);
extern void black_OriginalQuads(void);
extern void black_OriginalSolidQuads(void);
extern void black_OriginalStrip(void);
extern void black_OriginalFilmDraw(void);
extern void black_OriginalListDraw(void);
void black_ModsPlay(void);

/* display: +0 width, +0xC widescreen, view width/aspect of the first view */
#define DISPLAY 0x2D67F4u
#define DISPLAY_WIDESCREEN 0x0Cu
#define DISPLAY_WIDTH 0xD428u
#define DISPLAY_VIEW_WIDTH 0xD4C0u
#define DISPLAY_ASPECT 0xD4C4u
#define STAGE_X 0x295DA4u        /* float: the 640 Flash stage's inset on a widescreen */
#define UI 0x2D199Cu
#define UI_STAGE_SCALE_X 0x3710u
#define D3D_DEVICE_PTR 0x27BFF8u /* g_pDevice; the viewport D3DDevice_SetViewport stores */
#define DEVICE_VIEWPORT_W 0xEE8u
/* HUD anchors: x, y share of the safe area per anchor id; middle anchors get a
 * taller frame on a widescreen */
#define ANCHORS 0x2AD968u
#define TALLER 0x2AD9BCu
/* return addresses */
#define FROM_HUD 0x0013C02Fu
#define FROM_FLASH 0x00121E82u
/* films */
#define GAME 0x2D45F0u
#define CURRENT_FILM 0x2042Cu
#define FILM_TABLE 0x29D9C0u
#define FILM_ENTRY 12u
#define FILM_BEHIND_MENU 8u
#define FILMS 27u
#define LOGO_FILM 1u

enum { FILM_NONE, FILM_SHOWN, FILM_LOGOS, FILM_MENU };

static float g_anchor = BLACK_HUD_OFF;
static float g_width = 640.0f;
static uint32_t g_hud;    /* the player's HUD while its list draws */
static float g_inset;     /* 640x480 pixels a sprite's uv is pulled in at each edge */
static int g_film = FILM_NONE;

static double aspect(void)
{
    static double value;
    if (value == 0.0) value = black_display_aspect(getenv("RECOMP_ASPECT"));
    return value;
}

static int wide(void) { return black_display_wide(aspect()); }

static int enabled(void)
{
    static int value = -1;
    if (value < 0) value = getenv("BLACK_STRETCH_HUD") == NULL;
    return value;
}

static int render_scale(void)
{
    const char *e = getenv("RECOMP_SCALE");
    int n = e ? atoi(e) : 1;
    return n < 1 ? 1 : n > 6 ? 6 : n;
}

/* The game takes the screen's shape once at boot; keep its flag, first view
 * and Flash stage on the output's shape. */
static void follow_output(void)
{
    uint32_t display = MEM32(DISPLAY), ui = MEM32(UI);
    if (!display) return;
    float narrow = black_hud_narrow(aspect());
    MEM8(display + DISPLAY_WIDESCREEN) = (uint8_t)wide();
    MEMF(display + DISPLAY_VIEW_WIDTH) = black_view_width(narrow);
    MEMF(display + DISPLAY_ASPECT) = (float)aspect();
    MEMF(STAGE_X) = black_stage_x(narrow);
    if (ui) MEMF(ui + UI_STAGE_SCALE_X) = (float)(int32_t)MEM32(display + DISPLAY_WIDTH) / 640.0f * narrow;
}

/* An emitter's points, eax: x, y pairs, each drawn at itself plus the float at
 * [[esp+4]]. Moved for the call and put back: strips share vertex copies. */
static void emit(uint32_t count, void (*original)(void))
{
    if (g_anchor < 0.0f || count == 0 || count > 4096) {
        original();
        return;
    }
    static float saved[4096];
    uint32_t points = g_eax;
    float offset = MEMF(MEM32(g_esp + 4));
    float narrow = black_hud_narrow(aspect());
    for (uint32_t i = 0; i < count; i++) {
        float x = MEMF(points + i * 8);
        saved[i] = x;
        MEMF(points + i * 8) = black_hud_x(x, offset, g_anchor, g_width, narrow);
    }
    original();
    for (uint32_t i = 0; i < count; i++) MEMF(points + i * 8) = saved[i];
}

/* An emitter's uv pairs at [esp+0xC] hold the game's own tables: put back. */
typedef struct { uint32_t uv; uint32_t floats; float saved[64]; } SavedUv;

static int save_uv(SavedUv *s, uint32_t pairs)
{
    s->uv = MEM32(g_esp + 0xC);
    s->floats = pairs * 2;
    if (!s->uv || s->floats > 64) return 0;
    for (uint32_t i = 0; i < s->floats; i++) s->saved[i] = MEMF(s->uv + i * 4);
    return 1;
}

static void restore_uv(const SavedUv *s)
{
    for (uint32_t i = 0; i < s->floats; i++) MEMF(s->uv + i * 4) = s->saved[i];
}

/* Two corners per quad: corner b's u and v lie across from corner a's. */
static void inset_quads(uint32_t xy, uint32_t uv, uint32_t quads)
{
    for (uint32_t q = 0; q < quads; q++, xy += 16, uv += 16) {
        float w = fabsf(MEMF(xy + 8) - MEMF(xy)), h = fabsf(MEMF(xy + 12) - MEMF(xy + 4));
        float u = MEMF(uv), v = MEMF(uv + 4), u2 = MEMF(uv + 8), v2 = MEMF(uv + 12);
        MEMF(uv) = black_pull_in(u, u2, w, g_inset);
        MEMF(uv + 4) = black_pull_in(v, v2, h, g_inset);
        MEMF(uv + 8) = black_pull_in(u2, u, w, g_inset);
        MEMF(uv + 12) = black_pull_in(v2, v, h, g_inset);
    }
}

/* A rotated 4 point strip: each point's u moves toward the point sharing its v,
 * and v toward the one sharing its u. */
static void inset_strip(uint32_t xy, uint32_t uv)
{
    float in[8], p[8];
    for (int i = 0; i < 8; i++) { in[i] = MEMF(uv + i * 4); p[i] = MEMF(xy + i * 4); }
    for (int i = 0; i < 4; i++) {
        for (int axis = 0; axis < 2; axis++) {
            int other = 1 - axis, j = -1;
            for (int k = 0; k < 4 && j < 0; k++)
                if (in[k * 2 + other] == in[i * 2 + other] && in[k * 2 + axis] != in[i * 2 + axis]) j = k;
            if (j < 0) continue;
            float length = hypotf(p[j * 2] - p[i * 2], p[j * 2 + 1] - p[i * 2 + 1]);
            MEMF(uv + (i * 2 + axis) * 4) = black_pull_in(in[i * 2 + axis], in[j * 2 + axis], length, g_inset);
        }
    }
}

static int current_film(void)
{
    uint32_t game = MEM32(GAME);
    uint32_t film = game ? MEM32(game + CURRENT_FILM) : FILMS;
    if (film < FILMS && MEM8(FILM_TABLE + film * FILM_ENTRY + FILM_BEHIND_MENU)) return FILM_MENU;
    return film == LOGO_FILM ? FILM_LOGOS : FILM_SHOWN;
}

/* Textured and solid quads, two corners each. The film quad covers the frame:
 * a menu's film stays stretched, no wider than 16:9; a film is shown 4:3
 * (BLACK_FMV_FILL=1 fills the width instead, top and bottom cut); the EA intro
 * is always 4:3, filling cuts its text. */
static void draw_quads(uint32_t quads);

/* The Mods page's Reticle Shape (reticle_shape.h): the aiming dot's quad takes
 * the chosen shape's atlas cell for this call and the glow's collapses; the
 * cross's cell is painted into the atlas when its texture is built
 * (hud_reticle.c). Everything is put back afterwards, as the game reuses its
 * tables. Batches of the HUD's size only; a film or a long text batch is left
 * alone. */
enum { RESHAPE_MAX_QUADS = 64 };

/* Guest memory the runtime maps: the title's RAM and its 0x80000000 window. */
static int guest_mapped(uint32_t a, uint32_t size)
{
    return (a < 0x04000000u && size <= 0x04000000u - a) ||
           (a >= 0x80000000u && a < 0x84000000u && size <= 0x84000000u - a);
}

/* Auto's shape for the weapon in the player's hands (USA layout, the same
 * reads the Mods page's ammunition makes): the world player at 0x2D45D4, its
 * actor at +0x30 holding the weapon at +0x29C, whose first byte is its type in
 * the weapon manager's table (0x2D12D0: table, entries at +4, count at +1,
 * 0x20 each with the name key first). Anything not there keeps the dot. */
static int auto_shape(void)
{
    static char last[13];
    uint32_t player = MEM32(0x2D45D4), weapon, manager, table, entries, entry;
    unsigned type, count;
    if (!player || !guest_mapped(player + 0x30 + 0x29C, 4)) return BLACK_RETICLE_DOT;
    weapon = MEM32(player + 0x30 + 0x29C);
    manager = MEM32(0x2D12D0);
    if (!weapon || !manager || !guest_mapped(weapon, 1) || !guest_mapped(manager, 4)) return BLACK_RETICLE_DOT;
    table = MEM32(manager);
    if (!table || !guest_mapped(table, 8)) return BLACK_RETICLE_DOT;
    type = MEM8(weapon);
    count = MEM8(table + 1);
    entries = MEM32(table + 4);
    if (type >= count || count > 64 || !entries || !guest_mapped(entries, count * 0x20u)) return BLACK_RETICLE_DOT;
    entry = entries + type * 0x20u;
    char name[13];
    black_reticle_weapon_name((unsigned long long)MEM32(entry) | (unsigned long long)MEM32(entry + 4) << 32, name);
    int shape = black_reticle_auto(name);
    if (strcmp(name, last)) {
        static const char *const names[BLACK_RETICLE_SHAPES] = {"dot", "ring", "ring and ticks", "cross",
                                                                "chevrons"};
        memcpy(last, name, sizeof last);
        fprintf(stderr, "[RETICLE] Auto: %s -> %s\n", name, names[shape]);
    }
    return shape;
}

void sub_000D6C00(void)
{
    uint32_t quads = MEM32(g_esp + 8);
    black_ReticleTraceQuads(quads);
    int shape = black_SettingsMod(BLACK_MOD_RETICLE);
    uint32_t uv_points = MEM32(g_esp + 0xC);
    if (shape == BLACK_RETICLE_AUTO && uv_points && quads && quads <= RESHAPE_MAX_QUADS) shape = auto_shape();
    if (shape <= BLACK_RETICLE_DOT || !uv_points || !quads || quads > RESHAPE_MAX_QUADS) {
        draw_quads(quads);
        return;
    }
    static float saved_xy[RESHAPE_MAX_QUADS][4], saved_uv[RESHAPE_MAX_QUADS][4];
    static uint32_t changed[RESHAPE_MAX_QUADS];
    unsigned count = 0;
    for (uint32_t q = 0; q < quads; ++q) {
        float xy[4], uv[4];
        for (int i = 0; i < 4; ++i) {
            xy[i] = MEMF(g_eax + q * 16 + i * 4);
            uv[i] = MEMF(uv_points + q * 16 + i * 4);
        }
        float before_xy[4], before_uv[4];
        memcpy(before_xy, xy, sizeof xy);
        memcpy(before_uv, uv, sizeof uv);
        if (!black_reticle_reshape(xy, uv, shape)) continue;
        memcpy(saved_xy[count], before_xy, sizeof before_xy);
        memcpy(saved_uv[count], before_uv, sizeof before_uv);
        changed[count++] = q;
        for (int i = 0; i < 4; ++i) {
            MEMF(g_eax + q * 16 + i * 4) = xy[i];
            MEMF(uv_points + q * 16 + i * 4) = uv[i];
        }
    }
    draw_quads(quads);
    for (unsigned c = 0; c < count; ++c)
        for (int i = 0; i < 4; ++i) {
            MEMF(g_eax + changed[c] * 16 + i * 4) = saved_xy[c][i];
            MEMF(uv_points + changed[c] * 16 + i * 4) = saved_uv[c][i];
        }
}

static void draw_quads(uint32_t quads)
{
    if (!enabled()) { black_OriginalQuads(); return; }
    if (g_film == FILM_NONE && g_inset > 0.0f) {
        SavedUv uv;
        if (save_uv(&uv, quads * 2)) {
            inset_quads(g_eax, uv.uv, quads);
            emit(quads * 2, black_OriginalQuads);
            restore_uv(&uv);
            return;
        }
    }
    if (g_film == FILM_NONE || !wide()) { emit(quads * 2, black_OriginalQuads); return; }
    float q[4], saved[4];
    for (int i = 0; i < 4; i++) q[i] = saved[i] = MEMF(g_eax + i * 4);
    float narrow = black_hud_narrow(aspect());
    if (g_film == FILM_MENU) {
        float scale = (float)(16.0 / 9.0 / aspect());
        black_scale_quad(q, 0, scale < 1.0f ? scale : 1.0f);
    } else if (g_film == FILM_SHOWN && getenv("BLACK_FMV_FILL")) {
        black_scale_quad(q, 1, 1.0f / narrow);
    } else {
        black_scale_quad(q, 0, narrow);
    }
    uint32_t points = g_eax;
    for (int i = 0; i < 4; i++) MEMF(points + i * 4) = q[i];
    black_OriginalQuads();
    for (int i = 0; i < 4; i++) MEMF(points + i * 4) = saved[i];
}

void sub_000D6AA0(void)
{
    if (!enabled()) { black_OriginalSolidQuads(); return; }
    emit(MEM32(g_esp + 8) * 2, black_OriginalSolidQuads);
}

/* Textured strip */
void sub_000D6850(void)
{
    uint32_t points = MEM32(g_esp + 8);
    black_ReticleTraceStrip(points);
    if (!enabled()) { black_OriginalStrip(); return; }
    SavedUv uv;
    if (points != 4 || g_inset <= 0.0f || !save_uv(&uv, points)) {
        emit(points, black_OriginalStrip);
        return;
    }
    inset_strip(g_eax, uv.uv);
    emit(points, black_OriginalStrip);
    restore_uv(&uv);
}

void sub_001217C0(void)
{
    g_film = current_film();
    black_OriginalFilmDraw();
    g_film = FILM_NONE;
}

/* Begin. Left alone: the Flash player's, a smaller target's. */
void sub_000D99A0(void)
{
    /* Mods page player values: every frame on the game thread, before its
     * 2D layer; black_ModsPlay itself only acts in 3D play. */
    black_ModsPlay();
    black_ReticleTrace2DBegin();
    if (!enabled()) { black_Original2DBegin(); return; }
    uint32_t from = MEM32(g_esp), display = MEM32(DISPLAY);
    follow_output();
    black_Original2DBegin();
    uint32_t device = MEM32(D3D_DEVICE_PTR);
    uint32_t width = device ? MEM32(device + DEVICE_VIEWPORT_W) : 0;
    int native = from != FROM_FLASH;
    int screen = display && width && width == MEM32(display) && native;
    g_anchor = screen && wide() ? BLACK_HUD_CENTRE : BLACK_HUD_OFF;
    g_width = width ? (float)width : 640.0f;
    g_inset = native ? black_hud_inset(render_scale()) : 0.0f;
}

/* End */
void sub_000D3740(void)
{
    g_anchor = BLACK_HUD_OFF;
    g_inset = 0.0f;
    black_Original2DEnd();
}

/* List draw, ecx the list */
void sub_000DA4B0(void)
{
    uint32_t hud = MEM32(g_esp) == FROM_HUD ? g_ecx - 0x40 : 0;
    g_hud = hud;
    black_OriginalListDraw();
    g_hud = 0;
}

/* A list's item before it draws (0x2139D), ebx: anchor the coming points at
 * its HUD component, and give a widescreen's taller middle frames height 1. */
void black_HudItem(void)
{
    if (!enabled()) return;
    uint32_t frame = MEM32(g_ebx + 0x40);
    float anchor = BLACK_HUD_CENTRE;
    for (uint32_t c = g_hud ? MEM32(g_hud + 0xA0) : 0; c; c = MEM32(c + 8)) {
        if (MEM32(c + 4) != frame) continue;
        anchor = MEMF(ANCHORS + MEM32(c + 0x10) * 8);
        if (MEMF(frame + 0xC) == MEMF(TALLER)) MEMF(frame + 0xC) = 1.0f;
    }
    if (g_anchor != BLACK_HUD_OFF) g_anchor = anchor;
}
