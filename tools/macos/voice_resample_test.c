/* Exercise production sample fetching, interpolation, PIO and frame mixing.
 * Expected PCM is computed from absolute positions, independent of the
 * implementation's cache, interpolation pair and notification counters. */
#define BLACK_APU_DEVICE_HELPERS_ONLY
#include "apu_device_test.c"

enum { V = 64, CAP = 32768 };
static float out[CAP][2], split_out[CAP][2];
static int source_length, source_channels, source_adpcm;

static int16_t pattern(unsigned index, unsigned channel)
{ return (int16_t)((index * 977 + channel * 613 + 211) % 20001 - 10000); }

static float source(unsigned index, unsigned channel)
{
    if (source_adpcm) index = (index / 64) * 64;
    return pattern(index, source_channels == 1 ? 0 : channel) / 32768.0f;
}

static int active(void)
{ return voice_get_mask(&d, V, NV_PAVS_VOICE_PAR_STATE, NV_PAVS_VOICE_PAR_STATE_ACTIVE_VOICE); }

static uint8_t *notifier(unsigned ssl)
{
    return ram + d.regs[NV_PAPU_FENADDR] +
        16 * (MCPX_HW_NOTIFIER_BASE_OFFSET + V * MCPX_HW_NOTIFIER_COUNT +
              MCPX_HW_NOTIFIER_SSLA_DONE + ssl) + 15;
}

static void fixture(unsigned length, unsigned channels, int loop)
{
    init(); pcm(V, length, loop);
    source_length = length; source_channels = channels; source_adpcm = 0;
    field(V, NV_PAVS_VOICE_CFG_FMT, NV_PAVS_VOICE_CFG_FMT_STEREO, channels - 1);
    field(V, NV_PAVS_VOICE_CFG_FMT, NV_PAVS_VOICE_CFG_FMT_SAMPLES_PER_BLOCK, channels - 1);
    d.regs[NV_PAPU_FECV] = V;
    d.regs[NV_PAPU_TVL2D] = V;
    for (unsigned n = 0; n < length; ++n)
        for (unsigned ch = 0; ch < channels; ++ch)
            stw_le_phys(NULL, DATA + (n * channels + ch) * 2, pattern(n, ch));
}

static int render(float target[][2], unsigned limit, float ratio, int split)
{
    const unsigned chunks[] = { 1, 7, 3, 31, 2, 17, 32, 5 };
    unsigned total = 0, calls = 0;
    while (total < limit && active() && calls < CAP) {
        unsigned request = split ? chunks[calls % 8] : 32;
        if (request > limit - total) request = limit - total;
        int got = voice_resample(&d, V, target + total, request, ratio);
        ++calls;
        if (got <= 0) break;
        CHECK(got <= (int)request, "resampler respects output capacity");
        total += got;
    }
    return total;
}

static unsigned oracle_index(unsigned index, int lbo)
{
    if (index < (unsigned)source_length) return index;
    return lbo < 0 ? source_length - 1 :
        lbo + (index - source_length) % (source_length - lbo);
}

static void compare(float actual[][2], unsigned count, double position,
                    double step, int lbo, const char *label)
{
    unsigned mismatches = 0;
    for (unsigned n = 0; n < count; ++n) {
        double p = position + n * step;
        unsigned i = (unsigned)floor(p);
        float frac = (float)(p - i);
        for (unsigned ch = 0; ch < 2; ++ch) {
            float a = source(oracle_index(i, lbo), ch);
            float b = source(oracle_index(i + 1, lbo), ch);
            float expected = a + (b - a) * frac;
            if (fabsf(actual[n][ch] - expected) > 0.000002f) ++mismatches;
        }
    }
    CHECK(mismatches == 0, label);
}

static void rates(void)
{
    const int16_t pitches[] = { 0, -4096, -4597, -2396, -501, 4096, -32768, 32767 };
    for (unsigned ch = 1; ch <= 2; ++ch)
    for (unsigned p = 0; p < ARRAY_SIZE(pitches); ++p) {
        float ratio = 1.0f / powf(2.0f, pitches[p] / 4096.0f);
        unsigned length = p >= 6 ? 67 : 4099;
        int expected = (int)ceil(length * (double)ratio);
        fixture(length, ch, 0);
        int got = render(out, CAP, ratio, 0);
        CHECK(got == expected && !active(), "static output duration follows full signed pitch range");
        compare(out, got, 0, 1.0 / ratio, -1, "absolute PCM oracle including clamped final endpoint");
        CHECK(*notifier(0) == NV1BA0_NOTIFICATION_STATUS_DONE_SUCCESS, "static completion after tail");
        fixture(length, ch, 0);
        int split_got = render(split_out, CAP, ratio, 1);
        CHECK(split_got == got && !memcmp(out, split_out, got * sizeof out[0]),
              "arbitrary output chunk sizes preserve samples and duration");
        if (p == 0) {
            unsigned bad = 0;
            for (unsigned n = 0; n < (unsigned)got; ++n)
                for (unsigned c = 0; c < 2; ++c) bad += out[n][c] != source(n, c);
            CHECK(!bad, "unity PCM is bit exact");
        }
    }
}

