/* Per-voice host state, outside the pinned MCPXAPUState ABI.
 * Playback/prefetch separation follows portingpete/xboxrecomp a40397f9,
 * src/apu/apu_vp.c. Original xemu LGPL-2.0-or-later terms apply. */
#pragma once
typedef struct {
    float buffer[NUM_SAMPLES_PER_FRAME][2];
    unsigned count, pos;
    double phase;
    float current[2], next[2];
    bool have_current, have_next, source_done;
    uint64_t fetched, index, notify_boundary[MCPX_HW_SSLS_PER_VOICE];
    unsigned notify_pending;
} BlackVoiceResample;
static BlackVoiceResample s_resample[MCPX_HW_MAX_VOICES];

static void voice_resample_reset(uint16_t v)
{
    if (v < MCPX_HW_MAX_VOICES) memset(&s_resample[v], 0, sizeof s_resample[v]);
}
