/* MetalFX on a real GPU: a 96x72 frame comes back at 192x144 with its shape
 * kept (a bright square stays where it was, scaled), and a frame that would
 * shrink is passed through untouched. Skips (77) without Metal or MetalFX. */
#import "../../ports/macos/overrides/upscale_metal.h"
#import <MetalFX/MetalFX.h>
#include <stdio.h>

enum { W = 96, H = 72 };

int main(void)
{
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device || ![MTLFXSpatialScalerDescriptor supportsDevice:device]) {
            puts("MetalFX: unsupported here, skipped");
            return 77;
        }
        static uint8_t pixels[W * H * 4];
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                uint8_t v = x >= 24 && x < 48 && y >= 18 && y < 36 ? 230 : 20;
                uint8_t *p = pixels + (y * W + x) * 4;
                p[0] = p[1] = p[2] = v; p[3] = 255;
            }
        MTLTextureDescriptor *desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
            width:W height:H mipmapped:NO];
        desc.usage = MTLTextureUsageShaderRead;
        id<MTLTexture> source = [device newTextureWithDescriptor:desc];
        [source replaceRegion:MTLRegionMake2D(0, 0, W, H) mipmapLevel:0 withBytes:pixels bytesPerRow:W * 4];
        id<MTLCommandQueue> queue = [device newCommandQueue];
        id<MTLCommandBuffer> command = [queue commandBuffer];
        if (black_UpscaleApply(device, command, source, W / 2, H / 2) != source) return 1;
        id<MTLTexture> out = black_UpscaleApply(device, command, source, W * 2, H * 2);
        if (out == source || out.width != W * 2 || out.height != H * 2) return 2;
        id<MTLBuffer> buffer = [device newBufferWithLength:W * H * 16 options:MTLResourceStorageModeShared];
        id<MTLBlitCommandEncoder> blit = [command blitCommandEncoder];
        [blit copyFromTexture:out sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
                   sourceSize:MTLSizeMake(W * 2, H * 2, 1) toBuffer:buffer destinationOffset:0
       destinationBytesPerRow:W * 8 destinationBytesPerImage:W * H * 16];
        [blit endEncoding];
        [command commit];
        [command waitUntilCompleted];
        if (command.error) { fprintf(stderr, "%s\n", command.error.localizedDescription.UTF8String); return 3; }
        const uint8_t *r = buffer.contents;
        int inside = r[((36 * W * 2) + 72) * 4 + 1], outside = r[((100 * W * 2) + 150) * 4 + 1];
        printf("MetalFX: inside %d, outside %d\n", inside, outside);
        [buffer release]; [queue release]; [source release];
        if (inside < 200 || outside > 50) return 4;
        puts("MetalFX spatial upscale, pass-through when not larger: PASS");
    }
    return 0;
}
