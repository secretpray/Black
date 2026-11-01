#pragma once
#include <stdint.h>
int black_IrqlTryEnterDispatch(void);
void black_IrqlLeaveDispatch(void);
uint32_t black_audio_queue_completion(uint32_t voice);
