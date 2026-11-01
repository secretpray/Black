/* Caller owns d->lock. Publish before waiting for a guest-owned voice lock:
 * the connected ISR may be the only thread able to release that voice. */
void black_apu_publish_pending_irq(MCPXAPUState *d)
{
    if (d->set_irq) {
        d->set_irq = false;
        update_irq(d);
    }
}
