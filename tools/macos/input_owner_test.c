#include "../../ports/macos/overrides/input_owner.h"
#undef NDEBUG
#include <assert.h>
int main(void)
{
    BlackInputOwner state = {0};
    assert(black_input_owner(&state, BLACK_INPUT_AUTO, 1, 0, 0, 0) == BLACK_INPUT_GAMEPAD);
    assert(black_input_owner(&state, BLACK_INPUT_AUTO, 1, 1, 1, 10) == BLACK_INPUT_GAMEPAD);
    assert(black_input_owner(&state, BLACK_INPUT_AUTO, 1, 0, 1, 500) == BLACK_INPUT_GAMEPAD);
    /* Idle pad + mouse motion never switches. A deliberate key/button does. */
    assert(black_input_owner(&state, BLACK_INPUT_AUTO, 1, 0, 0, 2000) == BLACK_INPUT_GAMEPAD);
    assert(black_input_owner(&state, BLACK_INPUT_AUTO, 1, 0, 1, 2001) == BLACK_INPUT_KEYBOARD);
    assert(black_input_owner(&state, BLACK_INPUT_AUTO, 1, 0, 0, 2002) == BLACK_INPUT_KEYBOARD);
    assert(black_input_owner(&state, BLACK_INPUT_AUTO, 1, 1, 1, 2003) == BLACK_INPUT_GAMEPAD);
    assert(black_input_owner(&state, BLACK_INPUT_AUTO, 1, 0, 1, 9000) == BLACK_INPUT_GAMEPAD);
    assert(black_input_owner(&state, BLACK_INPUT_GAMEPAD, 1, 0, 1, 9000) == BLACK_INPUT_GAMEPAD);
    assert(black_input_owner(&state, BLACK_INPUT_KEYBOARD, 1, 1, 0, 9001) == BLACK_INPUT_KEYBOARD);
    assert(black_input_owner(&state, BLACK_INPUT_AUTO, 0, 0, 1, 9002) == BLACK_INPUT_KEYBOARD);
    assert(black_input_owner(&state, BLACK_INPUT_AUTO, 1, 0, 0, 9003) == BLACK_INPUT_GAMEPAD);
}
