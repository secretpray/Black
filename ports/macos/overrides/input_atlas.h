/* Host-only expanded font pictures: never consume the title's small file heap. */
#pragma once
#include <stdint.h>
#include <stddef.h>
typedef struct { unsigned width,height; const uint8_t *rgba; } BlackInputAtlas;
int black_InputAtlasFind(const void *top,size_t bytes,unsigned width,unsigned height,unsigned format,BlackInputAtlas *out);
