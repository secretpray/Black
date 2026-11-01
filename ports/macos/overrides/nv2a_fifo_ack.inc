/* BLACK's ISR 0x273EA0 disables PGRAPH FIFO access, clears ERROR (W1C),
 * then reads TRAPPED_DATA and handles the software method. Only its final
 * FIFO_ACCESS=1 releases the producer. Returning on W1C alone lets the next
 * method overwrite the payload before the ISR has consumed it.
 * Never synthesize a guest event or reopen the FIFO on a host deadline. */
int xbox_Nv2aSoftwareInterrupt(uint32_t trapped_addr, uint32_t trapped_data,
                              uint32_t nsource)
{
    int handled = black_OriginalNv2aSoftwareInterrupt(trapped_addr, trapped_data, nsource);
    if (!handled)
        return handled;
    struct timespec delay = { 0, 50000 };
    unsigned waits = 0;
    for (;;) {
        pthread_mutex_lock(&g_nv2a_irq_lock);
        uint32_t access = *nv2a_irq_slot(0x400720u);
        pthread_mutex_unlock(&g_nv2a_irq_lock);
        if (access & 1u)
            return handled;
#ifdef BLACK_NV2A_FIFO_TEST_HOOKS
        black_nv2a_fifo_wait_test_hook();
#endif
        if (!waits++) {
            static _Thread_local unsigned handoffs;
            ++handoffs;
            if (getenv("BLACK_SYNC_LOG") && (handoffs <= 4 || !(handoffs & (handoffs - 1))))
                fprintf(stderr, "[NV2A-FIFO] param=%08X W1C acknowledged; waiting for ISR FIFO_ACCESS, handoffs=%u\n",
                        trapped_data, handoffs);
        }
        nanosleep(&delay, NULL);
    }
}
