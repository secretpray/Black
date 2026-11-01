# Build-local checked instrumentation; pinned renderer sources stay intact.
function(black_trace_replace variable anchor replacement)
    string(REPLACE "${anchor}" "" stripped "${${variable}}")
    string(LENGTH "${${variable}}" before)
    string(LENGTH "${stripped}" after)
    string(LENGTH "${anchor}" size)
    math(EXPR count "(${before}-${after})/${size}")
    if(NOT count EQUAL 1)
        message(FATAL_ERROR "Graphics trace anchor changed: ${anchor}")
    endif()
    string(REPLACE "${anchor}" "${replacement}" edited "${${variable}}")
    set(${variable} "${edited}" PARENT_SCOPE)
endfunction()
set(trace_root "${BLACK_MAC_RUNTIME}/third_party/xemu_nv2a")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/graphics-overlay")
foreach(relative IN ITEMS nv2a_glue.c hw/xbox/nv2a/pgraph/gl/surface.c hw/xbox/nv2a/pgraph/gl/shaders.c hw/xbox/nv2a/pgraph/gl/texture.c)
    file(READ "${trace_root}/${relative}" content)
    if(NOT relative STREQUAL "nv2a_glue.c")
        foreach(local_header IN ITEMS debug.h renderer.h)
            string(REPLACE "#include \"${local_header}\""
                "#include \"${trace_root}/hw/xbox/nv2a/pgraph/gl/${local_header}\"" content "${content}")
        endforeach()
    endif()
    string(PREPEND content "#include \"frame_trace.h\"\n")
    get_filename_component(name "${relative}" NAME)
    if(name STREQUAL "nv2a_glue.c")
        black_trace_replace(content "    if (scale <= 1 || !xbox_Nv2aRegPeek)"
            "    if ((scale <= 1 && !getenv(\"BLACK_STABLE_FRAMES\")) || !xbox_Nv2aRegPeek)")
        black_trace_replace(content "static void complete_flip(NV2AState *d)\n{"
            "static void complete_flip(NV2AState *d)\n{\n    uint64_t black_flip_start = black_trace_ns();\n    unsigned black_draws = s_frame_draws;")
        black_trace_replace(content "    if (!no_dl)\n        pgraph_gl_download_dirty_surfaces(d);\n    capture_hires(d);"
            "    uint64_t black_download_start = black_trace_ns();\n    if (!no_dl)\n        pgraph_gl_download_dirty_surfaces(d);\n    uint64_t black_capture_start = black_trace_ns();\n    black_trace(BLACK_TRACE_DOWNLOAD, s_flips, black_download_start, black_capture_start, no_dl, black_draws);\n    capture_hires(d);\n    uint64_t black_flip_end = black_trace_ns();\n    black_trace(BLACK_TRACE_CAPTURE, s_flips, black_capture_start, black_flip_end, 0, 0);\n    black_trace(BLACK_TRACE_FLIP, s_flips, black_flip_start, black_flip_end, black_draws, 0);")
    elseif(name STREQUAL "surface.c")
        black_trace_replace(content "                                       uint8_t *pixels)\n{\n    PGRAPHState *pg = &d->pgraph;"
            "                                       uint8_t *pixels)\n{\n    uint64_t black_read_start = black_trace_ns();\n    PGRAPHState *pg = &d->pgraph;")
        black_trace_replace(content "    bind_current_surface(d);\n}\n\nstatic void surface_download(NV2AState *d, SurfaceBinding *surface, bool force)"
            "    bind_current_surface(d);\n    black_trace(BLACK_TRACE_READBACK, 0, black_read_start, black_trace_ns(), surface->width, surface->height);\n}\n\nstatic void surface_download(NV2AState *d, SurfaceBinding *surface, bool force)")
    elseif(name STREQUAL "texture.c")
        # Anisotropic filtering setting, at least the title's own level.
        black_trace_replace(content "        glTexParameterf(binding->gl_target, GL_TEXTURE_MAX_ANISOTROPY_EXT,\n                        max_anisotropy);"
            "        glTexParameterf(binding->gl_target, GL_TEXTURE_MAX_ANISOTROPY_EXT,\n                        black_anisotropy(max_anisotropy));")
        string(PREPEND content "#include \"texture_filter.h\"\n")
    else()
        black_trace_replace(content "void pgraph_gl_bind_shaders(PGRAPHState *pg)\n{"
            "void pgraph_gl_bind_shaders(PGRAPHState *pg)\n{\n#ifdef XBOXRECOMP_ANGLE\n    /* GLES has no GL_BGRA vertex size. The existing GLSL path must undo\n     * D3DCOLOR's BGRA byte order before both FF and programmable shaders. */\n    pg->swizzle_attrs = 0;\n    for (unsigned i = 0; i < NV2A_VERTEXSHADER_ATTRIBUTES; ++i)\n        if (pg->vertex_attributes[i].count &&\n            pg->vertex_attributes[i].format == NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_UB_D3D)\n            pg->swizzle_attrs |= 1u << i;\n#endif")
        black_trace_replace(content "    GLint compiled = 0;"
            "    uint64_t black_shader_start = black_trace_ns();\n    GLint compiled = 0;")
        black_trace_replace(content "    return shader;\n}"
            "    black_trace(BLACK_TRACE_SHADER, shader, black_shader_start, black_trace_ns(), gl_shader_type, 0);\n    return shader;\n}")
    endif()
    set(dest "${CMAKE_BINARY_DIR}/graphics-overlay/${name}")
    set(previous "")
    if(EXISTS "${dest}")
        file(READ "${dest}" previous)
    endif()
    if(NOT previous STREQUAL content)
        file(WRITE "${dest}" "${content}")
    endif()
    get_target_property(sources xemu_nv2a SOURCES)
    list(REMOVE_ITEM sources "${trace_root}/${relative}")
    set_property(TARGET xemu_nv2a PROPERTY SOURCES ${sources} "${dest}")
    get_filename_component(source_dir "${trace_root}/${relative}" DIRECTORY)
    target_include_directories(xemu_nv2a PRIVATE "${source_dir}" "${CMAKE_SOURCE_DIR}/ports/macos/overrides")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${trace_root}/${relative}")
