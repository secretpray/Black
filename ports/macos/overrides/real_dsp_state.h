/* Keep the pinned APU layout: GP/EP slots are opaque pointers outside the
 * legacy reset. Only this adapter interprets them as real DSP states. */
#pragma once
#define DSPState BlackLegacyDSPState
#define DSPDMAState BlackLegacyDSPDMAState
#include "../runtime/src/apu/apu_state.h"
#undef DSPState
#undef DSPDMAState
#include "dsp/dsp.h"
extern float black_real_dsp_output[256][2];
