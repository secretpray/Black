/* Optional per-run texture dump. Writes original top mip, never game files. */
#include "texture_dump.h"
#include "texture_pack.h"
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>

enum { QUEUE_MAX=64, SEEN_MAX=16384, PALETTE_VARIANTS_MAX=16 };
typedef struct { uint64_t key,pal; unsigned fmt,w,h; size_t size,psize; unsigned char payload[]; } Job;
static struct { pthread_once_t once; pthread_mutex_t mutex; pthread_cond_t wake; Job *queue[QUEUE_MAX]; unsigned head,count,nseen; size_t queued_bytes; int warned; struct {uint64_t key,pal;unsigned fmt,w,h;} seen[SEEN_MAX]; char dir[PATH_MAX]; int enabled; } D={.once=PTHREAD_ONCE_INIT,.mutex=PTHREAD_MUTEX_INITIALIZER,.wake=PTHREAD_COND_INITIALIZER};
static void le32(unsigned char *p,uint32_t n){p[0]=n;p[1]=n>>8;p[2]=n>>16;p[3]=n>>24;}
static int allwrite(int fd,const void *buf,size_t n){const char *p=buf;while(n){ssize_t a=write(fd,p,n);if(a<0&&errno==EINTR)continue;if(a<=0)return 0;p+=a;n-=a;}return 1;}
static int dds(Job *j,int fd){unsigned char h[128]={0};memcpy(h,"DDS ",4);le32(h+4,124);le32(h+8,0x81007);le32(h+12,j->h);le32(h+16,j->w);le32(h+20,(uint32_t)j->size);le32(h+76,32);le32(h+80,4);memcpy(h+84,j->fmt==12?"DXT1":j->fmt==14?"DXT3":"DXT5",4);le32(h+108,0x1000);return allwrite(fd,h,sizeof h)&&allwrite(fd,j->payload,j->size);}
static int png(Job *j,int fd){
    size_t n=(size_t)j->w*j->h; unsigned char *rgba=malloc(n*4);if(!rgba)return 0;
    unsigned mx=0,my=0,bit=1,mask=1;while(bit<j->w||bit<j->h){if(bit<j->w){mx|=mask;mask<<=1;}if(bit<j->h){my|=mask;mask<<=1;}bit<<=1;}
    unsigned oy=0;const unsigned char *pal=j->payload+j->size;
    for(unsigned y=0;y<j->h;y++){unsigned ox=0;for(unsigned x=0;x<j->w;x++){unsigned i=j->payload[oy+ox];if((size_t)i*4+4>j->psize){free(rgba);return 0;}size_t o=((size_t)y*j->w+x)*4;rgba[o]=pal[i*4+2];rgba[o+1]=pal[i*4+1];rgba[o+2]=pal[i*4];rgba[o+3]=pal[i*4+3];ox=(ox-mx)&mx;}oy=(oy-my)&my;}
    CFDataRef bytes=CFDataCreate(NULL,rgba,(CFIndex)(n*4));free(rgba);if(!bytes)return 0;
    CGDataProviderRef prov=CGDataProviderCreateWithCFData(bytes);CGColorSpaceRef cs=CGColorSpaceCreateDeviceRGB();
    CGImageRef image=CGImageCreate(j->w,j->h,8,32,j->w*4,cs,kCGImageAlphaLast|kCGBitmapByteOrderDefault,prov,NULL,false,kCGRenderingIntentDefault);
    CFMutableDataRef result=CFDataCreateMutable(NULL,0);CGImageDestinationRef dest=result?CGImageDestinationCreateWithData(result,CFSTR("public.png"),1,NULL):NULL;
    int ok=0;if(dest&&image){CGImageDestinationAddImage(dest,image,NULL);if(CGImageDestinationFinalize(dest))ok=allwrite(fd,CFDataGetBytePtr(result),(size_t)CFDataGetLength(result));}
    if(dest)CFRelease(dest);if(result)CFRelease(result);if(image)CGImageRelease(image);if(cs)CGColorSpaceRelease(cs);if(prov)CGDataProviderRelease(prov);CFRelease(bytes);return ok;
}
static void *worker(void *unused){(void)unused;for(;;){pthread_mutex_lock(&D.mutex);while(!D.count)pthread_cond_wait(&D.wake,&D.mutex);Job *j=D.queue[D.head];D.head=(D.head+1)%QUEUE_MAX;D.count--;D.queued_bytes-=j->size+j->psize;pthread_mutex_unlock(&D.mutex);
    char path[PATH_MAX];int n=snprintf(path,sizeof path,"%s/%016llx_%ux%u_%02x_%016llx.%s",D.dir,(unsigned long long)j->key,j->w,j->h,j->fmt,(unsigned long long)j->pal,j->fmt==11?"png":"dds");
    if(n>0&&(size_t)n<sizeof path){int fd=open(path,O_WRONLY|O_CREAT|O_EXCL,0600);if(fd>=0){int ok=j->fmt==11?png(j,fd):dds(j,fd);if(close(fd))ok=0;if(!ok){unlink(path);fprintf(stderr,"[TEXDUMP] write failed\n");}}else if(errno!=EEXIST)fprintf(stderr,"[TEXDUMP] open failed: %s\n",strerror(errno));}free(j);
 }return NULL;}
