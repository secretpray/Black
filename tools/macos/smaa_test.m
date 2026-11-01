/* SMAA on a real GPU: the translated reference compiles, and a hard diagonal
 * edge gains intermediate values while flat areas stay exact. Skips (77) on
 * a machine without a Metal device. */
#import "../../ports/macos/overrides/smaa_metal.h"
#include <stdio.h>

enum { W = 96, H = 96 };

int main(void)
{
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) { puts("SMAA: no Metal device, skipped"); return 77; }
        NSString *msl = black_SmaaMetalSource(W, H);
        if (!msl) return 1;
        NSError *error = nil;
        id<MTLLibrary> library = [device newLibraryWithSource:msl options:nil error:&error];
        [msl release];
        if (!library) { fprintf(stderr, "%s\n", error.localizedDescription.UTF8String); return 2; }
        [library release];

        /* White above the line y = 0.4 x + 20, black below: a shallow edge,
         * the case SMAA's area lookup exists for. */
        static uint8_t pixels[W * H * 4];
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                uint8_t v = y < 0.4 * x + 20 ? 255 : 0;
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
        id<MTLTexture> out = black_SmaaApply(device, command, source);
        if (out == source) return 3;
        id<MTLBuffer> buffer = [device newBufferWithLength:W * H * 4 options:MTLResourceStorageModeShared];
        id<MTLBlitCommandEncoder> blit = [command blitCommandEncoder];
        [blit copyFromTexture:out sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
                   sourceSize:MTLSizeMake(W, H, 1) toBuffer:buffer destinationOffset:0
       destinationBytesPerRow:W * 4 destinationBytesPerImage:W * H * 4];
        [blit endEncoding];
        [command commit];
        [command waitUntilCompleted];
        if (command.error) { fprintf(stderr, "%s\n", command.error.localizedDescription.UTF8String); return 4; }
        const uint8_t *result = buffer.contents;
        int blended = 0, flat_changed = 0;
        for (int y = 2; y < H - 2; ++y)
            for (int x = 2; x < W - 2; ++x) {
                uint8_t v = result[(y * W + x) * 4 + 1];
                double edge = 0.4 * x + 20 - y;
                if (edge > 3 || edge < -3) flat_changed += v != pixels[(y * W + x) * 4 + 1];
                else blended += v > 8 && v < 247;
            }
        [buffer release]; [queue release]; [source release];
        printf("SMAA: %d blended edge pixels, %d changed flat pixels\n", blended, flat_changed);
        if (blended < W / 2) return 5;
        if (flat_changed) return 6;
        puts("SMAA Metal translation, lookups and three passes: PASS");
    }
    return 0;
}
