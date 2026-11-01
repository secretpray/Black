/* Production APU IRQ publication plus real platform event wake, no game. */
#include "win32_compat.h"
#include "../../ports/macos/overrides/audio_irq_wake.h"
#include "../../ports/macos/runtime/src/apu/apu_regs.h"
#include <stdint.h>
#include <stdio.h>
typedef struct { uint32_t regs[0x20000]; } MCPXAPUState;
#define qatomic_or(p,v) (*(p) |= (v))
#define qatomic_and(p,v) (*(p) &= (v))
#define PCI_DEVICE(d) (d)
static HANDLE wake;
static unsigned failures, asserts, deasserts;
static void pci_irq_assert(MCPXAPUState *d) { (void)d; ++asserts; }
static void pci_irq_deassert(MCPXAPUState *d) { (void)d; ++deasserts; }
static void black_KernelWakeDeviceIRQ(void) { black_audio_irq_wake(wake); }
#include BLACK_AUDIO_IRQ_SOURCE
static void check(int pass, const char *name)
{ if (!pass) { ++failures; fprintf(stderr,"FAIL %s\n",name); } }
int main(void)
{
    MCPXAPUState d = {0};
    black_audio_irq_wake(NULL);
    wake = CreateEventA(NULL, FALSE, FALSE, NULL);
    check(wake != NULL, "event created");
    d.regs[NV_PAPU_IEN] = NV_PAPU_ISTS_GINTSTS | NV_PAPU_ISTS_FENINTSTS;
    d.regs[NV_PAPU_ISTS] = NV_PAPU_ISTS_FENINTSTS;
    update_irq(&d);
    check(WaitForSingleObject(wake, 5) == WAIT_OBJECT_0, "pending voice completion wakes kernel before 10-ms poll");
    check(asserts == 1 && (d.regs[NV_PAPU_ISTS] & NV_PAPU_ISTS_GINTSTS), "native IRQ bit remains real");
    check(WaitForSingleObject(wake, 0) == WAIT_TIMEOUT, "one wake consumed once");
    d.regs[NV_PAPU_ISTS] = 0; update_irq(&d);
    check(WaitForSingleObject(wake, 0) == WAIT_TIMEOUT && deasserts == 1, "acknowledged IRQ does not wake");
    d.regs[NV_PAPU_ISTS] = NV_PAPU_ISTS_FENINTSTS; d.regs[NV_PAPU_IEN] = 0; update_irq(&d);
    check(WaitForSingleObject(wake, 0) == WAIT_TIMEOUT, "disabled IRQ does not wake");
    d.regs[NV_PAPU_FECTL] = NV_PAPU_FECTL_FEMETHMODE_TRAPPED;
    /* fe_method owns this latch; FECTL alone is no longer an IRQ event. */
    d.regs[NV_PAPU_ISTS] = NV_PAPU_ISTS_FETINTSTS;
    d.regs[NV_PAPU_IEN] = NV_PAPU_ISTS_GINTSTS | NV_PAPU_ISTS_FETINTSTS; update_irq(&d);
    check(WaitForSingleObject(wake, 5) == WAIT_OBJECT_0, "FE trap wakes native ISR path");
    CloseHandle(wake);
    printf("APU IRQ wake: %u failures\n", failures);
    return failures ? 1 : 0;
}
