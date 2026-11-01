/* SMAA for the Metal presenter. The reference shader (Jimenez et al., MIT) is
 * HLSL-flavoured C with a custom-language hook; Metal takes it after a few
 * mechanical rewrites: out/inout parameters become thread references, array
 * parameters thread pointers, discard discard_fragment(), and the bool4
 * conditional move stops binding swizzles to references. Settings as in TGP's
 * D3D12 renderer. */
#import "smaa_metal.h"
#include <stdio.h>

extern const char *const black_smaa_reference __attribute__((weak));
extern const unsigned char *const black_smaa_area __attribute__((weak));
extern const unsigned char *const black_smaa_search __attribute__((weak));

enum { AREA_W = 160, AREA_H = 560, SEARCH_W = 64, SEARCH_H = 16 };

static NSString *const config =
    @"#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "#define SMAA_CUSTOM_SL\n"
    "#define SMAA_RT_METRICS float4(1.0 / %lu.0, 1.0 / %lu.0, %lu.0, %lu.0)\n"
    "#define SMAA_THRESHOLD 0.05\n"
    "#define SMAA_MAX_SEARCH_STEPS 16\n"
    "#define SMAA_MAX_SEARCH_STEPS_DIAG 8\n"
    "#define SMAA_CORNER_ROUNDING 25\n"
    "constexpr sampler LinearSampler(coord::normalized, address::clamp_to_edge, filter::linear);\n"
    "constexpr sampler PointSampler(coord::normalized, address::clamp_to_edge, filter::nearest);\n"
    "#define SMAATexture2D(tex) texture2d<float> tex\n"
    "#define SMAATexturePass2D(tex) tex\n"
    "#define SMAASampleLevelZero(tex, coord) tex.sample(LinearSampler, coord, level(0))\n"
    "#define SMAASampleLevelZeroPoint(tex, coord) tex.sample(PointSampler, coord, level(0))\n"
    "#define SMAASampleLevelZeroOffset(tex, coord, offset) tex.sample(LinearSampler, coord, level(0), offset)\n"
    "#define SMAASample(tex, coord) tex.sample(LinearSampler, coord)\n"
    "#define SMAASamplePoint(tex, coord) tex.sample(PointSampler, coord)\n"
    "#define SMAASampleOffset(tex, coord, offset) tex.sample(LinearSampler, coord, offset)\n"
    "#define SMAAGather(tex, coord) tex.gather(LinearSampler, coord)\n"
    "#define SMAA_FLATTEN\n"
    "#define SMAA_BRANCH\n"
    "#define mad(a, b, c) ((a) * (b) + (c))\n"
    "#define lerp(a, b, t) mix(a, b, t)\n"
    "#define frac(x) fract(x)\n";

/* One quad; each pass computes the reference's vertex-stage offsets per pixel,
 * which is exact: they are affine in the texture coordinate. */
static NSString *const passes =
    @"struct SmaaOut { float4 position [[position]]; float2 uv; };\n"
    "vertex SmaaOut smaa_vs(uint id [[vertex_id]]) {\n"
    "  const float2 p[] = { {-1,-1}, {1,-1}, {-1,1}, {1,1} };\n"
    "  const float2 t[] = { {0,1}, {1,1}, {0,0}, {1,0} };\n"
    "  SmaaOut o; o.position = float4(p[id], 0, 1); o.uv = t[id]; return o;\n"
    "}\n"
    "fragment float4 smaa_edges(SmaaOut in [[stage_in]], texture2d<float> color [[texture(0)]]) {\n"
    "  float4 offset[3]; SMAAEdgeDetectionVS(in.uv, offset);\n"
    "  return float4(SMAALumaEdgeDetectionPS(in.uv, offset, color), 0, 0);\n"
    "}\n"
    "fragment float4 smaa_weights(SmaaOut in [[stage_in]], texture2d<float> edges [[texture(0)]],\n"
    "    texture2d<float> area [[texture(1)]], texture2d<float> search [[texture(2)]]) {\n"
    "  float2 pix; float4 offset[3]; SMAABlendingWeightCalculationVS(in.uv, pix, offset);\n"
    "  return SMAABlendingWeightCalculationPS(in.uv, pix, offset, edges, area, search, float4(0));\n"
    "}\n"
    "fragment float4 smaa_blend(SmaaOut in [[stage_in]], texture2d<float> color [[texture(0)]],\n"
    "    texture2d<float> blend [[texture(1)]]) {\n"
    "  float4 offset; SMAANeighborhoodBlendingVS(in.uv, offset);\n"
    "  return SMAANeighborhoodBlendingPS(in.uv, offset, color, blend);\n"
    "}\n";

