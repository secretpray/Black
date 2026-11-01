/* Buffered, bounded timing records. No screenshots, GPU queries or new waits. */
#include "frame_trace.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
static pthread_once_t once = PTHREAD_ONCE_INIT;
static const char *directory;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static FILE *files[BLACK_TRACE_CHANNELS];
static unsigned counts[BLACK_TRACE_CHANNELS];
static const char *names[] = {"world", "flip", "download", "capture", "readback",
    "shader", "present", "pixels", "display", "source", "gpu"};
static void init(void) { directory = getenv("BLACK_FRAME_TRACE_DIR"); }
int black_trace_enabled(void) { pthread_once(&once, init); return directory != NULL; }
uint64_t black_trace_ns(void)
{
    if (!black_trace_enabled()) return 0;
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + t.tv_nsec;
}
void black_trace(unsigned channel, uint32_t frame, uint64_t start, uint64_t end,
    uint64_t a, uint64_t b)
{
    if (!start || !black_trace_enabled() || channel >= BLACK_TRACE_CHANNELS) return;
    pthread_mutex_lock(&lock);
    if (counts[channel] >= 250000) { pthread_mutex_unlock(&lock); return; }
    if (!files[channel]) {
        char path[4096]; snprintf(path, sizeof path, "%s/%s.csv", directory, names[channel]);
        files[channel] = fopen(path, "w");
        if (files[channel]) {
            setvbuf(files[channel], NULL, _IOFBF, 65536);
            fputs("frame,start_ns,end_ns,a,b\n", files[channel]);
        }
    }
    if (files[channel]) {
        fprintf(files[channel], "%u,%llu,%llu,%llu,%llu\n", frame,
            (unsigned long long)start, (unsigned long long)end,
            (unsigned long long)a, (unsigned long long)b);
        if (++counts[channel] % 60 == 0 || channel == BLACK_TRACE_SHADER) fflush(files[channel]);
    }
    pthread_mutex_unlock(&lock);
}
