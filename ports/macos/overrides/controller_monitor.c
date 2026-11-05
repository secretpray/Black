/* Observe a controller; never invent a new center or edit game progress.
 * Neutral measurement is explicit, bounded, device-specific and can fail. */
#include "controller_monitor.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static pthread_mutex_t monitor_lock = PTHREAD_MUTEX_INITIALIZER;
static BlackControllerMonitor monitor;
static uint32_t measure_start;
static unsigned samples;
/* Emit transitions only. A host-side observer can disconnect real hardware
 * immediately after begin without changing the two-second measurement window. */
static void calibration_event(const char *event, int previous, uint32_t ms)
{
    if (getenv("RECOMP_INPUT_DEBUG"))
        fprintf(stderr, "[PAD-CAL] event=%s instance=%d from=%d state=%d ms=%u samples=%u elapsed_ms=%u\n",
            event, monitor.instance, previous, monitor.calibration, ms, samples,
            samples ? (uint32_t)(ms - measure_start) : 0);
}

void black_ControllerPublish(const char *name, int instance, const int raw[6], const int mapped[6], uint32_t ms)
{
    pthread_mutex_lock(&monitor_lock);
    int previous = monitor.calibration;
    if ((monitor.calibration == BLACK_CAL_MEASURING || monitor.calibration == BLACK_CAL_READY) &&
        (!name || instance != monitor.instance))
        monitor.calibration = BLACK_CAL_DISCONNECTED;
    monitor.connected = name != NULL;
    monitor.instance = instance;
    monitor.updated_ms = ms;
    snprintf(monitor.name, sizeof monitor.name, "%s", name ? name : "No controller");
    if (raw && mapped) { memcpy(monitor.raw, raw, sizeof monitor.raw); memcpy(monitor.mapped, mapped, sizeof monitor.mapped); }
    else { memset(monitor.raw, 0, sizeof monitor.raw); memset(monitor.mapped, 0, sizeof monitor.mapped); }
    if (monitor.calibration == BLACK_CAL_MEASURING) {
        if (!samples) measure_start = ms;
        ++samples;
        for (int i = 0; i < 4; ++i) {
            int magnitude = abs(raw[i]);
            if (magnitude > monitor.peaks[i / 2]) monitor.peaks[i / 2] = magnitude;
            if (magnitude > 4915) monitor.calibration = BLACK_CAL_MOVED; /* >15%, not neutral */
        }
        if (monitor.calibration == BLACK_CAL_MEASURING && samples >= 30 && (uint32_t)(ms - measure_start) >= 2000) {
            for (int i = 0; i < 2; ++i) {
                int percent = (monitor.peaks[i] * 100 + 32766) / 32767 + 2;
                monitor.suggested[i] = percent > 17 ? 17 : percent;
            }
            monitor.calibration = BLACK_CAL_READY;
        }
    }
    if (monitor.calibration != previous)
        calibration_event("publish", previous, ms);
    pthread_mutex_unlock(&monitor_lock);
}
BlackControllerMonitor black_ControllerMonitor(void)
{
    pthread_mutex_lock(&monitor_lock);
    BlackControllerMonitor result = monitor;
    pthread_mutex_unlock(&monitor_lock);
    return result;
}
void black_ControllerMeasure(void)
{
    pthread_mutex_lock(&monitor_lock);
    int previous = monitor.calibration;
    monitor.calibration = monitor.connected ? BLACK_CAL_MEASURING : BLACK_CAL_DISCONNECTED;
    monitor.peaks[0] = monitor.peaks[1] = 0;
    samples = 0;
    calibration_event("begin", previous, monitor.updated_ms);
    pthread_mutex_unlock(&monitor_lock);
}
void black_ControllerCancelMeasure(void)
{
    pthread_mutex_lock(&monitor_lock);
    int previous = monitor.calibration;
    if (monitor.calibration == BLACK_CAL_MEASURING || monitor.calibration == BLACK_CAL_READY)
        monitor.calibration = BLACK_CAL_CANCELLED;
    if (monitor.calibration != previous)
        calibration_event("cancel", previous, monitor.updated_ms);
    pthread_mutex_unlock(&monitor_lock);
}
int black_ControllerTakeMeasurement(int percentages[2])
{
    pthread_mutex_lock(&monitor_lock);
    int ready = monitor.calibration == BLACK_CAL_READY;
    if (ready) {
        percentages[0] = monitor.suggested[0]; percentages[1] = monitor.suggested[1];
        monitor.calibration = BLACK_CAL_APPLIED;
        calibration_event("apply", BLACK_CAL_READY, monitor.updated_ms);
    }
    pthread_mutex_unlock(&monitor_lock);
    return ready;
}
