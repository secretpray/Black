#undef NDEBUG
#include "../../ports/macos/overrides/text_pad.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    BlackTextPad s = {0};
    /* Held on arrival: nothing until released and pressed again. */
    assert(!black_text_pad(&s, 0, 1, 1, 0, 0));
    assert(!black_text_pad(&s, 1, 1, 1, 0, 10));
    assert(!black_text_pad(&s, 1, 0, 0, 0, 20));
    /* X deletes once, then repeats after the delay while held. */
    assert(black_text_pad(&s, 1, 1, 0, 0, 100) == '\b');
    assert(!black_text_pad(&s, 1, 1, 0, 0, 100 + BLACK_TEXT_PAD_REPEAT_DELAY - 1));
    assert(black_text_pad(&s, 1, 1, 0, 0, 100 + BLACK_TEXT_PAD_REPEAT_DELAY) == '\b');
    assert(!black_text_pad(&s, 1, 1, 0, 0, 100 + BLACK_TEXT_PAD_REPEAT_DELAY + 1));
    assert(black_text_pad(&s, 1, 1, 0, 0, 100 + BLACK_TEXT_PAD_REPEAT_DELAY + BLACK_TEXT_PAD_REPEAT_EVERY) == '\b');
    assert(!black_text_pad(&s, 1, 0, 0, 0, 2000));
    /* Y types one space per press, no repeat. */
    assert(black_text_pad(&s, 1, 0, 1, 0, 2010) == ' ');
    assert(!black_text_pad(&s, 1, 0, 1, 0, 5000));
    assert(!black_text_pad(&s, 1, 0, 0, 0, 5010));
    assert(black_text_pad(&s, 1, 0, 1, 0, 5020) == ' ');
    /* Leaving the page drops the repeat. */
    assert(black_text_pad(&s, 1, 1, 0, 0, 6000) == '\b');
    assert(!black_text_pad(&s, 0, 1, 0, 0, 7000));
    assert(!black_text_pad(&s, 1, 1, 0, 0, 7010));
    /* Start finishes the name once per press; held on arrival it waits. */
    BlackTextPad d = {0};
    assert(!black_text_pad(&d, 0, 0, 0, 1, 0));
    assert(!black_text_pad(&d, 1, 0, 0, 1, 10));
    assert(!black_text_pad(&d, 1, 0, 0, 0, 20));
    assert(black_text_pad(&d, 1, 0, 0, 1, 30) == '\r');
    assert(!black_text_pad(&d, 1, 0, 0, 1, 1000));
    puts("Name keyboard pad shortcuts: PASS");
    return 0;
}
