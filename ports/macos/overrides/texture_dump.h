#ifndef BLACK_TEXTURE_DUMP_H
#define BLACK_TEXTURE_DUMP_H
#include <stddef.h>
#include <stdint.h>
void black_TextureDump(unsigned format,unsigned width,unsigned height,const uint8_t *data,size_t bytes,const uint8_t *palette,size_t palette_bytes);
#endif
