#import "../../ports/macos/overrides/app_install.h"
#include <stdio.h>
static void w(NSMutableData *d,NSUInteger at,uint32_t v)
{unsigned char *p=(unsigned char*)d.mutableBytes+at;for(int i=0;i<4;++i)p[i]=(v>>(8*i))&255;}
int main(int argc,char **argv)
{
    @autoreleasepool{
        NSError *e=nil;
        if(argc==4){BOOL ok=black_InstallGame([NSURL fileURLWithPath:@(argv[1])],[NSURL fileURLWithPath:@(argv[2])],[NSURL fileURLWithPath:@(argv[3])],&e);printf("Install: %s\n",ok?"PASS":e.localizedDescription.UTF8String);return ok?0:1;}
        NSURL *root=[NSURL fileURLWithPath:[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString]];
        NSFileManager *fm=NSFileManager.defaultManager;[fm createDirectoryAtURL:root withIntermediateDirectories:YES attributes:nil error:nil];
        NSURL *path=[root URLByAppendingPathComponent:@"synthetic.iso"];
        NSMutableData *data=[NSMutableData dataWithLength:40*2048];unsigned char *p=data.mutableBytes;
        memcpy(p+32*2048,"MICROSOFT*XBOX*MEDIA",20);w(data,32*2048+20,34);w(data,32*2048+24,2048);
        NSUInteger entry=34*2048;w(data,entry+4,36);w(data,entry+8,4);p[entry+13]=11;memcpy(p+entry+14,"default.xbe",11);memcpy(p+36*2048,"XBEH",4);
        [data writeToURL:path atomically:YES];NSFileHandle *f=[NSFileHandle fileHandleForReadingFromURL:path error:nil];uint64_t base=0;
        NSArray *files=black_DiscFiles(f,&base,&e);[f closeFile];
        if(files.count!=1||base||![files[0][@"path"]isEqual:@"default.xbe"])return 1;
        p[entry+14]='/';[data writeToURL:path atomically:YES];f=[NSFileHandle fileHandleForReadingFromURL:path error:nil];if(black_DiscFiles(f,&base,&e))return 2;[f closeFile];
        p[entry+14]='d';p[entry]=1;[data writeToURL:path atomically:YES];f=[NSFileHandle fileHandleForReadingFromURL:path error:nil];if(black_DiscFiles(f,&base,&e))return 3;[f closeFile];
        unsigned char delta[]={3,1,1,1,0,0};NSData *result=black_EuropePatch([@"abc" dataUsingEncoding:NSUTF8StringEncoding],[NSData dataWithBytes:delta length:sizeof delta],&e);
        if(![result isEqual:[@"acc" dataUsingEncoding:NSUTF8StringEncoding]])return 4;
        if(black_EuropePatch(NSData.data,[NSData dataWithBytes:delta length:1],&e))return 5;
        NSURL *dest=[root URLByAppendingPathComponent:@"Game"];
        if(black_InstallGame(path,dest,path,&e)||[fm fileExistsAtPath:dest.path])return 6;
        [fm createDirectoryAtURL:dest withIntermediateDirectories:NO attributes:nil error:nil];
        NSURL *sentinel=[dest URLByAppendingPathComponent:@"preserve"];[@"keep" writeToURL:sentinel atomically:YES encoding:NSUTF8StringEncoding error:nil];
        if(black_InstallGame(path,dest,path,&e)||![[NSString stringWithContentsOfURL:sentinel encoding:NSUTF8StringEncoding error:nil]isEqual:@"keep"])return 7;
        [fm removeItemAtURL:root error:nil];
        puts("Disc import: XDVDFS tree/bounds/unsafe names, malformed patch, rejected revision, existing install preserved: PASS");return 0;
    }
}
