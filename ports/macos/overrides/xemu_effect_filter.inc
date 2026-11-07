/* BLACK's post-processing chain (glow, blur: 320x240, 160x120, 80x60, 64x32)
 * downsamples and blurs its own small render targets with bilinear taps
 * placed in their Xbox texels. At render scale N those targets are N times
 * larger, so each tap averages a 2x2 host block out of an NxN Xbox texel and
 * the taps skip the rest: the glow loses energy unevenly and shows a faint
 * grid. When such a target is copied into the texture a linear-filtered draw
 * samples (render_surface_to), average it over one Xbox texel instead: four
 * bilinear taps a quarter Xbox texel off the centre, the approach of
 * Spark198rus/black-pc's smoothed_rt. Exact box at 2x and 4x, close at 3x.
 *
 * Only colour targets at most half of BLACK's 640x480 back buffer, at a scale
 * above 1, sampled with linear minification and magnification. The 160x120
 * target that holds depth as colour is read with box minification and keeps
 * the exact copy, as do all full-size targets. BLACK_EFFECT_FILTER=off keeps
 * the stock copy for A/B.
 *
 * Included into the build-local copy of xemu's pgraph/gl/surface.c through
 * cmake/BlackGraphicsTrace.cmake, after xemu_surface_alias.inc. */

/* NV_PGRAPH_TEXFILTER0_MAG: 1 box (point), 2 tent (linear). */
#define BLACK_TEXFILTER0_MAG_TENT_LOD0 2

static GLuint black_box_prog;
static GLint black_box_tex, black_box_offset;
static GLint black_box_saved[4];
static bool black_box_active;
static int black_box_enabled = -1;

static bool black_box_wanted(PGRAPHState *pg, const SurfaceBinding *surface,
                             int texture_unit)
{
    if (black_box_enabled < 0) {
        const char *mode = getenv("BLACK_EFFECT_FILTER");
        black_box_enabled = !(mode && !strcmp(mode, "off"));
    }
    if (!black_box_enabled || pg->surface_scale_factor <= 1 || !surface->color ||
        surface->width * 2 > 640 || surface->height * 2 > 480) {
        return false;
    }
    uint32_t filter = pgraph_reg_r(pg, NV_PGRAPH_TEXFILTER0 + texture_unit * 4);
    unsigned int min = GET_MASK(filter, NV_PGRAPH_TEXFILTER0_MIN);
    unsigned int mag = GET_MASK(filter, NV_PGRAPH_TEXFILTER0_MAG);
    bool min_linear = min == NV_PGRAPH_TEXFILTER0_MIN_TENT_LOD0 ||
                      min == NV_PGRAPH_TEXFILTER0_MIN_TENT_NEARESTLOD ||
                      min == NV_PGRAPH_TEXFILTER0_MIN_TENT_TENT_LOD;
    return min_linear && mag == BLACK_TEXFILTER0_MAG_TENT_LOD0;
}

static void black_box_init(void)
{
    if (black_box_prog) {
        return;
    }
    const char *vs =
        "#version 330\n"
        "void main()\n"
        "{\n"
        "    float x = -1.0 + float((gl_VertexID & 1) << 2);\n"
        "    float y = -1.0 + float((gl_VertexID & 2) << 1);\n"
        "    gl_Position = vec4(x, y, 0, 1);\n"
        "}\n";
    const char *fs =
        "#version 330\n"
        "uniform sampler2D tex;\n"
        "uniform vec2 offset;\n"
        "layout(location = 0) out vec4 out_Color;\n"
        "void main()\n"
        "{\n"
        "    vec2 uv = gl_FragCoord.xy / vec2(textureSize(tex, 0).xy);\n"
        "    out_Color = 0.25 * (texture(tex, uv + offset) + texture(tex, uv - offset) +\n"
        "                        texture(tex, uv + vec2(offset.x, -offset.y)) +\n"
        "                        texture(tex, uv + vec2(-offset.x, offset.y)));\n"
        "}\n";
    black_box_prog = pgraph_gl_compile_shader(vs, fs);
    black_box_tex = glGetUniformLocation(black_box_prog, "tex");
    black_box_offset = glGetUniformLocation(black_box_prog, "offset");
}

/* The surface is bound to GL_TEXTURE_2D on texture_unit. Returns false to
 * leave the stock copy in place. */
static bool black_box_use_program(PGRAPHState *pg, SurfaceBinding *surface,
                                  int texture_unit)
{
    black_box_active = false;
    if (!black_box_wanted(pg, surface, texture_unit)) {
        return false;
    }
    black_box_init();
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &black_box_saved[0]);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &black_box_saved[1]);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, &black_box_saved[2]);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, &black_box_saved[3]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glUseProgram(black_box_prog);
    glProgramUniform1i(black_box_prog, black_box_tex, texture_unit);
    glProgramUniform2f(black_box_prog, black_box_offset,
                       0.25f / (float)surface->width, 0.25f / (float)surface->height);
    black_box_active = true;
    {
        static unsigned logged;
        if (logged < 8 && getenv("BLACK_EFFECT_FILTER_TRACE")) {
            ++logged;
            fprintf(stderr, "[EFFECT] box filter %ux%u at %ux, vram %08x\n",
                    surface->width, surface->height, pg->surface_scale_factor,
                    (unsigned)surface->vram_addr);
        }
    }
    return true;
}

static void black_box_after_draw(SurfaceBinding *surface)
{
    if (!black_box_active) {
        return;
    }
    black_box_active = false;
    glBindTexture(GL_TEXTURE_2D, surface->gl_buffer);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, black_box_saved[0]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, black_box_saved[1]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, black_box_saved[2]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, black_box_saved[3]);
}
