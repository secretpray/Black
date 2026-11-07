/* BLACK reuses render-target memory under another format: it reads the
 * 640x480 Z24S8 depth buffer through an A8R8G8B8 texture header, writes
 * downsampled depth as colour into small A8R8G8B8 targets and then binds that
 * same memory as the depth buffer of its light-cone passes. Stock xemu has no
 * GPU path for either alias, so every frame each one went through guest RAM:
 * glReadPixels of the source, a new GL texture and glTexImage2D back -- at the
 * render scale -- which held some areas at 15-18 fps.
 *
 * On the Xbox a Z24S8 texel is the dword (Z << 8) | S. Read as A8R8G8B8 it is
 * A = Z[23:16], R = Z[15:8], G = Z[7:0], B = S. Both directions convert that
 * layout on the GPU at the current scale. Stencil is not carried (B = 0 and a
 * cleared stencil): the light-cone passes test depth only.
 *
 * Included into the build-local copy of xemu's pgraph/gl/surface.c through
 * cmake/BlackGraphicsTrace.cmake; the pinned renderer sources stay intact. */

typedef struct BlackSurfaceAlias {
    GLuint texture;   /* the previous surface's GL texture, 0 when none */
    bool from_color;  /* it held colour (A8R8G8B8) rather than Z24S8 */
} BlackSurfaceAlias;

static GLuint black_alias_z2c_prog, black_alias_c2z_prog;
static GLint black_alias_z2c_tex, black_alias_c2z_tex;
static GLuint black_alias_fbo;
static int black_alias_enabled = -1;

static bool black_alias_on(void)
{
    if (black_alias_enabled < 0)
        black_alias_enabled = getenv("BLACK_SURFACE_ALIAS_OFF") == NULL;
    return black_alias_enabled;
}

static bool black_alias_depth(const SurfaceBinding *s)
{
    return !s->color && s->shape.z_format == 0 &&
           s->fmt.gl_internal_format == GL_DEPTH24_STENCIL8;
}

static bool black_alias_argb_surface(const SurfaceBinding *s)
{
    return s->color && s->fmt.gl_internal_format == GL_RGBA8 &&
           (s->shape.color_format == NV097_SET_SURFACE_FORMAT_COLOR_LE_A8R8G8B8 ||
            s->shape.color_format == NV097_SET_SURFACE_FORMAT_COLOR_LE_X8R8G8B8_Z8R8G8B8);
}

/* A Z24S8 render target sampled as a same-sized 32-bit ARGB texture. */
static bool black_alias_zeta_texture_ok(const SurfaceBinding *surface,
                                        const TextureShape *shape)
{
    if (!black_alias_on() || !black_alias_depth(surface) || shape->cubemap ||
        shape->levels > 1 || shape->width != surface->width ||
        shape->height != surface->height) {
        return false;
    }
    switch (shape->color_format) {
    case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8R8G8B8:
    case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8R8G8B8:
    case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_X8R8G8B8:
    case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_X8R8G8B8:
        return true;
    default:
        return false;
    }
}

static void black_alias_init(void)
{
    if (black_alias_z2c_prog) {
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
    const char *z2c =
        "#version 330\n"
        "uniform sampler2D tex;\n"
        "layout(location = 0) out vec4 out_Color;\n"
        "void main()\n"
        "{\n"
        "    float d = texelFetch(tex, ivec2(gl_FragCoord.xy), 0).r;\n"
        "    uint z = min(uint(d * 16777215.0 + 0.5), 16777215u);\n"
        "    out_Color = vec4(float((z >> 8) & 255u), float(z & 255u), 0.0,\n"
        "                     float((z >> 16) & 255u)) / 255.0;\n"
        "}\n";
    const char *c2z =
        "#version 330\n"
        "uniform sampler2D tex;\n"
        "void main()\n"
        "{\n"
        "    vec4 c = texelFetch(tex, ivec2(gl_FragCoord.xy), 0);\n"
        "    uint z = (uint(c.a * 255.0 + 0.5) << 16) |\n"
        "             (uint(c.r * 255.0 + 0.5) << 8) | uint(c.g * 255.0 + 0.5);\n"
        "    gl_FragDepth = float(z) / 16777215.0;\n"
        "}\n";
    black_alias_z2c_prog = pgraph_gl_compile_shader(vs, z2c);
    black_alias_z2c_tex = glGetUniformLocation(black_alias_z2c_prog, "tex");
    black_alias_c2z_prog = pgraph_gl_compile_shader(vs, c2z);
    black_alias_c2z_tex = glGetUniformLocation(black_alias_c2z_prog, "tex");
    glGenFramebuffers(1, &black_alias_fbo);
}

/* Sample a depth texture's depth exactly: no comparison, no filtering. */
static void black_alias_depth_sampling(GLuint texture, bool exact)
{
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    exact ? GL_NEAREST : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                    exact ? GL_NEAREST : GL_LINEAR);
}

/* render_surface_to's program choice: the stock copy for colour (or the
 * effect box filter, xemu_effect_filter.inc), the depth unpacking one for a
 * Z24S8 surface (already bound on texture_unit). */
