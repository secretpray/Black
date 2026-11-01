/* A real framebuffer clear/readback through NV2A's context implementation.
 * Checks host graphics only; it makes no claim about BLACK rendering. */
#include <SDL3/SDL.h>
#include <stdio.h>
#include "gloffscreen.h"

static GLuint probe_shader(GLenum type, const char *source)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL); glCompileShader(shader);
    GLint ok = 0; glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[2048]; glGetShaderInfoLog(shader, sizeof log, NULL, log); fprintf(stderr, "%s\n", log); return 0; }
    return shader;
}
static int vertex_color_probe(void)
{
#ifdef XBOXRECOMP_ANGLE
    const char *vertex = "#version 300 es\nprecision highp float; layout(location=0) in vec4 c; out vec4 color; void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2); gl_Position=vec4(p*2.0-1.0,0,1); color=c.bgra;}";
    const char *fragment = "#version 300 es\nprecision highp float; in vec4 color; out vec4 frag; void main(){frag=color;}";
#else
    const char *vertex = "#version 330\nlayout(location=0) in vec4 c; out vec4 color; void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2); gl_Position=vec4(p*2.0-1.0,0,1); color=c;}";
    const char *fragment = "#version 330\nin vec4 color; out vec4 frag; void main(){frag=color;}";
#endif
    GLuint vs = probe_shader(GL_VERTEX_SHADER, vertex), fs = probe_shader(GL_FRAGMENT_SHADER, fragment);
    if (!vs || !fs) return 0;
    GLuint program = glCreateProgram(); glAttachShader(program, vs); glAttachShader(program, fs); glLinkProgram(program);
    GLint linked = 0; glGetProgramiv(program, GL_LINK_STATUS, &linked); if (!linked) return 0;
    glUseProgram(program);
    uint32_t argb[3] = {0xffcc2211, 0xffcc2211, 0xffcc2211};
    GLuint vao, buffer; glGenVertexArrays(1, &vao); glBindVertexArray(vao);
    glGenBuffers(1, &buffer); glBindBuffer(GL_ARRAY_BUFFER, buffer); glBufferData(GL_ARRAY_BUFFER, sizeof argb, argb, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
#ifdef XBOXRECOMP_ANGLE
    glVertexAttribPointer(0, 4, GL_UNSIGNED_BYTE, GL_TRUE, 4, NULL);
#else
    glVertexAttribPointer(0, GL_BGRA, GL_UNSIGNED_BYTE, GL_TRUE, 4, NULL);
#endif
    glDrawArrays(GL_TRIANGLES, 0, 3);
    unsigned char pixel[4] = {0}; glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    GLenum error = glGetError();
    int ok = error == GL_NO_ERROR && pixel[0] == 0xcc && pixel[1] == 0x22 && pixel[2] == 0x11 && pixel[3] == 0xff;
    printf("D3DCOLOR vertex red/blue: %u %u %u %u, GL error 0x%x: %s\n", pixel[0],pixel[1],pixel[2],pixel[3],error,ok ? "PASS" : "FAIL");
    glDeleteBuffers(1,&buffer); glDeleteVertexArrays(1,&vao); glDeleteProgram(program); glDeleteShader(vs); glDeleteShader(fs);
    return ok;
}

int main(void)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL video: %s\n", SDL_GetError());
        return 1;
    }
    GloContext *context = glo_context_create();
    glo_set_current(context);
    printf("GL renderer: %s\nGL version: %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));
    /* Use a texture FBO: ANGLE's surfaceless context has no default buffer. */
    GLuint texture, framebuffer;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        return 2;
    glViewport(0, 0, 4, 4);
    glClearColor(0.25f, 0.5f, 0.75f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    unsigned char pixel[4] = {0};
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    GLenum error = glGetError();
    int ok = error == GL_NO_ERROR && pixel[0] >= 63 && pixel[0] <= 65 &&
             pixel[1] >= 127 && pixel[1] <= 129 && pixel[2] >= 190 && pixel[2] <= 192 && pixel[3] == 255;
    printf("Framebuffer readback: %u %u %u %u, GL error 0x%x: %s\n",
           pixel[0], pixel[1], pixel[2], pixel[3], error, ok ? "PASS" : "FAIL");
    ok = vertex_color_probe() && ok;
    glDeleteFramebuffers(1, &framebuffer);
    glDeleteTextures(1, &texture);
    glo_context_destroy(context);
    SDL_Quit();
    return ok ? 0 : 3;
}