static void lifecycle(void)
{
    fixture(67, 2, 1);
    field(V, NV_PAVS_VOICE_CUR_PSH_SAMPLE, NV_PAVS_VOICE_CUR_PSH_SAMPLE_LBO, 3);
    float ratio = 1.0f / powf(2.0f, -2396 / 4096.0f);
    CHECK(render(out, 1024, ratio, 1) == 1024 && active(), "fractional loop remains active");
    compare(out, 1024, 0, 1.0 / ratio, 3, "nonzero LBO interpolation across multiple loops");

    fixture(257, 2, 0);
    CHECK(render(out, 7, 2, 1) == 7, "prime fractional phase");
    uint32_t cbo = voice_get_mask(&d, V, NV_PAVS_VOICE_PAR_OFFSET, NV_PAVS_VOICE_PAR_OFFSET_CBO);
    fe_method(&d, NV1BA0_PIO_VOICE_PAUSE, V | NV1BA0_PIO_VOICE_PAUSE_ACTION);
    CHECK(voice_resample(&d, V, out + 7, 32, 2) < 0 && active(), "paused voice emits nothing");
    CHECK(cbo == voice_get_mask(&d, V, NV_PAVS_VOICE_PAR_OFFSET, NV_PAVS_VOICE_PAR_OFFSET_CBO), "pause does not prefetch");
    fe_method(&d, NV1BA0_PIO_VOICE_PAUSE, V);
    CHECK(render(out + 7, 9, 2, 1) == 9, "resume keeps phase");
    compare(out, 16, 0, 0.5, -1, "pause resume continues exact timeline");
    CHECK(render(out, 13, 1, 1) == 13, "pitch can change without restart");
    compare(out, 13, 8, 1, -1, "new pitch uses previous source position");

    fe_method(&d, NV1BA0_PIO_SET_VOICE_BUF_CBO, 11);
    CHECK(render(out, 4, 1, 1) == 4, "PIO seek resets cached data");
    compare(out, 4, 11, 1, -1, "seek begins at requested CBO");
    /* Real VOICE_ON: insert into an empty 2D list, without acquiring an
     * uninitialized fixture lock (the guest already owns this voice lock). */
    d.vp.voice_locked[V / 64] = 1ULL << (V % 64);
    d.regs[NV_PAPU_FEAV] = 1u << ctz32(NV_PAPU_FEAV_LST);
    d.regs[NV_PAPU_TVL2D] = 0xffff;
    fe_method(&d, NV1BA0_PIO_VOICE_ON, V);
    CHECK(render(out, 4, 2, 1) == 4, "VOICE_ON restarts cached voice");
    compare(out, 4, 0, 0.5, -1, "VOICE_ON has no old phase or source cache");
    mcpx_apu_vp_reset(&d);
    field(V, NV_PAVS_VOICE_PAR_OFFSET, NV_PAVS_VOICE_PAR_OFFSET_CBO, 0);
    CHECK(render(out, 4, 1, 1) == 4, "VP reset clears TU state");
    compare(out, 4, 0, 1, -1, "VP reset has no old source cache");

    fixture(67, 1, 0);
    const float bad[] = { 0, -1, NAN, INFINITY };
    for (unsigned i = 0; i < ARRAY_SIZE(bad); ++i)
        CHECK(voice_resample(&d, V, out, 1, bad[i]) < 0, "invalid ratio produces no output");
    CHECK(voice_resample(&d, V, out, 0, 1) < 0, "zero request produces no output");
    CHECK(voice_resample(&d, V, out, 1, 1) == 1 && out[0][0] == source(0, 0), "rejected calls consume no source");
}

