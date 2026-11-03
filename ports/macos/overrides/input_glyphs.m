/* Original button artwork. Burnout Revenge's pad_glyphs is a visual/product
 * reference; no donor code, game atlas or third-party artwork is copied. */
#import "input_glyphs.h"
#import <CoreText/CoreText.h>
#include "keyboard_mouse.h"
#include "input_atlas.h"
#include <pthread.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

int black_InputGlyphValid(unsigned code)
{
    return (code>=0xE100 && code<0xE120) || code==0xE1FF ||
        (code>=0xE200 && code<0xE200+BLACK_KEY_LIMIT && black_KeyValid((int)code-0xE200));
}
static void color(CGContextRef c,unsigned rgb)
{ CGContextSetRGBFillColor(c,((rgb>>16)&255)/255.,((rgb>>8)&255)/255.,(rgb&255)/255.,1); }
static void stroke(CGContextRef c,unsigned rgb)
{ CGContextSetRGBStrokeColor(c,((rgb>>16)&255)/255.,((rgb>>8)&255)/255.,(rgb&255)/255.,1); }
static void rounded(CGContextRef c,CGRect r,CGFloat radius,unsigned rgb)
{
    CGPathRef p=CGPathCreateWithRoundedRect(r,radius,radius,NULL);
    color(c,rgb);CGContextAddPath(c,p);CGContextFillPath(c);CGPathRelease(p);
}
static void label(CGContextRef c,NSString *s,CGFloat x,CGFloat y,CGFloat size,CGFloat maxWidth,unsigned rgb)
{
    CTFontRef f=CTFontCreateWithName(CFSTR("Arial-BoldMT"),size,NULL);
    CGColorSpaceRef cs=CGColorSpaceCreateDeviceRGB();
    CGFloat rgba[]={((rgb>>16)&255)/255.,((rgb>>8)&255)/255.,(rgb&255)/255.,1};
    CGColorRef col=CGColorCreate(cs,rgba);CGColorSpaceRelease(cs);
    NSAttributedString *a=[[[NSAttributedString alloc] initWithString:s attributes:@{
        (id)kCTFontAttributeName:(id)f,(id)kCTForegroundColorAttributeName:(id)col}] autorelease];
    CTLineRef line=CTLineCreateWithAttributedString((CFAttributedStringRef)a);
    CGRect b=CTLineGetBoundsWithOptions(line,kCTLineBoundsUseGlyphPathBounds);
    CGFloat sx=MIN(1,maxWidth/MAX(1,b.size.width));
    CGContextSaveGState(c);CGContextTranslateCTM(c,x,y);CGContextScaleCTM(c,sx,-1);
    CGContextSetTextMatrix(c,CGAffineTransformIdentity);
    CGContextSetTextPosition(c,-CGRectGetMidX(b),-CGRectGetMidY(b));CTLineDraw(line,c);
    CGContextRestoreGState(c);CFRelease(line);CFRelease(f);CGColorRelease(col);
}
static void segment(CGContextRef c,CGFloat x,CGFloat y,CGFloat a,CGFloat b)
{ CGContextMoveToPoint(c,x,y);CGContextAddLineToPoint(c,a,b); }
static void arrow(CGContextRef c,int direction,CGFloat cx,CGFloat cy)
{
    CGContextSaveGState(c);CGContextTranslateCTM(c,cx,cy);CGContextRotateCTM(c,direction*M_PI/2);
    color(c,0xe1e5e1);CGContextMoveToPoint(c,0,-6);CGContextAddLineToPoint(c,5,2);
    CGContextAddLineToPoint(c,2,2);CGContextAddLineToPoint(c,2,6);CGContextAddLineToPoint(c,-2,6);
    CGContextAddLineToPoint(c,-2,2);CGContextAddLineToPoint(c,-5,2);CGContextClosePath(c);CGContextFillPath(c);
    CGContextRestoreGState(c);
}
CGImageRef black_InputGlyphCreate(unsigned code,unsigned height)
{
    if(!black_InputGlyphValid(code)||height<8||height>256)return NULL;
    int pad=code>=0xE100&&code<0xE120,slot=pad?(code-0xE100)%16:-1,ps=pad&&code>=0xE110;
    int key=pad?-1:(int)code-0xE200,mouse=key>=BLACK_MOUSE_LEFT&&key<=BLACK_WHEEL_DOWN;
    NSString *name=code==0xE1FF?@"?":pad?@"":[@(black_KeyName(key)) uppercaseString];
    if(key==53)name=@"ESC";if(key==36||key==76)name=@"ENTER";
    if(key==51)name=@"BKSP";if(key==117)name=@"DEL";
    if(key==BLACK_MOUSE_4)name=@"4";if(key==BLACK_MOUSE_5)name=@"5";
    CGFloat width=32;
    if(pad&&slot>=4&&slot<=7)width=44;
    if(pad&&(slot==12||slot==13))width=42;
    if(!pad&&!mouse)width=MAX(32,MIN(84,12+name.length*8));
    unsigned w=(unsigned)ceil(width*height/32.),h=height,ss=4;
    CGColorSpaceRef cs=CGColorSpaceCreateDeviceRGB();
    CGContextRef c=CGBitmapContextCreate(NULL,w*ss,h*ss,8,w*ss*4,cs,kCGImageAlphaPremultipliedLast|kCGBitmapByteOrder32Big);
    if(!c){CGColorSpaceRelease(cs);return NULL;}
    CGContextScaleCTM(c,w*ss/width,h*ss/32.);CGContextTranslateCTM(c,0,32);CGContextScaleCTM(c,1,-1);
    CGContextSetLineCap(c,kCGLineCapRound);CGContextSetLineJoin(c,kCGLineJoinRound);
    if(pad&&slot<4){
        color(c,0x080e0c);CGContextFillEllipseInRect(c,CGRectMake(1,2,30,30));
        color(c,0x829087);CGContextFillEllipseInRect(c,CGRectMake(1,0,30,30));
        color(c,0x202b26);CGContextFillEllipseInRect(c,CGRectMake(2.2,1.2,27.6,27.6));
        color(c,0x313f37);CGContextFillEllipseInRect(c,CGRectMake(4,2.5,24,11));
        static const unsigned xbox[]={0x8bd45f,0xf17568,0x77b5ef,0xf2d261};
        static const unsigned play[]={0x8cb6eb,0xef8d96,0xe1a3d8,0x8fd5b4};
        if(!ps)label(c,(@[@"A",@"B",@"X",@"Y"])[slot],16,15,21,19,xbox[slot]);
        else {
            stroke(c,play[slot]);CGContextSetLineWidth(c,2.2);
            if(slot==0){segment(c,10,9,22,21);segment(c,22,9,10,21);}
            if(slot==1)CGContextAddEllipseInRect(c,CGRectMake(8.5,7.5,15,15));
            if(slot==2)CGContextAddRect(c,CGRectMake(9,8,14,14));
            if(slot==3){CGContextMoveToPoint(c,16,7);CGContextAddLineToPoint(c,24,22);CGContextAddLineToPoint(c,8,22);CGContextClosePath(c);}
            CGContextStrokePath(c);
        }
    }else if(mouse){
        rounded(c,CGRectMake(5,0,22,31),10,0x080e0c);
        rounded(c,CGRectMake(5,0,22,29),10,0x9da89f);
        rounded(c,CGRectMake(6.3,1.3,19.4,26.4),8.6,0x28362d);
        if(key==BLACK_MOUSE_LEFT||key==BLACK_MOUSE_RIGHT){
            CGRect r=CGRectMake(key==BLACK_MOUSE_LEFT?7:16.7,2.3,8.3,12);
            rounded(c,r,3,0xd4dfd5);
        }
        stroke(c,0x9da89f);CGContextSetLineWidth(c,1);segment(c,6,15,26,15);segment(c,16,1,16,14);CGContextStrokePath(c);
        rounded(c,CGRectMake(14,6,4,7),1.8,0x9da89f);
        if(key==BLACK_MOUSE_MIDDLE)rounded(c,CGRectMake(14,6,4,7),1.8,0xf0eee0);
        if(key==BLACK_WHEEL_UP||key==BLACK_WHEEL_DOWN)arrow(c,key==BLACK_WHEEL_UP?0:2,16,19);
        if(key==BLACK_MOUSE_4||key==BLACK_MOUSE_5)label(c,name,16,21,11,12,0xe1e5e1);
    }else{
        CGFloat radius=pad?(slot>=14?13:6):4;
        rounded(c,CGRectMake(1,2,width-2,30),radius,0x080e0c);
        rounded(c,CGRectMake(1,0,width-2,29),radius,0x95a196);
        rounded(c,CGRectMake(2.3,1.3,width-4.6,25.8),MAX(2,radius-1.3),0x29382e);
        if(pad&&slot>=8&&slot<12){static const int dirs[]={0,2,3,1};arrow(c,dirs[slot-8],width/2,14);}
        else if(!pad&&key>=123&&key<=126){static const int dirs[]={3,1,2,0};arrow(c,dirs[key-123],width/2,14);}
        else {
            if(pad){static NSString *const xb[]={@"RB",@"LB",@"LT",@"RT"};static NSString *const p[]={@"R1",@"L1",@"L2",@"R2"};
                if(slot<=7)name=ps?p[slot-4]:xb[slot-4];
                if(slot==12)name=ps?@"≡":@"▶";
                if(slot==13)name=ps?@"•••":@"◀";
                if(slot>=14)name=ps?(slot==14?@"L3":@"R3"):(slot==14?@"LS":@"RS");
            }
            label(c,name,width/2,14,16,width-9,0xe1e5e1);
        }
    }
    CGImageRef big=CGBitmapContextCreateImage(c);CGContextRelease(c);
    CGContextRef small=CGBitmapContextCreate(NULL,w,h,8,w*4,cs,kCGImageAlphaPremultipliedLast|kCGBitmapByteOrder32Big);
    CGColorSpaceRelease(cs);CGContextSetInterpolationQuality(small,kCGInterpolationHigh);
    CGContextDrawImage(small,CGRectMake(0,0,w,h),big);CGImageRelease(big);
    CGImageRef image=CGBitmapContextCreateImage(small);CGContextRelease(small);return image;
}

