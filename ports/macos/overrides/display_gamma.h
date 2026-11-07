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
/* Test hook: back to identity and version 0. */
void black_GammaReset(void);

#endif
