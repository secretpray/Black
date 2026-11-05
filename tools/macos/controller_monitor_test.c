#include "../../ports/macos/overrides/controller_monitor.h"
#include <stdio.h>
static int raw[6] = {385, -128, -640, 256, 0, 0}, mapped[6];
static void publish(int instance, uint32_t ms) { black_ControllerPublish("Synthetic pad", instance, raw, mapped, ms); }
int main(void)
{
    int result[2];
    black_ControllerMeasure();
    if (black_ControllerMonitor().calibration != BLACK_CAL_DISCONNECTED) return 1;
    publish(1, 0);
    for (unsigned t = 0; t <= 3000; t += 10) publish(1, t);
    if (black_ControllerTakeMeasurement(result)) return 2;
    black_ControllerMeasure();
    publish(1, 0xfffffff0u);
    for (unsigned t = 0; t < 2000; t += 10) publish(1, 0xfffffff0u + t);
    if (black_ControllerTakeMeasurement(result)) return 3;
    publish(1, 0xfffffff0u + 2000);
    if (!black_ControllerTakeMeasurement(result) || result[0] != 4 || result[1] != 4 ||
        black_ControllerTakeMeasurement(result)) return 4;
    black_ControllerMeasure(); raw[2] = 20000; publish(1, 4000);
    if (black_ControllerMonitor().calibration != BLACK_CAL_MOVED || black_ControllerTakeMeasurement(result)) return 5;
    raw[2] = 640;
    black_ControllerMeasure(); publish(2, 5000);
    if (black_ControllerMonitor().calibration != BLACK_CAL_DISCONNECTED) return 6;
    black_ControllerMeasure(); black_ControllerPublish(NULL, -1, NULL, NULL, 6000);
    if (black_ControllerMonitor().calibration != BLACK_CAL_DISCONNECTED) return 7;
    publish(1, 7000); black_ControllerMeasure(); black_ControllerCancelMeasure();
    for (unsigned t = 0; t <= 3000; t += 10) publish(1, 7000 + t);
    if (black_ControllerTakeMeasurement(result) || black_ControllerMonitor().calibration != BLACK_CAL_CANCELLED) return 8;
    black_ControllerMeasure(); publish(1, 11000); publish(1, 14000);
    if (black_ControllerTakeMeasurement(result)) return 9;
    black_ControllerCancelMeasure();
    BlackControllerMonitor monitor = black_ControllerMonitor();
    if (!monitor.connected || monitor.raw[0] != 385 || monitor.updated_ms != 14000) return 10;
    /* A real disconnect just before the deadline must discard all samples,
     * never apply the partial result, and never resume it on reconnect. */
    black_ControllerMeasure();
    for (unsigned t = 0; t <= 1900; t += 10) publish(1, 20000 + t);
    if (black_ControllerMonitor().calibration != BLACK_CAL_MEASURING) return 11;
    black_ControllerPublish(NULL, -1, NULL, NULL, 21990);
    result[0] = 91; result[1] = 92;
    monitor = black_ControllerMonitor();
    if (monitor.calibration != BLACK_CAL_DISCONNECTED || monitor.connected ||
        black_ControllerTakeMeasurement(result) || result[0] != 91 || result[1] != 92) return 12;
    for (int i=0;i<6;++i) if (monitor.raw[i] || monitor.mapped[i]) return 13;
    for (unsigned t = 0; t <= 3000; t += 10) publish(2, 22000 + t);
    if (black_ControllerTakeMeasurement(result) ||
        black_ControllerMonitor().calibration != BLACK_CAL_DISCONNECTED) return 14;
    puts("Explicit neutral measurement: device identity, motion rejection, cancel, duration/sample floor and clock wrap PASS");
    return 0;
}