static void start(void){const char *dir=getenv("BLACK_TEXTURE_DUMP_DIR");if(!dir||!*dir||strlen(dir)>=sizeof D.dir)return;strcpy(D.dir,dir);pthread_t thread;if(pthread_create(&thread,NULL,worker,NULL)==0){pthread_detach(thread);D.enabled=1;fprintf(stderr,"[TEXDUMP] original top levels enabled\n");}}
void black_TextureDump(unsigned fmt,unsigned w,unsigned h,const uint8_t *data,size_t bytes,const uint8_t *palette,size_t psize){
    pthread_once(&D.once,start);if(!D.enabled||!data||w>4096||h>4096)return;size_t top=black_TexturePackTopBytes(fmt,w,h);if(!top||top>bytes||(fmt==11&&(!palette||psize<128||psize>1024||psize%8)))return;
    if(fmt==11)for(size_t i=0;i<top;i++)if((size_t)data[i]*4+4>psize)return;
    uint64_t key=black_TexturePackKey(data,top),pal=fmt==11?black_TexturePackKey(palette,psize):0;
    pthread_mutex_lock(&D.mutex);unsigned variants=0;for(unsigned i=0;i<D.nseen;i++)if(D.seen[i].key==key&&D.seen[i].fmt==fmt&&D.seen[i].w==w&&D.seen[i].h==h){if(D.seen[i].pal==pal){pthread_mutex_unlock(&D.mutex);return;}variants++;}
    if(variants>=PALETTE_VARIANTS_MAX){if(!D.warned){fprintf(stderr,"[TEXDUMP] palette variant limit reached; some textures may be missed\n");D.warned=1;}pthread_mutex_unlock(&D.mutex);return;}
    if(D.count==QUEUE_MAX||D.nseen==SEEN_MAX||D.queued_bytes+top+(fmt==11?psize:0)>64u*1024u*1024u){if(!D.warned){fprintf(stderr,"[TEXDUMP] queue or index full; some textures may be missed\n");D.warned=1;}pthread_mutex_unlock(&D.mutex);return;}
    Job *j=malloc(sizeof *j+top+(fmt==11?psize:0));if(!j){pthread_mutex_unlock(&D.mutex);return;}
    *j=(Job){.key=key,.pal=pal,.fmt=fmt,.w=w,.h=h,.size=top,.psize=fmt==11?psize:0};memcpy(j->payload,data,top);if(j->psize)memcpy(j->payload+top,palette,psize);
    D.seen[D.nseen].key=key;D.seen[D.nseen].pal=pal;D.seen[D.nseen].fmt=fmt;D.seen[D.nseen].w=w;D.seen[D.nseen].h=h;D.nseen++;
    D.queue[(D.head+D.count)%QUEUE_MAX]=j;D.count++;D.queued_bytes+=j->size+j->psize;pthread_cond_signal(&D.wake);pthread_mutex_unlock(&D.mutex);
}
