/* The DAC gamma table shared by the guest loader and the presenter; see
 * display_gamma.h. */
#include "display_gamma.h"

#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static uint8_t s_table[BLACK_GAMMA_ENTRIES * 3];
static uint32_t s_version;
static int s_ready;
static unsigned long s_loads;

static void black_GammaIdentity(void)
{
    for (unsigned i = 0; i < BLACK_GAMMA_ENTRIES; ++i)
        s_table[i * 3] = s_table[i * 3 + 1] = s_table[i * 3 + 2] = (uint8_t)i;
}

static void black_GammaTrace(uint32_t version, unsigned long loads, const uint8_t *t)
{
    static int trace = -1;
    static unsigned logged;
    if (trace < 0) trace = getenv("BLACK_GAMMA_TRACE") != NULL;
    if (!trace || logged >= 200) return;
    ++logged;
    fprintf(stderr, "[GAMMA] load %lu table %u: r0=%u r64=%u r128=%u r192=%u r255=%u g128=%u b128=%u%s\n",
        loads, version, t[0], t[64 * 3], t[128 * 3], t[192 * 3], t[255 * 3], t[128 * 3 + 1],
        t[128 * 3 + 2], black_GammaIsIdentity(t) ? " identity" : "");
}

void black_GammaSetRamp(const uint8_t ramp[BLACK_GAMMA_ENTRIES * 3])
{
    uint8_t next[BLACK_GAMMA_ENTRIES * 3];
    for (unsigned i = 0; i < BLACK_GAMMA_ENTRIES; ++i) {
        next[i * 3] = ramp[i];
        next[i * 3 + 1] = ramp[BLACK_GAMMA_ENTRIES + i];
        next[i * 3 + 2] = ramp[BLACK_GAMMA_ENTRIES * 2 + i];
    }
    pthread_mutex_lock(&s_lock);
    if (!s_ready) { black_GammaIdentity(); s_ready = 1; }
    int changed = memcmp(s_table, next, sizeof next) != 0;
    if (changed) {
        memcpy(s_table, next, sizeof next);
        if (!++s_version) s_version = 1;
    }
    uint32_t version = s_version;
    unsigned long loads = ++s_loads;
    pthread_mutex_unlock(&s_lock);
    if (changed || loads == 1) black_GammaTrace(version, loads, next);
}

uint32_t black_GammaSnapshot(uint8_t rgb[BLACK_GAMMA_ENTRIES * 3])
{
    pthread_mutex_lock(&s_lock);
    if (!s_ready) { black_GammaIdentity(); s_ready = 1; }
    memcpy(rgb, s_table, sizeof s_table);
    uint32_t version = s_version;
    pthread_mutex_unlock(&s_lock);
    return version;
}

int black_GammaIsIdentity(const uint8_t rgb[BLACK_GAMMA_ENTRIES * 3])
{
    for (unsigned i = 0; i < BLACK_GAMMA_ENTRIES; ++i)
        if (rgb[i * 3] != i || rgb[i * 3 + 1] != i || rgb[i * 3 + 2] != i) return 0;
    return 1;
}

void black_GammaReset(void)
{
    pthread_mutex_lock(&s_lock);
    black_GammaIdentity();
    s_ready = 1;
    s_version = 0;
    s_loads = 0;
    pthread_mutex_unlock(&s_lock);
}

static int black_GammaClamp(int v, int low, int high) { return v < low ? low : v > high ? high : v; }

int black_GammaUserNeutral(int brightness, int gamma)
{
    return black_GammaClamp(brightness, BLACK_BRIGHTNESS_MIN, BLACK_BRIGHTNESS_MAX) == BLACK_BRIGHTNESS_NEUTRAL &&
        black_GammaClamp(gamma, BLACK_GAMMA_MIN, BLACK_GAMMA_MAX) == BLACK_GAMMA_NEUTRAL;
}

double black_GammaUserCurve(double x, int brightness, int gamma)
{
    double b = black_GammaClamp(brightness, BLACK_BRIGHTNESS_MIN, BLACK_BRIGHTNESS_MAX) / 100.0;
    double g = black_GammaClamp(gamma, BLACK_GAMMA_MIN, BLACK_GAMMA_MAX) / 100.0;
    x = x < 0 ? 0 : x > 1 ? 1 : x;
    if (g != 1.0) x = pow(x, 1.0 / g);
    if (b > 0) x = b + x * (1.0 - b);
    else if (b < 0) x = x + b <= 0 ? 0 : (x + b) / (1.0 + b);
    return x;
}

void black_GammaCompose(const uint8_t rgb[BLACK_GAMMA_ENTRIES * 3], int brightness, int gamma,
    uint16_t out[BLACK_GAMMA_ENTRIES * 3])
{
    int neutral = black_GammaUserNeutral(brightness, gamma);
    for (unsigned i = 0; i < BLACK_GAMMA_ENTRIES * 3; ++i) {
        if (neutral) { out[i] = (uint16_t)(rgb[i] * 257u); continue; }
        double y = black_GammaUserCurve(rgb[i] / 255.0, brightness, gamma);
        out[i] = (uint16_t)lround(y * 65535.0);
    }
}
