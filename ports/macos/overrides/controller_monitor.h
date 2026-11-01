#ifndef BLACK_CONTROLLER_MONITOR_H
#define BLACK_CONTROLLER_MONITOR_H
#include <stdint.h>
enum { BLACK_CAL_IDLE, BLACK_CAL_MEASURING, BLACK_CAL_READY, BLACK_CAL_APPLIED,
       BLACK_CAL_MOVED, BLACK_CAL_DISCONNECTED, BLACK_CAL_CANCELLED };
typedef struct {
    int connected, instance, raw[6], mapped[6];
    uint32_t updated_ms;
    char name[96];
    int calibration, peaks[2], suggested[2];
} BlackControllerMonitor;
void black_ControllerPublish(const char *name, int instance, const int raw[6], const int mapped[6], uint32_t ms);
BlackControllerMonitor black_ControllerMonitor(void);
void black_ControllerMeasure(void);
void black_ControllerCancelMeasure(void);
int black_ControllerTakeMeasurement(int percentages[2]);
#endif
