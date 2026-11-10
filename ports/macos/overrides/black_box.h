#ifndef BLACK_BOX_H
#define BLACK_BOX_H
#include <stdint.h>
/* A rolling record of the pad state the guest actually saw (xinput_device.c),
 * saved with the player's settings and launch options so a run can be
 * described, and its input replayed.
 *
 * The replay is the input timeline at guest poll times, not a save state:
 * the game still steps in real time, so a replayed run reproduces the
 * controls, not a bit-exact session. */
void black_BoxRecord(uint16_t buttons, const uint8_t analog[8], const int16_t axes[4]);
/* Writes the last minute to a file beside the settings; 1 on success. */
int black_BoxSave(void);
/* Last saved path, or "" before the first save. Main thread only. */
const char *black_BoxPath(void);
/* Replaces state with the recorded sample for this moment; 0 without a
 * replay (BLACK_INPUT_REPLAY) or before its first sample. */
int black_BoxReplay(uint16_t *buttons, uint8_t analog[8], int16_t axes[4]);
#endif