endforeach()
target_link_libraries(xemu_nv2a PUBLIC black-port-ui)

file(READ "${BLACK_MAC_RUNTIME}/src/video/fb_present_posix.m" presenter)
black_trace_replace(presenter "            if (s_hr_texture && now_ms() - s_hr_up_ms < 250)\n                source = s_hr_texture;"
    "            if (s_hr_texture && (getenv(\"BLACK_STABLE_FRAMES\") || now_ms() - s_hr_up_ms < 250)) {\n                source = s_hr_texture;\n                uint64_t black_source_ns = black_trace_ns();\n                black_trace(BLACK_TRACE_SOURCE, (uint32_t)s_fb_frames_presented, black_source_ns, black_source_ns, seq, xnv2a_flip_count ? xnv2a_flip_count() : 0);\n            }")
black_trace_replace(presenter "        if (build_candidates(candidates, &candidate_count) > 0) {"
    "        if (!black_StableFrameAvailable() && build_candidates(candidates, &candidate_count) > 0) {")
black_trace_replace(presenter "if (getenv(\"RECOMP_BACKGROUND\") || getenv(\"RECOMP_FLOAT\"))"
    "if (getenv(\"RECOMP_FLOAT\"))")
black_trace_replace(presenter "            [s_window orderFrontRegardless];"
    "            [s_window orderFront:nil];")
black_trace_replace(presenter "        [s_window setTitle:@\"Black - Xbox Recomp Framebuffer\"];"
    "        [s_window setTitle:getenv(\"RECOMP_BACKGROUND\") ? @\"BLACK | Scripted diagnostics (controller disabled)\" : @\"BLACK\"];")
black_trace_replace(presenter "- (void)mouseDown:(NSEvent *)event\n{"
    "- (void)mouseDown:(NSEvent *)event\n{\n    if (getenv(\"RECOMP_BACKGROUND\")) return;")
black_trace_replace(presenter "- (void)rightMouseDown:(NSEvent *)event { xbox_MousePressButton(K_MOUSE_RIGHT); }"
    "- (void)rightMouseDown:(NSEvent *)event { if (!getenv(\"RECOMP_BACKGROUND\")) xbox_MousePressButton(K_MOUSE_RIGHT); }")
black_trace_replace(presenter "- (void)otherMouseDown:(NSEvent *)event\n{"
    "- (void)otherMouseDown:(NSEvent *)event\n{\n    if (getenv(\"RECOMP_BACKGROUND\")) return;")
