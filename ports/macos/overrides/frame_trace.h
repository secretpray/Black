#ifndef BLACK_FRAME_TRACE_H
#define BLACK_FRAME_TRACE_H
#include <stdint.h>
enum { BLACK_TRACE_WORLD, BLACK_TRACE_FLIP, BLACK_TRACE_DOWNLOAD,
       BLACK_TRACE_CAPTURE, BLACK_TRACE_READBACK, BLACK_TRACE_SHADER,
       BLACK_TRACE_PRESENT, BLACK_TRACE_PIXELS, BLACK_TRACE_DISPLAY,
       BLACK_TRACE_SOURCE, BLACK_TRACE_GPU,
       BLACK_TRACE_CHANNELS };
int black_trace_enabled(void);
uint64_t black_trace_ns(void);
void black_trace(unsigned channel, uint32_t frame, uint64_t start, uint64_t end,
    uint64_t a, uint64_t b);
#endif
