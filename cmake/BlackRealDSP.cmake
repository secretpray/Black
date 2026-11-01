# Optional real MCPX DSP, already present in the pinned runtime but not wired
# into its build. No writes to either pinned source tree.
option(BLACK_ENABLE_REAL_DSP "Build experimental DSP56300 JIT audio engine" OFF)
if(NOT BLACK_ENABLE_REAL_DSP)
    return()
endif()
function(black_dsp_write path content)
    set(previous "")
    if(EXISTS "${path}")
        file(READ "${path}" previous)
    endif()
    if(NOT previous STREQUAL content)
        file(WRITE "${path}" "${content}")
    endif()
endfunction()
set(dsp_root "${BLACK_MAC_RUNTIME}/third_party/dsp56300")
execute_process(COMMAND git -C "${dsp_root}" rev-parse HEAD
    OUTPUT_VARIABLE dsp_sha OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE dsp_git)
if(NOT dsp_git EQUAL 0 OR NOT dsp_sha STREQUAL "c60aeedb6a6014850204c71e53bc0cf5b3104222")
    message(FATAL_ERROR "Initialize pinned DSP: git -C ports/macos/runtime submodule update --init --depth 1 third_party/dsp56300")
endif()
find_program(BLACK_CARGO cargo REQUIRED)
set(dsp_archive "${CMAKE_SOURCE_DIR}/out/macos/dsp56300/release/libdsp56300_emu_ffi.a")
add_custom_target(black-dsp-rust
    COMMAND "${BLACK_CARGO}" build --locked --release -p dsp56300-emu-ffi
        --manifest-path "${dsp_root}/Cargo.toml"
        --target-dir "${CMAKE_SOURCE_DIR}/out/macos/dsp56300" --jobs 4
    BYPRODUCTS "${dsp_archive}" VERBATIM)
add_library(black-dsp STATIC
    "${BLACK_MAC_RUNTIME}/src/apu/dsp/dsp.c"
    "${BLACK_MAC_RUNTIME}/src/apu/dsp/dsp_dma.c"
    "${BLACK_MAC_RUNTIME}/src/apu/dsp/dsp_jit.c")
add_dependencies(black-dsp black-dsp-rust)
target_include_directories(black-dsp PRIVATE "${dsp_root}/crates/emu-ffi/include")
target_link_libraries(black-dsp PUBLIC "${dsp_archive}" "-framework Security")
file(READ "${BLACK_MAC_RUNTIME}/src/apu/apu_dsp_real.c" real)
string(REPLACE "#include \"fpconv.h\"" "#include \"fpconv.h\"\n#include \"dsp_headroom.h\"" real "${real}")
string(REPLACE "float_to_24b(mixbins[b][i])" "float_to_24b(mixbins[b][i] * black_dsp_input_gain())" real "${real}")
string(REPLACE "=int24_to_float(s);" "=int24_to_float(s) / black_dsp_input_gain();" real "${real}")
string(REPLACE "#include \"apu_state.h\"" "#include \"real_dsp_state.h\"\nfloat black_real_dsp_output[256][2];" real "${real}")
# State allocations store the real pointer in the unchanged opaque slots.
string(REPLACE "d->gp.dsp = dsp_init(d, gp_scratch_rw, gp_fifo_rw, true);"
    "BlackLegacyDSPState **gp_slot = &d->gp.dsp;\n    *gp_slot = (BlackLegacyDSPState *)dsp_init(d, gp_scratch_rw, gp_fifo_rw, true);" real "${real}")
string(REPLACE "d->ep.dsp = dsp_init(d, ep_scratch_rw, ep_fifo_rw, false);"
    "BlackLegacyDSPState **ep_slot = &d->ep.dsp;\n    *ep_slot = (BlackLegacyDSPState *)dsp_init(d, ep_scratch_rw, ep_fifo_rw, false);" real "${real}")
# Leave the new slot declarations intact while casting all DSP operations.
string(REPLACE "d->gp.dsp" "((DSPState *)d->gp.dsp)" real "${real}")
string(REPLACE "d->ep.dsp" "((DSPState *)d->ep.dsp)" real "${real}")
string(REPLACE "&((DSPState *)d->gp.dsp)" "&d->gp.dsp" real "${real}")
string(REPLACE "&((DSPState *)d->ep.dsp)" "&d->ep.dsp" real "${real}")
string(REPLACE "d->monitor.float_frame_buf" "black_real_dsp_output" real "${real}")
string(APPEND real "\nvoid black_real_dsp_reset(MCPXAPUState *d) { dsp_reset((DSPState *)d->gp.dsp); dsp_reset((DSPState *)d->ep.dsp); d->set_irq = false; }\n")
set(real_dest "${CMAKE_BINARY_DIR}/runtime-overlay/apu_dsp_real.c")
black_dsp_write("${real_dest}" "${real}")
target_sources(xbox_apu PRIVATE "${real_dest}")
target_compile_definitions(xbox_apu PRIVATE BLACK_HAVE_REAL_DSP=1)
target_include_directories(xbox_apu PRIVATE "${BLACK_MAC_RUNTIME}/src/apu")
target_link_libraries(xbox_apu PUBLIC black-dsp)
file(READ "${BLACK_MAC_RUNTIME}/src/kernel/xbox_memory_layout.c" memory)
string(REPLACE "    if (!g_dsp_ack_enabled || !g_mcpx_regs || g_apu_mmio_trapped)"
    "    if (getenv(\"RECOMP_REAL_DSP\") || !g_dsp_ack_enabled || !g_mcpx_regs || g_apu_mmio_trapped)" memory "${memory}")
set(memory_dest "${CMAKE_BINARY_DIR}/runtime-overlay/xbox_memory_layout.c")
black_dsp_write("${memory_dest}" "${memory}")
get_target_property(kernel_sources xbox_kernel SOURCES)
list(REMOVE_ITEM kernel_sources xbox_memory_layout.c)
set_property(TARGET xbox_kernel PROPERTY SOURCES ${kernel_sources} "${memory_dest}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${BLACK_MAC_RUNTIME}/src/apu/apu_dsp_real.c" "${BLACK_MAC_RUNTIME}/src/kernel/xbox_memory_layout.c")
