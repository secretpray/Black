#pragma once
/* Research trace of the HUD sprites near the aiming point (reticle_trace.c),
 * on with BLACK_RETICLE_TRACE=1 only. Each reads the batcher's arguments from
 * the guest stack and eax, so it is called first thing in its hook. */
#include <stdint.h>

void black_ReticleTraceQuads(uint32_t quads);
void black_ReticleTraceStrip(uint32_t points);
void black_ReticleTrace2DBegin(void);
