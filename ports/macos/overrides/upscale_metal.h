#ifndef BLACK_UPSCALE_METAL_H
#define BLACK_UPSCALE_METAL_H
#import <Metal/Metal.h>

/* MetalFX spatial upscaling of a finished frame to the size it is shown at.
 * Encodes into `command` and returns the upscaled texture; returns `source`
 * itself when MetalFX is unsupported, the output is not larger than the
 * source, or the scaler cannot be built (logged once). */
id<MTLTexture> black_UpscaleApply(id<MTLDevice> device, id<MTLCommandBuffer> command, id<MTLTexture> source,
    NSUInteger out_w, NSUInteger out_h);
#endif
