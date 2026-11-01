#pragma once
#include "../runtime/src/kernel/kernel.h"

typedef struct { KIRQL irql, floor; unsigned depth; } BlackInterruptScope;
BlackInterruptScope black_InterruptEnter(KIRQL level);
BlackInterruptScope black_InterruptEnterObject(uint32_t interrupt, KIRQL fallback);
void black_InterruptLeave(BlackInterruptScope previous);
void black_InterruptRemember(uint32_t interrupt, KIRQL level);
KIRQL black_InterruptLevel(uint32_t interrupt, KIRQL fallback);

/* Used only by the HAL translation unit. Host ISR scopes do not own the
 * DISPATCH mutex simply because their synthetic IRQL is above level 2. */
#ifdef BLACK_INTERRUPT_HAL
static _Thread_local unsigned black_interrupt_depth;
static _Thread_local KIRQL black_interrupt_floor;
#endif
