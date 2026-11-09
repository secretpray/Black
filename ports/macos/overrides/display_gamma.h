/* The Xbox video DAC's 256-entry RGB lookup. XDK SetGammaRamp leaves a ramp
 * for the vertical blank, whose handler loads it into the DAC palette
 * registers (PRMDIO 0x3C8/0x3C9); the DAC then applies it to every pixel
 * shown, so the game's brightness setting, tints and fades reach the screen
 * this way. Here that blank loads the DAC only while a flip waits for it,
 * which stops after the first frames, and the DAC registers are plain RAM:
 * the game's brightness ramp never reached the screen. BLACK's renderer
 * (0x1762B0) hands each new ramp to this table as it queues it, and the
 * presenter applies the table as the last step of its scaling pass. */
#ifndef BLACK_DISPLAY_GAMMA_H
#define BLACK_DISPLAY_GAMMA_H

#include <stdint.h>

#define BLACK_GAMMA_ENTRIES 256u

/* D3DGAMMARAMP layout: red[256], green[256], blue[256]. */
void black_GammaSetRamp(const uint8_t ramp[BLACK_GAMMA_ENTRIES * 3]);
/* Fills the current table as interleaved R,G,B bytes. Returns its version,
 * which changes whenever the table changes (0: never changed, identity). */
uint32_t black_GammaSnapshot(uint8_t rgb[BLACK_GAMMA_ENTRIES * 3]);
/* 1 when the interleaved table maps every entry to itself. */
int black_GammaIsIdentity(const uint8_t rgb[BLACK_GAMMA_ENTRIES * 3]);
/* The player's display brightness and gamma, applied after the game's ramp,
 * as a TV's own controls act on the console's signal. Brightness moves the
 * black level in percent of the range, white staying white: positive lifts
 * shadows, negative deepens them. Gamma, in hundredths, bends the midtones
 * with both ends fixed: above 100 brighter, below darker. At the neutral
 * 0 / 100 the output is the game's table exactly. */
#define BLACK_BRIGHTNESS_MIN (-20)
#define BLACK_BRIGHTNESS_MAX 20
#define BLACK_BRIGHTNESS_NEUTRAL 0
#define BLACK_GAMMA_MIN 60
#define BLACK_GAMMA_MAX 160
#define BLACK_GAMMA_NEUTRAL 100
int black_GammaUserNeutral(int brightness, int gamma);
/* One channel value 0..1 through the player's curve. */
double black_GammaUserCurve(double x, int brightness, int gamma);
/* The interleaved table through the player's curve, as 16-bit values
 * (65535 = full), so the curve adds no 8-bit banding of its own. */
void black_GammaCompose(const uint8_t rgb[BLACK_GAMMA_ENTRIES * 3], int brightness, int gamma,
    uint16_t out[BLACK_GAMMA_ENTRIES * 3]);
/* Test hook: back to identity and version 0. */
void black_GammaReset(void);

#endif
