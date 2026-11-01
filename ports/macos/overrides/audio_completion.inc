/* Adapted from portingpete/Black-2006-Recomp 2041e5a97f1300dca128d51fc06e1ae32cea9f21
 * src/recomp_manual.c (MIT). The ISR must not edit native voice lists while
 * another host thread owns DISPATCH. Never block an ISR waiting for a thread
 * that may itself await the hardware acknowledgement. The native busy return
 * leaves the inactive voice linked for retry after the FE trap is released. */
void sub_0021C7EF(void)
{
    if (!black_IrqlTryEnterDispatch()) {
        g_eax = MEM32(g_esp + 4);
        g_esp += 8;
        return;
    }
    black_OriginalAudioCompletion();
    black_IrqlLeaveDispatch();
}

/* A completion owns one embedded list link. Restarting a voice before the
 * deferred callback drains it must not insert that link a second time.
 * Status, work flags and the actual DPC remain in generated native code. */
uint32_t black_audio_queue_completion(uint32_t voice)
{
    uint32_t node = voice + 0x54;
    uint32_t head = MEM32(voice + 8) + 0x4A0;
    if (MEM32(node) == node && MEM32(node + 4) == node) {
        uint32_t tail = MEM32(head + 4);
        MEM32(node) = head;
        MEM32(node + 4) = tail;
        MEM32(tail) = node;
        MEM32(head + 4) = node;
    }
    return head;
}