file(READ "${CMAKE_SOURCE_DIR}/ports/macos/overrides/presenter_input.inc" input_methods)
set(input_start "/* Shift and Ctrl are modifiers: they produce flagsChanged, not keyDown.")
string(FIND "${presenter}" "${input_start}" input_begin)
if(input_begin EQUAL -1)
    message(FATAL_ERROR "Cocoa input methods anchor changed")
endif()
string(SUBSTRING "${presenter}" ${input_begin} -1 input_suffix)
string(FIND "${input_suffix}" "\n@end" input_end)
if(input_end EQUAL -1)
    message(FATAL_ERROR "Cocoa input view end changed")
endif()
math(EXPR input_end "${input_begin} + ${input_end}")
string(SUBSTRING "${presenter}" 0 ${input_begin} input_prefix)
string(SUBSTRING "${presenter}" ${input_end} -1 input_suffix)
set(presenter "${input_prefix}${input_methods}${input_suffix}")
string(PREPEND presenter "#include \"${CMAKE_SOURCE_DIR}/ports/macos/overrides/keyboard_mouse.h\"\n#include \"${CMAKE_SOURCE_DIR}/ports/macos/overrides/flash_menu.h\"\n")
black_trace_replace(presenter "    s_mouse_captured = on;"
    "    s_mouse_captured = on;\n    black_MouseCapture(on);")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/ports/macos/overrides/presenter_input.inc")
# RECOMP_AA: 0 off, 1 FXAA in the scaling pass, smaa three passes before it;
# RECOMP_UPSCALE=metalfx then brings the frame to the shown size.
black_trace_replace(presenter "        int on = !(aa && strcmp(aa, \"0\") == 0);"
    "        int on = !(aa && (strcmp(aa, \"0\") == 0 || strcmp(aa, \"smaa\") == 0));")
black_trace_replace(presenter "    command = [s_queue commandBuffer];\n    encoder = [command renderCommandEncoderWithDescriptor:pass];"
    "    command = [s_queue commandBuffer];\n    source = black_PostProcess(command, source, drawable);\n    encoder = [command renderCommandEncoderWithDescriptor:pass];")
black_trace_replace(presenter "static int update_pixels(void)\n{"
    "static int black_OriginalUpdatePixels(void)\n{")
black_trace_replace(presenter "static void render_frame(void)\n{"
    "static void black_OriginalRenderFrame(void)\n{")
black_trace_replace(presenter "    [command presentDrawable:drawable];"
    "    if (black_trace_enabled()) {\n        uint32_t black_present_id = (uint32_t)s_fb_frames_presented;\n        uint64_t black_submit_ns = black_trace_ns();\n        uint32_t black_flip_id = xnv2a_flip_count ? xnv2a_flip_count() : 0;\n        [drawable addPresentedHandler:^(id<MTLDrawable> shown) {\n            black_trace(BLACK_TRACE_DISPLAY, black_present_id, black_submit_ns, black_trace_ns(),\n                (uint64_t)(shown.presentedTime * 1000000000.0), black_flip_id);\n        }];\n    }\n    [command presentDrawable:drawable];")
set(presenter_dest "${CMAKE_BINARY_DIR}/graphics-overlay/fb_present_posix.m")
set(previous "")
if(EXISTS "${presenter_dest}")
    file(READ "${presenter_dest}" previous)
endif()
if(NOT previous STREQUAL presenter)
    file(WRITE "${presenter_dest}" "${presenter}")
endif()
target_compile_definitions(xbox_video PRIVATE BLACK_PRESENTER_SOURCE="${presenter_dest}")
file(READ "${BLACK_MAC_GAME}/src/main_posix.c" host)
black_trace_replace(host "xbox_mmio_intercept(APU_VA, APU_VP_SIZE, apu_region_read, apu_region_write,"
    "xbox_mmio_intercept(APU_VA, getenv(\"RECOMP_REAL_DSP\") ? 0x60000u : APU_VP_SIZE, apu_region_read, apu_region_write,")
set(host_dest "${CMAKE_BINARY_DIR}/runtime-overlay/main_posix.c")
set(previous "")
if(EXISTS "${host_dest}")
    file(READ "${host_dest}" previous)
endif()
if(NOT previous STREQUAL host)
    file(WRITE "${host_dest}" "${host}")
endif()
set(BLACK_HOST_OVERLAY "${host_dest}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${BLACK_MAC_RUNTIME}/src/video/fb_present_posix.m" "${BLACK_MAC_GAME}/src/main_posix.c")
