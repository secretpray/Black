/* FE gate/epoch preservation adapted from portingpete/xboxrecomp
 * a40397f9baeceb6970fb3b484d93fe9d49925d91 apu_core.c. Keep the pinned
 * Mac VP/DSP ABI. Original xemu LGPL-2.0-or-later terms apply. */
static bool black_apu_active(MCPXAPUState *d)
{
    int mode = GET_MASK(qatomic_read(&d->regs[NV_PAPU_SECTL]), NV_PAPU_SECTL_XCNTMODE);
    uint32_t fe = qatomic_read(&d->regs[NV_PAPU_FECTL]);
    return mode != NV_PAPU_SECTL_XCNTMODE_OFF &&
        !(fe & (NV_PAPU_FECTL_FEMETHMODE_TRAPPED | NV_PAPU_FECTL_FEMETHMODE_HALTED));
}

static void *mcpx_apu_frame_thread(void *arg)
{
    MCPXAPUState *d = MCPX_APU_DEVICE(arg);
    qemu_mutex_lock(&d->lock);
    uint64_t gated = 0;
    while (!qatomic_read(&d->exiting)) {
        if (d->pause_requested && !g_test_tone.active && !g_mixer_active_count) {
            d->is_idle = true;
            qemu_cond_signal(&d->idle_cond);
            qemu_cond_wait(&d->cond, &d->lock);
            d->is_idle = false;
            continue;
        }
        /* A trapped/halted VP needs the guest ISR, not another copy of the
         * last PCM packet. Retain all completed slices of this 8-tick epoch
         * and release the APU lock so the guest can service the trap. */
        if (!black_apu_active(d) && !g_test_tone.active && !g_mixer_active_count) {
            if (d->set_irq) { d->set_irq = false; update_irq(d); }
            ++gated;
            if (getenv("RECOMP_AUDIO_DIAG") && (gated <= 8 || gated % 1000 == 0))
                fprintf(stderr, "[APU-GATE] waits=%llu epoch_slice=%u fe=%08X; PCM retained\n",
                    (unsigned long long)gated, d->ep_frame_div % 8, d->regs[NV_PAPU_FECTL]);
            qemu_cond_timedwait(&d->cond, &d->lock, 1);
            continue;
        }
        throttle(d);
        if (qatomic_read(&d->exiting) ||
            (d->pause_requested && !g_test_tone.active && !g_mixer_active_count)) continue;
        /* throttle releases the lock: control may have changed meanwhile. */
        bool active = black_apu_active(d);
        if (!active && !g_test_tone.active && !g_mixer_active_count) continue;
        if (active && !g_test_tone.active) {
            se_frame(d);
            if (d->set_irq) { d->set_irq = false; update_irq(d); }
        } else {
            /* Explicit host mixer/test-tone output remains allowed while OFF.
             * It owns only this slice; never replay an old native epoch. */
            unsigned off = (d->ep_frame_div % 8) * NUM_SAMPLES_PER_FRAME;
            memset(d->monitor.frame_buf + off, 0,
                NUM_SAMPLES_PER_FRAME * sizeof d->monitor.frame_buf[0]);
            d->monitor.dsp_wrote_frame = false;
            mcpx_apu_monitor_frame(d, NULL);
            d->ep_frame_div++;
        }
    }
    qemu_mutex_unlock(&d->lock);
    return NULL;
}