static void setup_stream(unsigned a, unsigned b, int persist)
{
    field(V, NV_PAVS_VOICE_CFG_FMT, NV_PAVS_VOICE_CFG_FMT_DATA_TYPE, 1);
    field(V, NV_PAVS_VOICE_CFG_FMT, NV_PAVS_VOICE_CFG_FMT_PERSIST, persist);
    if (!persist) field(V, NV_PAVS_VOICE_PAR_STATE, NV_PAVS_VOICE_PAR_STATE_EACUR,
                        NV_PAVS_VOICE_PAR_STATE_EFCUR_RELEASE);
    d.vp.ssl[V].base[1] = 1;
    d.vp.ssl[V].count[0] = 1;
    d.vp.ssl[V].count[1] = b != 0;
    stl_le_phys(NULL, SSL, DATA);
    stl_le_phys(NULL, SSL + 4, a | (1u << 16));
    stl_le_phys(NULL, SSL + 8, DATA + a * 2);
    stl_le_phys(NULL, SSL + 12, b | (1u << 16));
}

static void streams(void)
{
    fixture(6, 1, 0); setup_stream(3, 3, 1);
    CHECK(render(out, 1, 2, 1) == 1 && !*notifier(0), "prefetch does not publish SSL DONE");
    CHECK(render(out + 1, 4, 2, 1) == 4 && !*notifier(0), "fractional endpoint not completed early");
    CHECK(render(out + 5, 1, 2, 1) == 1 && *notifier(0) == NV1BA0_NOTIFICATION_STATUS_DONE_SUCCESS,
          "first SSL DONE at playback boundary");
    d.vp.ssl[V].count[0] = 0;
    CHECK(render(out + 6, 6, 2, 1) == 6 && *notifier(1) == NV1BA0_NOTIFICATION_STATUS_DONE_SUCCESS,
          "second SSL completes at its own playback boundary");
    compare(out, 12, 0, 0.5, -1, "fractional SSL join without duplicate or missing sample");

    fixture(2, 1, 0); setup_stream(1, 0, 1);
    CHECK(render(out, 2, 2, 1) == 2 && out[0][0] == source(0, 0) && out[1][0] == source(0, 0),
          "starved SSL endpoint plays full duration without next buffer");
    CHECK(*notifier(0) == NV1BA0_NOTIFICATION_STATUS_DONE_SUCCESS, "starvation cannot prevent previous DONE");
    d.vp.ssl[V].count[0] = 0;
    *notifier(0) = *notifier(1) = 0;
    for (unsigned n = 0; n < 10; ++n)
        CHECK(voice_resample(&d, V, out + 2, 32, 2) < 0 && active(), "persistent starvation keeps voice, emits no data");
    CHECK(!*notifier(0) && !*notifier(1), "empty SSL produces no repeated completion");
    d.vp.ssl[V].count[1] = 1;
    stl_le_phys(NULL, SSL + 12, 1 | (1u << 16));
    CHECK(render(out + 2, 2, 2, 1) == 2 && out[2][0] == source(1, 0) && out[3][0] == source(1, 0),
          "late supply resumes without duplicate, dropped endpoint or phase reset");

    fixture(2, 1, 0); setup_stream(1, 0, 1);
    CHECK(render(out, 1, 0.4f, 1) == 1, "downsampling reaches beyond missing SSL");
    d.vp.ssl[V].count[0] = 0;
    CHECK(voice_resample(&d, V, out + 1, 1, 0.4f) < 0, "missing downsample source stalls");
    d.vp.ssl[V].count[1] = 1;
    stl_le_phys(NULL, SSL + 12, 1 | (1u << 16));
    /* More than one missing frame of source debt remains: consume the next
     * sample without emitting it, then stall again until a new SSL arrives. */
    CHECK(voice_resample(&d, V, out + 1, 1, 0.4f) < 0, "downsample source debt survives starvation");

    fixture(1, 1, 0); setup_stream(1, 0, 1);
    CHECK(render(out, 255, 256, 1) == 255 && !*notifier(0), "slow endpoint retains 255 outputs before DONE");
    CHECK(render(out + 255, 1, 256, 1) == 1 && *notifier(0) == NV1BA0_NOTIFICATION_STATUS_DONE_SUCCESS,
          "slow endpoint completes after 256 outputs");
    fixture(1, 1, 0); setup_stream(1, 0, 0);
    CHECK(render(out, 8, 2, 1) == 2 && !active(), "nonpersistent stream drains final fractional sample");
}

