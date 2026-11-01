/* Adapted from the pinned xemu VP loop and portingpete/xboxrecomp
 * a40397f9baeceb6970fb3b484d93fe9d49925d91; LGPL-2.0-or-later.
 * Copyright (c) 2012 espes, 2018-2019 Jannik Vogel,
 * 2019-2025 Matt Borgerson. See ports/macos/licenses/.
 */
void mcpx_apu_vp_frame(MCPXAPUState *d,
                        float mixbins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME])
{
    /* The ISR must consume the previous payload/cursors before another tick. */
    if ((qatomic_read(&d->regs[NV_PAPU_FECTL]) & NV_PAPU_FECTL_FEMETHMODE)
        == NV_PAPU_FECTL_FEMETHMODE_TRAPPED) return;

    bool idle_trap_pending = false;
    memset(d->vp.sample_buf, 0, sizeof(d->vp.sample_buf));

    for (int list = 0; list < 3; list++) {
        hwaddr top, current, next;
        top = voice_list_regs[list].top;
        current = voice_list_regs[list].current;
        next = voice_list_regs[list].next;

        uint32_t cursor = d->regs[top];
        if (!idle_trap_pending) d->regs[current] = cursor;

        for (int i = 0; cursor != 0xFFFF; i++) {
            if (i >= MCPX_HW_MAX_VOICES) {
                DPRINTF("Voice list contains invalid entry!\n");
                break;
            }

            uint16_t v = (uint16_t)cursor;
            uint32_t next_voice = voice_get_mask(d, v, NV_PAVS_VOICE_TAR_PITCH_LINK,
                               NV_PAVS_VOICE_TAR_PITCH_LINK_NEXT_VOICE_HANDLE);
            if (!idle_trap_pending) d->regs[next] = next_voice;

            if (!voice_get_mask(d, v, NV_PAVS_VOICE_PAR_STATE,
                                NV_PAVS_VOICE_PAR_STATE_ACTIVE_VOICE)) {
                if (!idle_trap_pending) {
                    fe_method(d, SE2FE_IDLE_VOICE, v);
                    idle_trap_pending =
                        (qatomic_read(&d->regs[NV_PAPU_FECTL]) &
                         NV_PAPU_FECTL_FEMETHMODE) ==
                        NV_PAPU_FECTL_FEMETHMODE_TRAPPED;
                }
            } else {
                /* Process voice directly (single-threaded) */
                voice_process(d, mixbins, d->vp.sample_buf, v, list);
                g_f7_apu_vp_voices_processed++;
            }
            /* Preserve the first idle voice and its unlink cursors for ISR,
             * while still mixing active voices in this and following lists. */
            cursor = next_voice;
            if (!idle_trap_pending) d->regs[current] = cursor;
        }
    }

    /* VP monitor output */
    if (d->monitor.point == MCPX_APU_DEBUG_MON_VP) {
        int16_t isamp[NUM_SAMPLES_PER_FRAME * 2];
        src_float_to_short_array((float *)d->vp.sample_buf, isamp,
                                 NUM_SAMPLES_PER_FRAME * 2);
        int off = (d->ep_frame_div % 8) * NUM_SAMPLES_PER_FRAME;
        for (int i = 0; i < NUM_SAMPLES_PER_FRAME; i++) {
            d->monitor.frame_buf[off + i][0] += isamp[2 * i];
            d->monitor.frame_buf[off + i][1] += isamp[2 * i + 1];
        }
        memset(d->vp.sample_buf, 0, sizeof(d->vp.sample_buf));
        memset(mixbins, 0, sizeof(float) * NUM_MIXBINS * NUM_SAMPLES_PER_FRAME);
    }
}