static void black_alias_use_program(PGRAPHState *pg, PGRAPHGLState *r,
                                    SurfaceBinding *surface,
                                    int texture_unit, unsigned int width,
                                    unsigned int height)
{
    if (surface->color && black_box_use_program(pg, surface, texture_unit)) {
        return;
    }
    if (surface->color) {
        glUseProgram(r->s2t_rndr.prog);
        glProgramUniform1i(r->s2t_rndr.prog, r->s2t_rndr.tex_loc,
                           texture_unit);
        glProgramUniform2f(r->s2t_rndr.prog, r->s2t_rndr.surface_size_loc,
                           width, height);
        return;
    }
    black_alias_init();
    black_alias_depth_sampling(surface->gl_buffer, true);
    glUseProgram(black_alias_z2c_prog);
    glProgramUniform1i(black_alias_z2c_prog, black_alias_z2c_tex,
                       texture_unit);
}

static void black_alias_after_draw(SurfaceBinding *surface)
{
    black_box_after_draw(surface);
    if (!surface->color) {
        black_alias_depth_sampling(surface->gl_buffer, false);
    }
}

/* update_surface_part: the target at this address changes between colour
 * A8R8G8B8 and Z24S8 of the same size. Keep the old texture for a GPU
 * conversion instead of reading it back; the caller then creates the new
 * surface and calls black_alias_finish. */
static bool black_alias_take(NV2AState *d, SurfaceBinding *found,
                             SurfaceBinding *entry, bool color,
                             BlackSurfaceAlias *alias)
{
    alias->texture = 0;
    if (!black_alias_on() || found->color == color ||
        found->width != entry->width || found->height != entry->height ||
        found->swizzle || entry->swizzle || found->upload_pending) {
        return false;
    }
    bool pair = color ? (black_alias_depth(found) && black_alias_argb_surface(entry))
                      : (black_alias_argb_surface(found) && black_alias_depth(entry));
    if (!pair) {
        return false;
    }
    alias->texture = found->gl_buffer;
    alias->from_color = found->color;
    /* The texture now belongs to the alias; invalidating frees the binding. */
    found->gl_buffer = 0;
    found->draw_dirty = false;
    pgraph_gl_surface_invalidate(d, found);
    return true;
}

static void black_alias_finish(NV2AState *d, SurfaceBinding *surface,
                               BlackSurfaceAlias *alias)
{
    if (!alias->texture) {
        return;
    }
    PGRAPHState *pg = &d->pgraph;
    PGRAPHGLState *r = pg->gl_renderer_state;
    unsigned int width = surface->width ? surface->width : 1;
    unsigned int height = surface->height ? surface->height : 1;
    pgraph_apply_scaling_factor(pg, &width, &height);

    black_alias_init();
    glBindFramebuffer(GL_FRAMEBUFFER, black_alias_fbo);
    glActiveTexture(GL_TEXTURE0);
    glViewport(0, 0, width, height);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DITHER);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glBindVertexArray(r->s2t_rndr.vao);
    glBindBuffer(GL_ARRAY_BUFFER, r->s2t_rndr.vbo);

    if (alias->from_color) {
        /* colour bytes -> depth */
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                               GL_TEXTURE_2D, surface->gl_buffer, 0);
        GLenum none = GL_NONE;
        glDrawBuffers(1, &none);
        glBindTexture(GL_TEXTURE_2D, alias->texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glColorMask(false, false, false, false);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_ALWAYS);
        glDepthMask(true);
        glStencilMask(0xFF);
        glClearStencil(0);
        glClear(GL_STENCIL_BUFFER_BIT);
        glUseProgram(black_alias_c2z_prog);
        glProgramUniform1i(black_alias_c2z_prog, black_alias_c2z_tex, 0);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                               GL_TEXTURE_2D, 0, 0);
        glColorMask(true, true, true, true);
    } else {
        /* depth -> colour bytes */
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, surface->gl_buffer, 0);
        GLenum color0 = GL_COLOR_ATTACHMENT0;
        glDrawBuffers(1, &color0);
        black_alias_depth_sampling(alias->texture, true);
        glColorMask(true, true, true, true);
        glDisable(GL_DEPTH_TEST);
        glUseProgram(black_alias_z2c_prog);
        glProgramUniform1i(black_alias_z2c_prog, black_alias_z2c_tex, 0);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, 0, 0);
    }

    glDeleteTextures(1, &alias->texture);
    alias->texture = 0;
    glBindFramebuffer(GL_FRAMEBUFFER, r->gl_framebuffer);
    glBindVertexArray(r->gl_vertex_array);
    glUseProgram(r->shader_binding ? r->shader_binding->gl_program : 0);

    /* The GPU copy is the current contents; guest RAM catches up only when
     * something needs it (download_if_dirty). */
    surface->upload_pending = false;
    surface->draw_dirty = true;
    surface->cleared = false;
    surface->draw_time = pg->draw_time;
    static unsigned logged;
    if (logged++ < 8) {
        fprintf(stderr, "[SURF-ALIAS] %s at %08X %ux%u converted on the GPU\n",
                alias->from_color ? "colour->depth" : "depth->colour",
                (unsigned)surface->vram_addr, surface->width, surface->height);
    }
}
