#include "../../ports/macos/overrides/dsp_headroom.h"
#include "../../ports/macos/runtime/src/apu/fpconv.h"
#undef NDEBUG
#include <assert.h>
int main(void)
{
    setenv("BLACK_DSP_HEADROOM_DB", "6", 1);
    float gain = black_dsp_input_gain();
    assert(gain > .50f && gain < .502f);
    /* Reproduce input saturation before the DSP can run. Output attenuation
     * cannot recover it; fixed input headroom + inverse gain can. */
    const float samples[] = {1.5f, -1.5f, .25f, -.125f, 0};
    for (unsigned i = 0; i < sizeof samples / sizeof samples[0]; ++i) {
        float sample = samples[i];
        float bus = int24_to_float(float_to_24b(sample * gain));
        float recovered = bus / gain;
        assert(fabsf(recovered - sample) < .000001f);
    }
    assert(int24_to_float(float_to_24b(1.5f)) < 1.001f);
}
