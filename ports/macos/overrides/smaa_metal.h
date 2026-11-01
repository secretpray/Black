#ifndef BLACK_SMAA_METAL_H
#define BLACK_SMAA_METAL_H
#import <Metal/Metal.h>

/* SMAA 1x on a finished frame: luma edges, blend weights from the area and
 * search lookups, neighbourhood blending. Encodes three passes into `command`
 * at the source's own size and returns the antialiased texture, or `source`
 * itself when the passes cannot be built (logged once). */
id<MTLTexture> black_SmaaApply(id<MTLDevice> device, id<MTLCommandBuffer> command, id<MTLTexture> source);

/* The reference shader as Metal Shading Language for a w x h target; the
 * caller releases it. NULL if the reference is missing. */
NSString *black_SmaaMetalSource(NSUInteger w, NSUInteger h);
#endif