static void rewrite(NSMutableString *s, NSString *pattern, NSString *template)
{
    NSRegularExpression *re = [NSRegularExpression regularExpressionWithPattern:pattern options:0 error:NULL];
    [re replaceMatchesInString:s options:0 range:NSMakeRange(0, s.length) withTemplate:template];
}

NSString *black_SmaaMetalSource(NSUInteger w, NSUInteger h)
{
    if (!&black_smaa_reference || !black_smaa_reference) return nil;
    NSMutableString *body = [[NSMutableString alloc] initWithUTF8String:black_smaa_reference];
    rewrite(body, @"\\bout\\s+(float4)\\s+(\\w+)\\[3\\]", @"thread $1 *$2");
    rewrite(body, @"\\b(float4)\\s+(offset)\\[3\\]", @"thread $1 *$2");
    rewrite(body, @"\\binout\\s+(float[234]?|bool[234]?)\\s+(\\w+)", @"thread $1 &$2");
    rewrite(body, @"\\bout\\s+(float[234]?|bool[234]?)\\s+(\\w+)", @"thread $1 &$2");
    rewrite(body, @"\\bdiscard\\s*;", @"discard_fragment();");
    rewrite(body, @"SMAAMovc\\(cond\\.xy, variable\\.xy, value\\.xy\\);\\s*SMAAMovc\\(cond\\.zw, variable\\.zw, value\\.zw\\);",
        @"if (cond.x) variable.x = value.x; if (cond.y) variable.y = value.y;\n"
         "    if (cond.z) variable.z = value.z; if (cond.w) variable.w = value.w;");
    rewrite(body, @"(SMAADetect(?:Horizontal|Vertical)CornerPattern)\\(SMAATexturePass2D\\(edgesTex\\), weights\\.(\\w\\w), ([^;]*)\\);",
        @"{ float2 corner = weights.$2; $1(SMAATexturePass2D(edgesTex), corner, $3); weights.$2 = corner; }");
    NSString *head = [NSString stringWithFormat:config, (unsigned long)w, (unsigned long)h,
        (unsigned long)w, (unsigned long)h];
    NSString *source = [[NSString alloc] initWithFormat:@"%@%@\n%@", head, body, passes];
    [body release];
    return source;
}

typedef struct {
    NSUInteger w, h;
    id<MTLRenderPipelineState> edges, weights, blend;
    id<MTLTexture> edge_tex, weight_tex, out_tex;
} SmaaTarget;

static SmaaTarget s_targets[2];
static unsigned s_next;
static id<MTLTexture> s_area, s_search;
static id<MTLDevice> s_owner;
static int s_failed;

static id<MTLRenderPipelineState> pipeline(id<MTLDevice> device, id<MTLLibrary> library,
    NSString *fragment, MTLPixelFormat format)
{
    MTLRenderPipelineDescriptor *desc = [MTLRenderPipelineDescriptor new];
    id<MTLFunction> vs = [library newFunctionWithName:@"smaa_vs"];
    id<MTLFunction> fs = [library newFunctionWithName:fragment];
    desc.vertexFunction = vs;
    desc.fragmentFunction = fs;
    desc.colorAttachments[0].pixelFormat = format;
    NSError *error = nil;
    id<MTLRenderPipelineState> state = [device newRenderPipelineStateWithDescriptor:desc error:&error];
    if (!state) fprintf(stderr, "[SMAA] pipeline %s: %s\n", fragment.UTF8String,
        error.localizedDescription.UTF8String);
    [vs release]; [fs release]; [desc release];
    return state;
}

static id<MTLTexture> texture(id<MTLDevice> device, MTLPixelFormat format, NSUInteger w, NSUInteger h,
    MTLTextureUsage usage)
{
    MTLTextureDescriptor *desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format
        width:w height:h mipmapped:NO];
    desc.usage = usage;
    desc.storageMode = MTLStorageModePrivate;
    return [device newTextureWithDescriptor:desc];
}

static void release_target(SmaaTarget *t)
{
    [t->edges release]; [t->weights release]; [t->blend release];
    [t->edge_tex release]; [t->weight_tex release]; [t->out_tex release];
    *t = (SmaaTarget){0};
}

