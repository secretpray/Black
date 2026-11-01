/* Production software-method publication, with the guest ISR's real ordering:
 * 0x273EA0 FIFO=0; 0x273F0E W1C; 0x273F1F payload; 0x273F45 FIFO=1.
 * The baseline must fail normally, never by a timeout or signal. */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define NV2A_PGRAPH_TRAPPED_ADDR 0x400704u
#define NV2A_PGRAPH_TRAPPED_DATA 0x400708u
#define NV2A_PGRAPH_NSOURCE 0x400108u
#define NV2A_PGRAPH_INTR 0x400100u
static int g_nv2a_irq_on = 1;
static pthread_mutex_t g_nv2a_irq_lock = PTHREAD_MUTEX_INITIALIZER;
static uint32_t registers[5];
static uint32_t *nv2a_irq_slot(uint32_t offset)
{
    switch (offset) {
    case NV2A_PGRAPH_TRAPPED_ADDR: return &registers[0];
    case NV2A_PGRAPH_TRAPPED_DATA: return &registers[1];
    case NV2A_PGRAPH_NSOURCE: return &registers[2];
    case NV2A_PGRAPH_INTR: return &registers[3];
    case 0x400720u: return &registers[4];
    default: abort();
    }
}
static pthread_mutex_t control = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
static unsigned requests, acked, first_returned, waiting, release_first, immediate;
static uint32_t observed[2];
static int results[2];

static void set_register(uint32_t address, uint32_t value)
{
    pthread_mutex_lock(&g_nv2a_irq_lock);
    *nv2a_irq_slot(address) = value;
    pthread_mutex_unlock(&g_nv2a_irq_lock);
}
static uint32_t read_register(uint32_t address)
{
    pthread_mutex_lock(&g_nv2a_irq_lock);
    uint32_t result = *nv2a_irq_slot(address);
    pthread_mutex_unlock(&g_nv2a_irq_lock);
    return result;
}
static void xbox_KernelRequestNv2aInterrupt(void)
{
    pthread_mutex_lock(&control);
    ++requests;
    if (immediate) {
        observed[0] = read_register(NV2A_PGRAPH_TRAPPED_DATA);
        set_register(NV2A_PGRAPH_INTR, 0);
    }
    pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&control);
}
#include BLACK_NV2A_IRQ_SOURCE
#ifdef BLACK_NV2A_FIFO_BASELINE
#define xbox_Nv2aSoftwareInterrupt black_OriginalNv2aSoftwareInterrupt
#else
static void black_nv2a_fifo_wait_test_hook(void)
{
    pthread_mutex_lock(&control);
    waiting = 1;
    pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&control);
}
#define BLACK_NV2A_FIFO_TEST_HOOKS 1
#include "nv2a_fifo_ack.inc"
#endif

static void *isr(void *unused)
{
    (void)unused;
    for (unsigned i = 0; i < 2; ++i) {
        pthread_mutex_lock(&control);
        while (requests <= i) pthread_cond_wait(&changed, &control);
        pthread_mutex_unlock(&control);
        set_register(0x400720u, 0);
        set_register(NV2A_PGRAPH_INTR, 0); /* guest W1C happens BEFORE payload read */
        pthread_mutex_lock(&control);
        ++acked;
        pthread_cond_broadcast(&changed);
        while (!i && !release_first) pthread_cond_wait(&changed, &control);
        pthread_mutex_unlock(&control);
        observed[i] = read_register(NV2A_PGRAPH_TRAPPED_DATA);
        set_register(0x400720u, 1); /* callback consumed its payload */
    }
    return NULL;
}
static void *producer(void *unused)
{
    (void)unused;
    results[0] = xbox_Nv2aSoftwareInterrupt(0x100, 5, 1);
    pthread_mutex_lock(&control);
    first_returned = 1;
    pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&control);
    results[1] = xbox_Nv2aSoftwareInterrupt(0x100, 1, 1);
    return NULL;
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    set_register(0x400720u, 1);
    if (!strcmp(argv[1], "disabled")) {
        g_nv2a_irq_on = 0;
        set_register(0x400720u, 0);
        return xbox_Nv2aSoftwareInterrupt(0x100, 5, 1) != 0 || requests != 0;
    }
    if (!strcmp(argv[1], "immediate")) {
        immediate = 1;
        return xbox_Nv2aSoftwareInterrupt(0x100, 5, 1) != 1 || observed[0] != 5;
    }
    int delayed = !strcmp(argv[1], "delayed");
    if (!delayed && strcmp(argv[1], "handoff")) return 2;
    pthread_t consumer, writer;
    if (pthread_create(&consumer, NULL, isr, NULL) ||
        pthread_create(&writer, NULL, producer, NULL)) return 2;
    pthread_mutex_lock(&control);
    while (!acked || (!waiting && !first_returned)) pthread_cond_wait(&changed, &control);
    /* On the baseline force the second payload to publish before ISR reads A. */
    while (first_returned && requests < 2) pthread_cond_wait(&changed, &control);
    int premature = first_returned;
    pthread_mutex_unlock(&control);
    if (delayed) {
        struct timespec delay = {0, 300000000};
        nanosleep(&delay, NULL); /* no host-deadline unlock while FIFO is closed */
    }
    pthread_mutex_lock(&control);
    premature |= first_returned;
    release_first = 1;
    pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&control);
    pthread_join(writer, NULL);
    pthread_join(consumer, NULL);
    printf("premature=%d payloads=%u,%u handled=%d,%d\n",
           premature, observed[0], observed[1], results[0], results[1]);
    return premature || observed[0] != 5 || observed[1] != 1 ||
           results[0] != 1 || results[1] != 1;
}
