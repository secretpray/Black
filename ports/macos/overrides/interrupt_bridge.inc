/* The original bridge still owns guest lookup, stdcall stack and results.
 * Device IRQL/gate scopes wrap its actual callbacks, including failure paths. */
static void bridge_KeInitializeInterrupt(void)
{
    uint32_t object = STACK_ARG(0);
    KIRQL level = (KIRQL)STACK_ARG(4);
    BlackInterruptScope scope = black_InterruptEnter(0);
    black_OriginalInitializeInterrupt();
    black_InterruptRemember(object, level);
    black_InterruptLeave(scope);
}

static void bridge_KeSynchronizeExecution(void)
{
    uint32_t object = STACK_ARG(0);
    KIRQL fallback = object ? (KIRQL)BRIDGE_MEM32(object + 8) : DISPATCH_LEVEL;
    BlackInterruptScope scope = black_InterruptEnterObject(object, fallback);
    black_OriginalSynchronizeExecution();
    static _Thread_local unsigned calls;
    ++calls;
    if (getenv("BLACK_SYNC_LOG") && (calls <= 4 || (calls & (calls - 1)) == 0))
        fprintf(stderr, "[SYNC-SCOPE] object=%08X level=%u calls=%u result=%u\n",
                object, (unsigned)black_InterruptLevel(object, fallback), calls, g_eax);
    black_InterruptLeave(scope);
}

static int kernel_raise_interrupt(uint32_t vector)
{
    uint32_t object = xbox_GetConnectedInterrupt(vector);
    if (!object) return -1;
    BlackInterruptScope scope = black_InterruptEnterObject(object, (KIRQL)vector);
    int claimed = black_OriginalRaiseInterrupt(vector);
    static _Thread_local unsigned calls;
    ++calls;
    if (getenv("BLACK_SYNC_LOG") && (calls <= 4 || (calls & (calls - 1)) == 0))
        fprintf(stderr, "[IRQ-SCOPE] vector=%u object=%08X level=%u calls=%u claimed=%d\n",
                vector, object, (unsigned)black_InterruptLevel(object, (KIRQL)vector), calls, claimed);
    black_InterruptLeave(scope);
    return claimed;
}
