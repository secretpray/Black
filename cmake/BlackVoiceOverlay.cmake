# Keep the pinned MCPXAPUState ABI. Gain history belongs to this translation
# unit, reset by the same VOICE_ON path as filters/resampler history.
file(READ "${BLACK_MAC_RUNTIME}/src/apu/apu_vp.c" voice)
function(black_voice_replace anchor replacement)
    string(REPLACE "${anchor}" "" stripped "${voice}")
    string(LENGTH "${voice}" before)
    string(LENGTH "${stripped}" after)
    string(LENGTH "${anchor}" size)
    math(EXPR count "(${before}-${after})/${size}")
    if(NOT count EQUAL 1)
        message(FATAL_ERROR "Pinned VP anchor changed: ${anchor}")
    endif()
    string(REPLACE "${anchor}" "${replacement}" voice "${voice}")
    set(voice "${voice}" PARENT_SCOPE)
endfunction()
# Device fixes verified against xboxrecomp e0d2525c/aabebed0 and
# portingpete/xboxrecomp a40397f9. Keep the pinned state layout unchanged.
black_voice_replace("            d->regs[NV_PAPU_FECTL] |= NV_PAPU_FECTL_FETRAPREASON_REQUESTED;"
    "            d->regs[NV_PAPU_FECTL] |= NV_PAPU_FECTL_FETRAPREASON_REQUESTED;\n            /* One event per trap, never re-derived from FECTL on ack. */\n            qatomic_or(&d->regs[NV_PAPU_ISTS], NV_PAPU_ISTS_FETINTSTS);")
black_voice_replace("    if (cbo >= ebo) {" "    if (cbo > ebo) {")
# The descriptor encodes 1..32 blocks. Mono decoding of even two blocks
# already exceeds the old 130-sample array; allow the entire encoded range.
black_voice_replace("    uint32_t adpcm_block[36 * 2 / 4];\n    int16_t adpcm_decoded[65 * 2];"
    "    uint32_t adpcm_block[36 * 32 / 4];\n    int16_t adpcm_decoded[65 * 32 * 2];")
black_apu_extract_function(voice "void mcpx_apu_vp_frame(MCPXAPUState *d," vp_frame_baseline)
file(READ "${CMAKE_SOURCE_DIR}/ports/macos/overrides/apu_vp_frame.inc" vp_frame_fixed)
black_voice_replace("${vp_frame_baseline}" "${vp_frame_fixed}")
black_voice_replace("#include \"apu_state.h\""
    "#include \"apu_state.h\"\n#include \"voice_gain.h\"\n#include \"audio_stage_probe.h\"\nstatic BlackVoiceGain black_voice_gains[MCPX_HW_MAX_VOICES];")
black_voice_replace("    /* Get volume bins */"
    "    for (unsigned i = 0; i < NUM_SAMPLES_PER_FRAME; ++i)\n        for (unsigned ch = 0; ch < channels; ++ch) black_voice_stage(0, v, samples[i][ch]);\n    black_voice_stage_report();\n    /* Get volume bins */")
black_voice_replace("    /* Mix into bins */\n    int nonzero_count = 0;"
    "    if (black_audio_stages_enabled()) {\n        static unsigned logged;\n        if (logged < 100 && samples[0][0] != 0) {\n            ++logged;\n            fprintf(stderr, \"[VOICE-CONFIG] voice=%u channels=%u hrtf=%d voice_hr=%u hrtf_hr=%u bins=\", v, channels, g_config.audio.hrtf, voice_get_mask(d,v,NV_PAVS_VOICE_CFG_FMT,NV_PAVS_VOICE_CFG_FMT_HEADROOM), d->vp.hrtf_headroom);\n            for(unsigned b=0;b<8;++b) fprintf(stderr, \"%d:%u:%u,\", bin[b],vol[b],d->vp.submix_headroom[bin[b]]);\n            fputc('\\n',stderr);\n        }\n    }\n    /* Mix into bins */\n    int nonzero_count = 0;")
black_voice_replace("                samples[i][ch] = run_svf(filter, samples[i][ch]);"
    "                samples[i][ch] = run_svf(filter, samples[i][ch]);\n                black_voice_stage(1, v, samples[i][ch]);")
black_voice_replace("    assert(v < MCPX_HW_MAX_VOICES);\n    memset(&d->vp.filters[v].svf"
    "    assert(v < MCPX_HW_MAX_VOICES);\n    black_voice_gains[v].valid = 0;\n    memset(&d->vp.filters[v].svf")
black_voice_replace("    /* Mix into bins */\n    int nonzero_count = 0;"
    "    /* Mix into bins: target gain ramp from xboxrecomp v0.16.0. */\n    static int black_legacy_gain = -1;\n    if (black_legacy_gain < 0) black_legacy_gain = getenv(\"BLACK_LEGACY_VOICE_GAIN\") != NULL;\n    int nonzero_count = 0;")
black_voice_replace("        g *= attenuate(vol[b]) / hr;\n        for (int i = 0; i < NUM_SAMPLES_PER_FRAME; i++) {\n            float s = g * samples[i][b % channels];"
    "        g *= attenuate(vol[b]) / hr;\n        float start = black_voice_gain_begin(&black_voice_gains[v], b, g);\n        for (int i = 0; i < NUM_SAMPLES_PER_FRAME; i++) {\n            float gain = black_legacy_gain ? g : black_voice_gain_sample(start, g, i, NUM_SAMPLES_PER_FRAME);\n            float s = gain * samples[i][b % channels];")
include("${CMAKE_SOURCE_DIR}/cmake/BlackResampleOverlay.cmake")
include("${CMAKE_SOURCE_DIR}/cmake/BlackVoiceLockOverlay.cmake")
set(BLACK_APU_VOICE_OVERLAY "${CMAKE_BINARY_DIR}/runtime-overlay/apu_vp.c")
set(previous "")
if(EXISTS "${BLACK_APU_VOICE_OVERLAY}")
    file(READ "${BLACK_APU_VOICE_OVERLAY}" previous)
endif()
if(NOT previous STREQUAL voice)
    file(WRITE "${BLACK_APU_VOICE_OVERLAY}" "${voice}")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${BLACK_MAC_RUNTIME}/src/apu/apu_vp.c"
    "${CMAKE_SOURCE_DIR}/ports/macos/overrides/apu_vp_frame.inc")
