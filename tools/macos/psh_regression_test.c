/* A malformed PROJECT2D texture dimension must still declare its register. */
#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/glsl/psh.h"
#include <stdio.h>
#include <string.h>

int main(void)
{
    PshState state = {0};
    state.shader_stage_program = PS_TEXTUREMODES_PROJECT2D << 10;
    for (int dim = 0; dim <= 3; dim += dim == 0 ? 2 : 1) {
        state.dim_tex[2] = dim;
        MString *shader = pgraph_glsl_gen_psh(&state, (GenPshGlslOptions){0});
        const char *text = mstring_get_str(shader);
        const char *register_decl = strstr(text, "vec4 t2 = textureProj(texSamp2,");
        if (!register_decl || strstr(register_decl + 1, "vec4 t2 =")) {
            fprintf(stderr, "missing or repeated stage 2 for dimension %d\n", dim);
            mstring_unref(shader);
            return 1;
        }
        mstring_unref(shader);
    }
    puts("PROJECT2D dimensions 0, 2, 3 declare texture register: PASS");
    return 0;
}
