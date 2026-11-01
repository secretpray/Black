/* Preserve the pinned memory model and the existing DSP MMIO overlay.
 * The original owns publication/W1C; extend its completed handoff below.
 * License: same as the included runtime source. */
#define xbox_Nv2aSoftwareInterrupt black_OriginalNv2aSoftwareInterrupt
#include BLACK_NV2A_MEMORY_SOURCE
#undef xbox_Nv2aSoftwareInterrupt
#include "nv2a_fifo_ack.inc"