static uint32_t u32(const uint8_t *b){uint32_t n;memcpy(&n,b,4);return n;}
static uint16_t u16(const uint8_t *b){uint16_t n;memcpy(&n,b,2);return n;}
static float f32(const uint8_t *b){float n;memcpy(&n,b,4);return n;}
static void put32(uint8_t *b,uint32_t n){memcpy(b,&n,4);}
static NSData *invalid(NSError **e,NSString *reason)
{if(e)*e=[NSError errorWithDomain:@"BLACK.InputArtwork" code:1 userInfo:@{NSLocalizedDescriptionKey:reason}];return nil;}
static unsigned rgb565(const unsigned *c){return ((c[0]*31/255)<<11)|((c[1]*63/255)<<5)|(c[2]*31/255);}
static void unpack(unsigned n,unsigned *c){c[0]=((n>>11)&31)*255/31;c[1]=((n>>5)&63)*255/63;c[2]=(n&31)*255/31;}
/* DXT3 stores explicit alpha and a four-colour RGB565 palette per block.
 * Choose the farthest visible pair, then nearest palette colour per texel. */
static void block(uint8_t *out,const uint8_t *px,unsigned stride)
{
    unsigned col[16][3],alpha[16],lo=0,hi=0,dist=0;
    memset(out,0,16);
    for(unsigned i=0;i<16;++i){const uint8_t *p=px+(i/4)*stride+(i%4)*4;alpha[i]=p[3];
        for(int k=0;k<3;++k)col[i][k]=alpha[i]?MIN(255,p[k]*255/alpha[i]):0;
        out[i/2]|=((alpha[i]+8)/17)<<((i%2)*4);
    }
    for(unsigned i=0;i<16;++i)for(unsigned j=0;j<i;++j)if(alpha[i]>16&&alpha[j]>16){
        unsigned d=0;for(int k=0;k<3;++k){int v=(int)col[i][k]-(int)col[j][k];d+=v*v;}
        if(d>dist){dist=d;lo=i;hi=j;}
    }
    if(!dist)for(unsigned i=0;i<16;++i)if(alpha[i]){lo=hi=i;break;}
    unsigned a=rgb565(col[lo]),b=rgb565(col[hi]);if(a<b){unsigned t=a;a=b;b=t;}
    out[8]=a&255;out[9]=a>>8;out[10]=b&255;out[11]=b>>8;
    unsigned palette[4][3];unpack(a,palette[0]);unpack(b,palette[1]);
    for(int k=0;k<3;++k){palette[2][k]=(2*palette[0][k]+palette[1][k])/3;palette[3][k]=(palette[0][k]+2*palette[1][k])/3;}
    uint32_t indices=0;
    for(unsigned i=0;i<16;++i){unsigned best=0,score=~0u;
        for(unsigned p=0;p<4;++p){unsigned d=0;for(int k=0;k<3;++k){int v=(int)col[i][k]-(int)palette[p][k];d+=v*v;}if(d<score){score=d;best=p;}}
        indices|=best<<(i*2);
    }
    put32(out+12,indices);
}
typedef struct { unsigned w,h; NSData *source,*pixels; unsigned expanded; } Atlas;
static Atlas atlases[8];
static pthread_mutex_t atlas_lock=PTHREAD_MUTEX_INITIALIZER;
int black_InputAtlasFind(const void *top,size_t bytes,unsigned w,unsigned h,unsigned fmt,BlackInputAtlas *out)
{
    if(fmt!=14||!top||bytes<(size_t)w*h)return 0;
    int found=0;pthread_mutex_lock(&atlas_lock);
    for(unsigned i=0;i<8;++i)if(atlases[i].source&&atlases[i].w==w&&atlases[i].h==h&&
        !memcmp(top,atlases[i].source.bytes,w*h)){
        *out=(BlackInputAtlas){w,atlases[i].expanded,atlases[i].pixels.bytes};found=1;break;
    }
    pthread_mutex_unlock(&atlas_lock);return found;
}
static int publish(const uint8_t *original,unsigned w,unsigned h,const uint8_t *expanded,unsigned newH)
{
    NSMutableData *pixels=[NSMutableData dataWithLength:w*newH*4];uint8_t *out=pixels.mutableBytes;
    for(unsigned y=0;y<newH;y+=4)for(unsigned x=0;x<w;x+=4){
        const uint8_t *b=expanded+(y/4*(w/4)+x/4)*16;unsigned c[4][3];unpack(u16(b+8),c[0]);unpack(u16(b+10),c[1]);
        for(int k=0;k<3;++k){c[2][k]=(2*c[0][k]+c[1][k])/3;c[3][k]=(c[0][k]+2*c[1][k])/3;}
        uint32_t indices=u32(b+12);
        for(unsigned i=0;i<16;++i){uint8_t *p=out+((y+i/4)*w+x+i%4)*4;unsigned color=(indices>>(i*2))&3;
            for(int k=0;k<3;++k)p[k]=c[color][k];p[3]=((b[i/2]>>((i%2)*4))&15)*17;
        }
    }
    int done=0;pthread_mutex_lock(&atlas_lock);
    for(unsigned i=0;i<8;++i){Atlas *a=atlases+i;
        if(a->source&&a->w==w&&a->h==h&&!memcmp(a->source.bytes,original,w*h)){done=1;break;}
        if(!a->source){*a=(Atlas){w,h,[[NSData alloc] initWithBytes:original length:w*h],[pixels copy],newH};done=1;break;}
    }
    pthread_mutex_unlock(&atlas_lock);return done;
}
NSData *black_InputGlyphFont(NSData *input,NSError **error)
{
    const uint8_t *b=input.bytes;NSUInteger n=input.length;
    if(n<0x220+128||memcmp(b,"4.1v",4))return invalid(error,@"Not a game font.");
    uint32_t tex=u32(b+4),head=u32(b+0x1C);
    if(tex+128ull>n||head<0x220||head+40ull>tex||(head-0x220)%40)return invalid(error,@"Invalid game font records.");
    uint32_t fmt=u32(b+tex+12);unsigned W=1u<<((fmt>>20)&15),H=1u<<((fmt>>24)&15);
    if(((fmt>>8)&255)!=14||W>1024||H>4096||tex+128ull+W*H>n)return invalid(error,@"Unsupported game font texture.");
    unsigned count=0,existing=0;
    for(unsigned code=0xE100;code<=0xE200+BLACK_KEY_LIMIT;++code)if(black_InputGlyphValid(code))++count;
    float cap=0,top=0;
    for(uint32_t at=0x220;at<head;at+=40){unsigned code=u16(b+at+28);
        if(black_InputGlyphValid(code))++existing;
        if(code=='H'){cap=f32(b+at+12)*H;top=f32(b+at+20)*H;}
    }
    if(existing==count)return input;
    if(existing||!(cap>0&&cap<=64))return invalid(error,@"Unsupported or partial input artwork.");
    unsigned height=MAX(16,(unsigned)ceil(cap*1.4)),x=4,y=H+4,row=0;
    NSMutableArray *cells=[NSMutableArray array];
    for(unsigned code=0xE100;code<=0xE200+BLACK_KEY_LIMIT;++code)if(black_InputGlyphValid(code)){
        CGImageRef img=black_InputGlyphCreate(code,height);if(!img)return invalid(error,@"Could not draw input artwork.");
        unsigned w=(unsigned)CGImageGetWidth(img),h=(unsigned)CGImageGetHeight(img);
        CGImageRelease(img);unsigned cw=(w+3)&~3u,ch=(h+3)&~3u;
        if(cw+8>W)return invalid(error,@"Input cell is wider than the atlas.");
        if(x+cw+4>W){x=4;y+=row+4;row=0;}
        [cells addObject:@[@(code),@(x),@(y),@(w),@(h),@(cw),@(ch)]];
        x+=cw+4;row=MAX(row,ch);
    }
    unsigned needed=y+row+4,newH=H;while(newH<needed&&newH<4096)newH*=2;
    if(newH<needed)return invalid(error,@"Input atlas exceeds 4096 pixels.");
    NSMutableData *records=[NSMutableData dataWithBytes:b+0x220 length:head-0x220];
    for(NSArray *cell in cells){unsigned code=[cell[0] unsignedIntValue],cx=[cell[1] unsignedIntValue],cy=[cell[2] unsignedIntValue],w=[cell[3] unsignedIntValue],h=[cell[4] unsignedIntValue];
        uint8_t rec[40]={0};float f[7]={(float)cx/W,(float)cy/H,(float)w/W,(float)h/H,0,(top-(h-cap)/2)/H,(float)(w+2)/W};
        memcpy(rec,f,28);uint16_t ch=code;memcpy(rec+28,&ch,2);[records appendBytes:rec length:40];
    }
    uint8_t *pool=records.mutableBytes;
    for(NSUInteger at=0;at<records.length;at+=40)for(int k=1;k<=5;k+=2){float f=f32(pool+at+k*4)*H/newH;memcpy(pool+at+k*4,&f,4);}
    qsort_b(pool,records.length/40,40,^int(const void *a,const void *b){unsigned x=u16((const uint8_t *)a+28),y=u16((const uint8_t *)b+28);return (int)(((x&127)<<16)|x)-(int)(((y&127)<<16)|y);});
    NSMutableData *out=[NSMutableData dataWithBytes:b length:0x220];[out appendData:records];uint32_t newHead=(uint32_t)out.length;
    [out appendBytes:b+head length:tex-head];
    /* Default glyph follows the same normalized vertical coordinate rules. */
    for(unsigned k=1;k<=5;k+=2){float v=f32(b+head+k*4)*H/newH;memcpy((uint8_t *)out.mutableBytes+newHead+k*4,&v,4);}
    [out increaseLengthBy:(128-out.length%128)%128];uint32_t newTex=(uint32_t)out.length;
    [out appendBytes:b+tex length:128];[out increaseLengthBy:W*newH];
    uint8_t *o=out.mutableBytes;put32(o+4,newTex);put32(o+0x1C,newHead);
    /* Glyph extents double as normalized geometry. The font's vertical
     * geometry scale must grow with the texture, preserving every old letter. */
    float vertical=f32(b+12)*newH/H;memcpy(o+12,&vertical,4);
    unsigned exponent=0;for(unsigned h=newH;h>1;h>>=1)++exponent;
    put32(o+newTex+12,(fmt&~(15u<<24))|(exponent<<24));
    memcpy(o+newTex+128,b+tex+128,W*H); /* original DXT blocks remain byte-identical */
    for(int k=0;k<128;++k)put32(o+0x20+k*4,newHead);
    for(NSUInteger at=records.length;at>0;at-=40){uint32_t offset=0x220+(uint32_t)at-40;put32(o+0x20+(u16(o+offset+28)&127)*4,offset);}
    for(NSArray *cell in cells){unsigned code=[cell[0] unsignedIntValue],cx=[cell[1] unsignedIntValue],cy=[cell[2] unsignedIntValue],cw=[cell[5] unsignedIntValue],ch=[cell[6] unsignedIntValue];
        CGImageRef image=black_InputGlyphCreate(code,height);CGColorSpaceRef cs=CGColorSpaceCreateDeviceRGB();
        CGContextRef c=CGBitmapContextCreate(NULL,cw,ch,8,cw*4,cs,kCGImageAlphaPremultipliedLast|kCGBitmapByteOrder32Big);CGColorSpaceRelease(cs);
        CGContextDrawImage(c,CGRectMake(0,ch-height,CGImageGetWidth(image),height),image);CGImageRelease(image);
        const uint8_t *px=CGBitmapContextGetData(c);
        for(unsigned yy=0;yy<ch;yy+=4)for(unsigned xx=0;xx<cw;xx+=4)
            block(o+newTex+128+((cy+yy)/4*(W/4)+(cx+xx)/4)*16,px+(yy*cw+xx)*4,cw*4);
        CGContextRelease(c);
    }
    if(!publish(b+tex+128,W,H,o+newTex+128,newH))return invalid(error,@"Input atlas registry is full.");
    /* Keep the Xbox texture header and payload at their original dimensions.
     * Only the host renderer sees the expanded atlas. Extra guest font bytes
     * would otherwise exhaust BLACK's startup file heap before GlobData.bin. */
    put32(o+newTex+12,fmt);
    [out setLength:newTex+128+W*H];
    return out;
}
