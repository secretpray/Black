// SMAA's reference shader and precomputed lookups, as the root checkout keeps
// them for TGP's renderer (src/d3d, MIT licence in each header), exported to
// the Metal presenter's C/Objective-C without a second copy.
#include "d3d/smaa_shader.h"
#include "d3d/smaa_textures.h"

// extern on each: a namespace-scope const has internal linkage in C++ otherwise.
extern "C" const char *const black_smaa_reference = gpu::kSmaaHlsl;
extern "C" const unsigned char *const black_smaa_area = gpu::kSmaaArea;
extern "C" const unsigned char *const black_smaa_search = gpu::kSmaaSearch;
