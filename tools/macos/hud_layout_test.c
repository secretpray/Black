#include "../../ports/macos/overrides/display_aspect.h"
#include "../../ports/macos/overrides/hud_layout.h"
#include <math.h>
static int near(float a, float b) { return fabsf(a - b) < 1e-3f; }
int main(void)
{
    float k = black_hud_narrow(black_display_aspect("16:9"));
    if (!near(k, 0.75f)) return 1;
    /* The game's own values on 16:9: view width 1.2, stage inset 106.67. */
    if (!near(black_view_width(k), 1.2f)) return 2;
    if (!near(black_stage_x(k), 106.6667f)) return 3;
    /* 4:3 changes nothing. */
    float square = black_hud_narrow(4.0 / 3.0);
    if (!near(black_view_width(square), 1.0f) || !near(black_stage_x(square), 0.0f)) return 4;
    if (!near(black_hud_x(100.0f, 0.0f, BLACK_HUD_CENTRE, 640.0f, square), 100.0f)) return 5;
    /* Left anchored: x shrinks toward 0; right anchored: toward 640. */
    if (!near(black_hud_x(40.0f, 0.0f, 0.0f, 640.0f, k), 30.0f)) return 6;
    if (!near(black_hud_x(600.0f, 0.0f, 1.0f, 640.0f, k), 610.0f)) return 7;
    /* Centred: the middle stays, a quarter moves in. */
    if (!near(black_hud_x(320.0f, 0.0f, BLACK_HUD_CENTRE, 640.0f, k), 320.0f)) return 8;
    if (!near(black_hud_x(160.0f, 0.0f, BLACK_HUD_CENTRE, 640.0f, k), 200.0f)) return 9;
    /* Centred full-screen edges stay put; the offset is honoured. */
    if (!near(black_hud_x(0.0f, 0.0f, BLACK_HUD_CENTRE, 640.0f, k), 0.0f)) return 10;
    if (!near(black_hud_x(640.0f, 0.0f, BLACK_HUD_CENTRE, 640.0f, k), 640.0f)) return 11;
    if (!near(black_hud_x(150.0f, 10.0f, BLACK_HUD_CENTRE, 640.0f, k), 190.0f)) return 12;
    /* Ultrawide keeps the 16:9 height of view. */
    float ultra = black_hud_narrow(21.0 / 9.0);
    if (!near(black_view_width(ultra), 0.9f / ultra)) return 13;
    /* uv inset: none at 1x, toward half a texel when upscaled. */
    if (!near(black_hud_inset(1), 0.0f) || !near(black_hud_inset(2), 0.25f)) return 14;
    float q[4] = {0.0f, 0.0f, 640.0f, 480.0f};
    black_scale_quad(q, 0, k);
    if (!near(q[0], 80.0f) || !near(q[2], 560.0f) || !near(q[1], 0.0f)) return 15;
    return 0;
}
