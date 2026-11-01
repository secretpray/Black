/* SDL already supplies centered controller axes. Never learn a new center
 * from arbitrary input at startup: a held stick would become permanent drift. */
#ifndef BLACK_CONTROLLER_AXES_H
#define BLACK_CONTROLLER_AXES_H
#include <stdint.h>

/* TGP game_info.cpp: a signed 16-bit reversal, including -32768. */
static inline int black_invert_axis(int value, int invert)
{ return invert ? -1 - value : value; }
#include <math.h>

static inline int16_t black_axis_clamp(double value)
{
    if (value > 32767.0) return 32767;
    if (value < -32768.0) return -32768;
    return (int16_t)value;
}

/* TGP's MoveDeadZone, adapted to native C. BLACK subtracts 0.2 from
 * each axis itself (sub_000C24D0, initialized by sub_000C27C0). Move that
 * existing threshold; applying another rescaled deadzone stacks the two. */
static inline int16_t black_title_axis(int value, int deadzone)
{
    if (value > 32767) value = 32767;
    if (value < -32768) value = -32768;
    if (deadzone <= 0) return (int16_t)value; /* title's original response */
    if (deadzone > 32766) deadzone = 32766;
    double magnitude = fmin(fabs((double)value), 32767.0);
    if (magnitude <= deadzone) return 0;
    double adjusted = (0.2 + (magnitude - deadzone) / (32767.0 - deadzone) * 0.8) * 32767.0;
    return black_axis_clamp(lround(value < 0 ? -adjusted : adjusted));
}

static inline void black_title_stick(int16_t raw_x, int16_t raw_y,
    int deadzone, int16_t *out_x, int16_t *out_y)
{
    *out_x = black_title_axis(raw_x, deadzone);
    *out_y = black_title_axis(-(int)raw_y, deadzone);
}

/* A negative threshold preserves the original path. Zero explicitly removes
 * the title's inner deadzone, keeping its own 0.9 saturation unchanged. */
static inline int16_t black_configured_axis(int value, int threshold)
{
    if (threshold < 0) return black_title_axis(value, 0);
    if (threshold > 0) return black_title_axis(value, threshold);
    if (!value) return 0;
    double magnitude = fmin(fabs((double)value), 32767.0);
    double adjusted = (0.2 + magnitude / 32767.0 * 0.8) * 32767.0;
    return black_axis_clamp(lround(value < 0 ? -adjusted : adjusted));
}

static inline double black_game_axis(int16_t mapped)
{
    double value = mapped / 32767.0;
    double result = fmax(0, (fmin(fabs(value), 0.9) - 0.2) / 0.7);
    return value < 0 ? -result : result;
}

static inline uint8_t black_trigger(int value, int percent)
{
    if (value < 0) value = 0;
    if (value > 32767) value = 32767;
    if (percent <= 0) return (uint8_t)(value >> 7); /* unchanged native mapping */
    int threshold = (int)lround(32767.0 * percent / 100.0);
    if (value <= threshold) return 0;
    return (uint8_t)lround((value - threshold) * 255.0 / (32767 - threshold));
}
#endif
