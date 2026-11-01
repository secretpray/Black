/* Preserve DSP command acknowledgements and frame accumulation; replace only
 * the all-even/all-odd dry sum with BLACK's documented speaker routing.
 * License: same as included runtime, with routing adapted from root TGP. */
#define mcpx_apu_dsp_frame black_OriginalDSPFrame
#define mcpx_apu_dsp_init black_OriginalDSPInit
#include "../runtime/src/apu/apu_dsp.c"
#undef mcpx_apu_dsp_frame
#undef mcpx_apu_dsp_init
#include "black_audio_mix.h"
#include "audio_probe.h"
#include "audio_stage_probe.h"
#include "dsp_headroom.h"
#include "../runtime/src/apu/apu_limiter.h"
static ApuOutputLimiter black_output_limiter;
static float black_output_mix[256][2];
#ifdef BLACK_HAVE_REAL_DSP
#include "apu_dsp_real.h"
extern float black_real_dsp_output[256][2];
#endif

void mcpx_apu_dsp_init(MCPXAPUState *d)
{
    memset(&black_output_limiter, 0, sizeof black_output_limiter);
#ifdef BLACK_HAVE_REAL_DSP
    if (mcpx_apu_real_dsp_enabled()) { mcpx_apu_real_dsp_init(d); return; }
#endif
#ifndef BLACK_HAVE_REAL_DSP
    if (getenv("RECOMP_REAL_DSP")) {
        fprintf(stderr, "[APU] DSP engine was not built; use black.py build --real-dsp\n");
        exit(1);
    }
#endif
    black_OriginalDSPInit(d);
}

void mcpx_apu_dsp_frame(MCPXAPUState *d,
    float mixbins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME])
{
    black_bins_probe(mixbins, black_dsp_input_gain());
#ifdef BLACK_HAVE_REAL_DSP
    if (mcpx_apu_real_dsp_enabled()) {
        g_f7_apu_dsp_frames_processed++;
        int off = (d->ep_frame_div % 8) * NUM_SAMPLES_PER_FRAME;
        /* A reset/stopped GP writes nothing. Publish silence for this slice
         * rather than replay old float output or invoke all-bin dry fallback. */
        memset(black_real_dsp_output + off, 0,
               NUM_SAMPLES_PER_FRAME * sizeof black_real_dsp_output[0]);
        mcpx_apu_real_dsp_frame(d, mixbins);
        d->monitor.dsp_wrote_frame = true;
        black_audio_probe(black_real_dsp_output + off, NUM_SAMPLES_PER_FRAME);
        for (unsigned i = 0; i < NUM_SAMPLES_PER_FRAME; ++i)
            for (unsigned ch = 0; ch < 2; ++ch) {
                float value = black_real_dsp_output[off + i][ch];
                black_output_mix[off + i][ch] = value;
                if (getenv("BLACK_LEGACY_AUDIO_CLIP"))
                    d->monitor.frame_buf[off + i][ch] = (int16_t)(fmaxf(-1, fminf(1, value)) * 32767);
            }
        if (!getenv("BLACK_LEGACY_AUDIO_CLIP") && off == 256 - NUM_SAMPLES_PER_FRAME)
            apu_limit_pcm(&black_output_limiter, black_output_mix, d->monitor.frame_buf);
        return;
    }
#endif
    if (getenv("BLACK_LEGACY_AUDIO_ROUTING") || d->monitor.point == MCPX_APU_DEBUG_MON_VP) {
        black_OriginalDSPFrame(d, mixbins);
        return;
    }
    g_f7_apu_dsp_frames_processed++;
    dsp_ack_frame(d);
    d->monitor.dsp_wrote_frame = true;
    int off = (d->ep_frame_div % 8) * NUM_SAMPLES_PER_FRAME;
    float raw[NUM_SAMPLES_PER_FRAME][2];
    for (unsigned i = 0; i < NUM_SAMPLES_PER_FRAME; ++i) {
        float bins[32], left, right;
        for (unsigned b = 0; b < 32; ++b) bins[b] = mixbins[b][i];
        black_mix_stereo(bins, &left, &right);
        raw[i][0] = left; raw[i][1] = right;
        black_output_mix[off + i][0] = left;
        black_output_mix[off + i][1] = right;
        if (getenv("BLACK_LEGACY_AUDIO_CLIP")) {
            left = fmaxf(-1.0f, fminf(1.0f, left));
            right = fmaxf(-1.0f, fminf(1.0f, right));
            d->monitor.frame_buf[off + i][0] = (int16_t)(left * 32767.0f);
            d->monitor.frame_buf[off + i][1] = (int16_t)(right * 32767.0f);
        }
    }
    black_audio_probe(raw, NUM_SAMPLES_PER_FRAME);
    if (!getenv("BLACK_LEGACY_AUDIO_CLIP") && off == 256 - NUM_SAMPLES_PER_FRAME)
        apu_limit_pcm(&black_output_limiter, black_output_mix, d->monitor.frame_buf);
}
