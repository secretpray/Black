# Included after the other VP changes. The baseline is the previous working
# overlay, so its failures isolate resampling from the earlier device fixes.
set(BLACK_APU_RESAMPLE_BASELINE "${CMAKE_BINARY_DIR}/runtime-overlay/apu_resample_baseline.c")
file(READ "${CMAKE_SOURCE_DIR}/ports/macos/overrides/apu_vp_frame.inc" resample_frame)
file(READ "${CMAKE_SOURCE_DIR}/ports/macos/overrides/apu_vp_frame_previous.inc" resample_previous_frame)
string(REPLACE "${resample_frame}" "${resample_previous_frame}" resample_baseline "${voice}")
file(WRITE "${BLACK_APU_RESAMPLE_BASELINE}" "${resample_baseline}")
black_voice_replace("#include \"apu_state.h\""
    "#include \"apu_state.h\"\n#include \"voice_resample_state.h\"")
black_voice_replace("static void voice_resample_reset(uint16_t v);" "")
# A stereo ADPCM block needs both 36-byte channel blocks. One raw block
# decodes only 25 stereo frames, less than the 64-frame source block index.
black_voice_replace("    if (adpcm) {\n        block_size = 36;"
    "    if (adpcm) {\n        if (samples_per_block < channels) return -1;\n        block_size = 36;")
black_voice_replace("    case NV1BA0_PIO_SET_VOICE_BUF_CBO:\n"
    "    case NV1BA0_PIO_SET_VOICE_BUF_CBO:\n        voice_resample_reset((uint16_t)d->regs[NV_PAPU_FECV]);\n")
black_voice_replace("void mcpx_apu_vp_reset(MCPXAPUState *d)\n{"
    "void mcpx_apu_vp_reset(MCPXAPUState *d)\n{\n    memset(s_resample, 0, sizeof s_resample);")
black_voice_replace("                d->vp.ssl[v].ssl_index = 0;\n                voice_off(d, (uint16_t)v);"
    "                d->vp.ssl[v].ssl_index = 0;\n                s_resample[v].source_done = true;")
# Empty persistent SSL has no new data to complete. Its previous DONE was
# already scheduled at the end of the supplied segment, exactly once.
black_voice_replace("            } else {\n                set_notify_status(d, v, MCPX_HW_NOTIFIER_SSLA_DONE +\n                                  d->vp.ssl[v].ssl_index,\n                                  NV1BA0_NOTIFICATION_STATUS_DONE_SUCCESS);\n            }"
    "            }")
black_voice_replace("                set_notify_status(d, v, MCPX_HW_NOTIFIER_SSLA_DONE + ssl_index,\n                                  NV1BA0_NOTIFICATION_STATUS_DONE_SUCCESS);"
    "                s_resample[v].notify_boundary[ssl_index] =\n                    s_resample[v].fetched + sample_count;\n                s_resample[v].notify_pending |= 1u << ssl_index;")
black_voice_replace("                cbo = ebo;\n                voice_off(d, (uint16_t)v);"
    "                cbo = ebo;\n                s_resample[v].source_done = true;")
# Replace the old TU-local state, scalar fetch and resampler as one checked
# section. New reset lives in voice_resample_state.h before voice_off.
set(resample_start "static struct {\n    double frac;")
string(FIND "${voice}" "${resample_start}" resample_pos)
if(resample_pos EQUAL -1)
    message(FATAL_ERROR "Pinned scalar resampler start changed")
endif()
black_apu_extract_function(voice "static int voice_resample(MCPXAPUState *d," resample_function)
string(FIND "${voice}" "${resample_function}" resample_function_pos)
string(LENGTH "${resample_function}" resample_function_len)
math(EXPR resample_len "${resample_function_pos} + ${resample_function_len} - ${resample_pos}")
string(SUBSTRING "${voice}" ${resample_pos} ${resample_len} resample_old)
black_voice_replace("${resample_old}" "#include \"voice_resample.inc\"\n")
# Already-produced samples in the final partial tick must still be mixed.
black_voice_replace("            if (!active) return;" "            if (!active) break;")
black_voice_replace("    int active = voice_get_mask(d, v, NV_PAVS_VOICE_PAR_STATE,\n                                NV_PAVS_VOICE_PAR_STATE_ACTIVE_VOICE);\n    if (!active) return;\n" "")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/ports/macos/overrides/voice_resample_state.h"
    "${CMAKE_SOURCE_DIR}/ports/macos/overrides/voice_resample.inc")
