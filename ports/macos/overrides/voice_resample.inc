/* Block fetch and playback-timed completion adapted from
 * portingpete/xboxrecomp a40397f9baeceb6970fb3b484d93fe9d49925d91,
 * src/apu/apu_vp.c; xemu LGPL-2.0-or-later terms apply.
 * All added state is local to this translation unit, not the pinned ABI. */
static bool voice_resample_source(MCPXAPUState *d, uint16_t v, float out[2])
{
    BlackVoiceResample *s = &s_resample[v];
    if (s->pos == s->count) {
        if (s->source_done) return false;
        int count = voice_get_samples(d, v, s->buffer, NUM_SAMPLES_PER_FRAME);
        if (count <= 0) return false;
        s->pos = 0;
        s->count = (unsigned)count;
        s->fetched += count;
    }
    memcpy(out, s->buffer[s->pos++], sizeof s->current);
    return true;
}

static bool voice_resample_advance(MCPXAPUState *d, uint16_t v)
{
    BlackVoiceResample *s = &s_resample[v];
    while (s->phase >= 1.0) {
        s->phase -= 1.0;
        ++s->index;
        s->have_current = false;
        if (s->have_next) {
            memcpy(s->current, s->next, sizeof s->current);
            s->have_next = false;
        } else if (!voice_resample_source(d, v, s->current)) {
            if (s->source_done) voice_off(d, v);
            /* No next persistent SSL yet: keep the remaining phase until
             * supply resumes. Repeated starved calls must not advance it. */
            return false;
        }
        s->have_current = true;
    }
    return true;
}

static int voice_resample(MCPXAPUState *d, uint16_t v, float samples[][2],
                          int requested_num, float rate)
{
    BlackVoiceResample *s = &s_resample[v];
    if (requested_num <= 0 || !isfinite(rate) || rate <= 0 ||
        voice_get_mask(d, v, NV_PAVS_VOICE_PAR_STATE,
                       NV_PAVS_VOICE_PAR_STATE_PAUSED) ||
        !voice_get_mask(d, v, NV_PAVS_VOICE_PAR_STATE,
                        NV_PAVS_VOICE_PAR_STATE_ACTIVE_VOICE)) return -1;

    double step = 1.0 / (double)rate;
    int count = 0;
    if (!s->have_current) {
        if (!voice_resample_source(d, v, s->current)) {
            if (s->source_done) voice_off(d, v);
            return -1;
        }
        s->have_current = true;
    }
    while (count < requested_num) {
        if (!voice_resample_advance(d, v)) break;
        if (s->phase > 0 && !s->have_next)
            s->have_next = voice_resample_source(d, v, s->next);
        /* An unavailable next SSL must not shorten the current endpoint.
         * Holding it for the remaining duration lets its DONE release the
         * guest's buffer without needing data from the following buffer. */
        float fraction = (float)s->phase;
        for (unsigned ch = 0; ch < 2; ++ch) {
            float a = s->current[ch], b = s->have_next ? s->next[ch] : a;
            samples[count][ch] = a + (b - a) * fraction;
        }
        ++count;
        s->phase += step;
        double played = (double)s->index + s->phase;
        for (unsigned ssl = 0; ssl < MCPX_HW_SSLS_PER_VOICE; ++ssl) {
            if ((s->notify_pending & (1u << ssl)) &&
                played >= (double)s->notify_boundary[ssl]) {
                s->notify_pending &= ~(1u << ssl);
                set_notify_status(d, v, MCPX_HW_NOTIFIER_SSLA_DONE + ssl,
                                  NV1BA0_NOTIFICATION_STATUS_DONE_SUCCESS);
            }
        }
        if (s->source_done && played >= (double)s->fetched) {
            voice_off(d, v);
            break;
        }
    }
    return count ? count : -1;
}