static void blocks(void)
{
    const float ratios[] = { 1, 2, 1.0f / powf(2.0f, -2396 / 4096.0f) };
    for (unsigned channels = 1; channels <= 2; ++channels)
    for (unsigned r = 0; r < ARRAY_SIZE(ratios); ++r) {
        fixture(192, channels, 0); source_adpcm = 1;
        memset(ram + DATA, 0, 3 * 36 * channels);
        for (unsigned block = 0; block < 3; ++block)
            for (unsigned ch = 0; ch < channels; ++ch)
                stw_le_phys(NULL, DATA + block * 36 * channels + ch * 4, pattern(block * 64, ch));
        field(V, NV_PAVS_VOICE_CFG_FMT, NV_PAVS_VOICE_CFG_FMT_CONTAINER_SIZE,
              NV_PAVS_VOICE_CFG_FMT_CONTAINER_SIZE_ADPCM);
        int got = render(out, CAP, ratios[r], 1);
        CHECK(got == (int)ceil(192 * (double)ratios[r]), "ADPCM full duration across block boundaries");
        compare(out, got, 0, 1.0 / ratios[r], -1, "ADPCM zero-nibble fixture equals independent block oracle");
    }
    /* The ordinary decoder also works in blocks: at unity its CBO advances
     * by one 32-source-frame batch, not by the interpolation pair each call. */
    fixture(257, 1, 0);
    CHECK(render(out, 1, 1, 1) == 1 &&
          voice_get_mask(&d, V, NV_PAVS_VOICE_PAR_OFFSET, NV_PAVS_VOICE_PAR_OFFSET_CBO) == 32,
          "source block fetched once for next 32 unity outputs");
    CHECK(render(out + 1, 31, 1, 1) == 31 &&
          voice_get_mask(&d, V, NV_PAVS_VOICE_PAR_OFFSET, NV_PAVS_VOICE_PAR_OFFSET_CBO) == 32,
          "cached block avoids further decoder fetches");
}

static void descriptor(void)
{
    fixture(64, 2, 0);
    field(V, NV_PAVS_VOICE_CFG_FMT, NV_PAVS_VOICE_CFG_FMT_CONTAINER_SIZE,
          NV_PAVS_VOICE_CFG_FMT_CONTAINER_SIZE_ADPCM);
    field(V, NV_PAVS_VOICE_CFG_FMT, NV_PAVS_VOICE_CFG_FMT_SAMPLES_PER_BLOCK, 0);
    /* The raw decoder confirms why this stereo descriptor is insufficient:
     * 36 bytes give only 25 frames, but the source indexes a 64-frame block. */
    int16_t decoded[130];
    memset(ram + DATA, 0, 36);
    CHECK(adpcm_decode_block(decoded, ram + DATA, 36, 2) == 25, "short stereo block has 25 frames, not 64");
    CHECK(voice_get_samples(&d, V, out, 32) < 0, "reject incomplete stereo descriptor before caching partial block");
}

static void frames(void)
{
    const int16_t pitch[] = { 0, -4096, -2396, -501 };
    for (unsigned p = 0; p < ARRAY_SIZE(pitch); ++p) {
        fixture(67, 1, 0);
        float ratio = 1.0f / powf(2.0f, pitch[p] / 4096.0f);
        field(V, NV_PAVS_VOICE_TAR_PITCH_LINK, NV_PAVS_VOICE_TAR_PITCH_LINK_PITCH, (uint16_t)pitch[p]);
        unsigned ticks = 0;
        while (active() && ticks < 16) {
            float bins[32][32] = {{0}};
            mcpx_apu_vp_frame(&d, bins);
            for (unsigned i = 0; i < 32; ++i) out[ticks * 32 + i][0] = out[ticks * 32 + i][1] = bins[0][i];
            ++ticks;
        }
        unsigned expected = (unsigned)ceil(67 * (double)ratio);
        CHECK(!active() && ticks == (expected + 31) / 32, "production VP applies signed pitch and full duration");
        compare(out, expected, 0, 1.0 / ratio, -1, "production voice_process mixes final partial tick");
        for (unsigned n = expected; n < ticks * 32; ++n)
            CHECK(out[n][0] == 0, "partial tick zero-padded after actual tail");
    }
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    if (!strcmp(argv[1], "rates")) rates();
    else if (!strcmp(argv[1], "lifecycle")) lifecycle();
    else if (!strcmp(argv[1], "streams")) streams();
    else if (!strcmp(argv[1], "blocks")) blocks();
    else if (!strcmp(argv[1], "descriptor")) descriptor();
    else if (!strcmp(argv[1], "frames")) frames();
    else return 2;
    printf("VP resample %s: %u failures\n", argv[1], failures);
    return failures ? 1 : 0;
}