static int lookups(id<MTLDevice> device)
{
    if (s_area && s_search) return 1;
    if (!&black_smaa_area || !black_smaa_area || !black_smaa_search) return 0;
    MTLTextureDescriptor *a = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRG8Unorm
        width:AREA_W height:AREA_H mipmapped:NO];
    MTLTextureDescriptor *s = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm
        width:SEARCH_W height:SEARCH_H mipmapped:NO];
    a.usage = s.usage = MTLTextureUsageShaderRead;
    s_area = [device newTextureWithDescriptor:a];
    s_search = [device newTextureWithDescriptor:s];
    [s_area replaceRegion:MTLRegionMake2D(0, 0, AREA_W, AREA_H) mipmapLevel:0
                withBytes:black_smaa_area bytesPerRow:AREA_W * 2];
    [s_search replaceRegion:MTLRegionMake2D(0, 0, SEARCH_W, SEARCH_H) mipmapLevel:0
                  withBytes:black_smaa_search bytesPerRow:SEARCH_W];
    return s_area && s_search;
}

static SmaaTarget *target(id<MTLDevice> device, NSUInteger w, NSUInteger h)
{
    if (s_owner != device) {
        for (unsigned i = 0; i < 2; ++i) release_target(&s_targets[i]);
        [s_area release]; [s_search release];
        s_area = s_search = nil;
        s_owner = device;
    }
    /* A small cache: movies and gameplay can alternate two sizes. */
    for (unsigned i = 0; i < 2; ++i)
        if (s_targets[i].w == w && s_targets[i].h == h && s_targets[i].blend) return &s_targets[i];
    if (!lookups(device)) return NULL;
    SmaaTarget *t = &s_targets[s_next++ % 2];
    release_target(t);
    NSString *source = black_SmaaMetalSource(w, h);
    if (!source) return NULL;
    NSError *error = nil;
    id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&error];
    [source release];
    if (!library) {
        fprintf(stderr, "[SMAA] shader: %s\n", error.localizedDescription.UTF8String);
        return NULL;
    }
    t->edges = pipeline(device, library, @"smaa_edges", MTLPixelFormatRG8Unorm);
    t->weights = pipeline(device, library, @"smaa_weights", MTLPixelFormatRGBA8Unorm);
    t->blend = pipeline(device, library, @"smaa_blend", MTLPixelFormatBGRA8Unorm);
    [library release];
    MTLTextureUsage rt = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    t->edge_tex = texture(device, MTLPixelFormatRG8Unorm, w, h, rt);
    t->weight_tex = texture(device, MTLPixelFormatRGBA8Unorm, w, h, rt);
    t->out_tex = texture(device, MTLPixelFormatBGRA8Unorm, w, h, rt);
    if (!t->edges || !t->weights || !t->blend || !t->edge_tex || !t->weight_tex || !t->out_tex) {
        release_target(t);
        return NULL;
    }
    t->w = w; t->h = h;
    fprintf(stderr, "[SMAA] passes ready at %lux%lu\n", (unsigned long)w, (unsigned long)h);
    return t;
}

static void pass(id<MTLCommandBuffer> command, id<MTLTexture> output, id<MTLRenderPipelineState> state,
    id<MTLTexture> t0, id<MTLTexture> t1, id<MTLTexture> t2)
{
    MTLRenderPassDescriptor *desc = [MTLRenderPassDescriptor renderPassDescriptor];
    desc.colorAttachments[0].texture = output;
    desc.colorAttachments[0].loadAction = MTLLoadActionClear;
    desc.colorAttachments[0].storeAction = MTLStoreActionStore;
    desc.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);
    id<MTLRenderCommandEncoder> encoder = [command renderCommandEncoderWithDescriptor:desc];
    [encoder setRenderPipelineState:state];
    [encoder setFragmentTexture:t0 atIndex:0];
    if (t1) [encoder setFragmentTexture:t1 atIndex:1];
    if (t2) [encoder setFragmentTexture:t2 atIndex:2];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
    [encoder endEncoding];
}

id<MTLTexture> black_SmaaApply(id<MTLDevice> device, id<MTLCommandBuffer> command, id<MTLTexture> source)
{
    if (s_failed || !device || !command || !source) return source;
    SmaaTarget *t = target(device, source.width, source.height);
    if (!t) {
        s_failed = 1;
        fprintf(stderr, "[SMAA] unavailable; presenting without it\n");
        return source;
    }
    pass(command, t->edge_tex, t->edges, source, nil, nil);
    pass(command, t->weight_tex, t->weights, t->edge_tex, s_area, s_search);
    pass(command, t->out_tex, t->blend, source, t->weight_tex, nil);
    return t->out_tex;
}
