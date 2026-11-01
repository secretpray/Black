/* MetalFX spatial upscaling for the presenter. The title renders at the
 * chosen scale; MetalFX brings that frame to the window's pixel size instead
 * of the bilinear stretch of the presentation pass, so a cheaper render scale
 * still lands sharp on a Retina display. Apple asks for an antialiased input:
 * SMAA runs before it when selected. */
#import "upscale_metal.h"
#import <MetalFX/MetalFX.h>
#include <stdio.h>

static id<MTLFXSpatialScaler> s_scaler;
static id<MTLTexture> s_output;
static id<MTLDevice> s_owner;
static NSUInteger s_in_w, s_in_h, s_out_w, s_out_h;
static int s_failed;

static void reset(void)
{
    [s_scaler release]; s_scaler = nil;
    [s_output release]; s_output = nil;
    s_in_w = s_in_h = s_out_w = s_out_h = 0;
}

static int build(id<MTLDevice> device, NSUInteger in_w, NSUInteger in_h, NSUInteger out_w, NSUInteger out_h)
{
    reset();
    s_owner = device;
    MTLFXSpatialScalerDescriptor *desc = [MTLFXSpatialScalerDescriptor new];
    desc.inputWidth = in_w;
    desc.inputHeight = in_h;
    desc.outputWidth = out_w;
    desc.outputHeight = out_h;
    desc.colorTextureFormat = MTLPixelFormatBGRA8Unorm;
    desc.outputTextureFormat = MTLPixelFormatBGRA8Unorm;
    desc.colorProcessingMode = MTLFXSpatialScalerColorProcessingModePerceptual;
    s_scaler = [desc newSpatialScalerWithDevice:device];
    [desc release];
    if (!s_scaler) return 0;
    MTLTextureDescriptor *out = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
        width:out_w height:out_h mipmapped:NO];
    out.usage = s_scaler.outputTextureUsage | MTLTextureUsageShaderRead;
    out.storageMode = MTLStorageModePrivate;
    s_output = [device newTextureWithDescriptor:out];
    if (!s_output) { reset(); return 0; }
    s_in_w = in_w; s_in_h = in_h; s_out_w = out_w; s_out_h = out_h;
    fprintf(stderr, "[METALFX] spatial upscale %lux%lu -> %lux%lu\n", (unsigned long)in_w, (unsigned long)in_h,
        (unsigned long)out_w, (unsigned long)out_h);
    return 1;
}

id<MTLTexture> black_UpscaleApply(id<MTLDevice> device, id<MTLCommandBuffer> command, id<MTLTexture> source,
    NSUInteger out_w, NSUInteger out_h)
{
    if (s_failed || !device || !command || !source) return source;
    NSUInteger in_w = source.width, in_h = source.height;
    /* Only ever upward: a frame already as large as the screen is shown as is. */
    if (out_w <= in_w || out_h <= in_h) return source;
    if (!(source.usage & MTLTextureUsageShaderRead)) return source;
    if (s_owner != device || in_w != s_in_w || in_h != s_in_h || out_w != s_out_w || out_h != s_out_h) {
        if (![MTLFXSpatialScalerDescriptor supportsDevice:device] || !build(device, in_w, in_h, out_w, out_h)) {
            s_failed = 1;
            fprintf(stderr, "[METALFX] unavailable; presenting with the bilinear stretch\n");
            return source;
        }
    }
    s_scaler.colorTexture = source;
    s_scaler.outputTexture = s_output;
    s_scaler.inputContentWidth = in_w;
    s_scaler.inputContentHeight = in_h;
    [s_scaler encodeToCommandBuffer:command];
    return s_output;
}
