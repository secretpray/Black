/* XDVDFS semantics follow pinned runtime tools/xiso/xdvdfs.py; Europe patch
 * follows TGP src/common/game_files.cpp. No game data is embedded here. */
#import "app_install.h"
#import <CommonCrypto/CommonDigest.h>
static NSString *const usa=@"df2739c372d254a90aeeccf5971d12097f22d89fbb90db021fda96dd4deb68f6";
static NSString *const europe=@"796ad8369354e48aa15644d4707dd70bd3bc3b4c";
static NSString *const patchHash=@"3cb0f57bb9a8ee6e01e4654bafb331efa82946c5665b87428464fb1c1c19c390";
static BOOL fail(NSError **e,NSString *s)
{if(e)*e=[NSError errorWithDomain:@"BLACK.Install" code:1 userInfo:@{NSLocalizedDescriptionKey:s}];return NO;}
static NSString *hash(NSData *data,BOOL sha1)
{
    unsigned char bytes[CC_SHA256_DIGEST_LENGTH];
    if(sha1)CC_SHA1(data.bytes,(CC_LONG)data.length,bytes);else CC_SHA256(data.bytes,(CC_LONG)data.length,bytes);
    NSMutableString *s=[NSMutableString string];for(int i=0;i<(sha1?20:32);++i)[s appendFormat:@"%02x",bytes[i]];return s;
}
static uint32_t le32(const unsigned char *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint32_t le16(const unsigned char *p){return p[0]|p[1]<<8;}
static NSData *readAt(NSFileHandle *f,uint64_t at,NSUInteger n,NSError **e)
{
    if(![f seekToOffset:at error:e])return nil;
    NSData *d=[f readDataUpToLength:n error:e];
    if(d&&d.length!=n){fail(e,@"The disc image is truncated.");return nil;}return d;
}
static BOOL safeName(NSString *s)
{for(NSUInteger i=0;i<s.length;++i)if(![s characterAtIndex:i])return NO;return s.length&&![s isEqual:@"."]&&![s isEqual:@".."]&&[s rangeOfCharacterFromSet:[NSCharacterSet characterSetWithCharactersInString:@"/\\:\0"]].location==NSNotFound;}
NSArray<NSDictionary *> *black_DiscFiles(NSFileHandle *file,uint64_t *base,NSError **e)
{
    uint64_t size=[file seekToEndOfFile];BOOL found=NO;
    for(NSNumber *candidate in @[@0,@0xfd90,@0x30600,@0xfd90000,@0x18300000]){
        uint64_t at=candidate.unsignedLongLongValue+32*2048;
        if(at+2048>size)continue;NSData *d=readAt(file,at,2048,e);if(!d)return nil;
        if(!memcmp(d.bytes,"MICROSOFT*XBOX*MEDIA",20)){*base=candidate.unsignedLongLongValue;found=YES;break;}
    }
    if(!found){fail(e,@"This is not a supported Original Xbox ISO/XISO image.");return nil;}
    NSData *descriptor=readAt(file,*base+32*2048,2048,e);if(!descriptor)return nil;
    const unsigned char *p=descriptor.bytes;
    NSMutableArray *todo=[NSMutableArray arrayWithObject:@{@"path":@"",@"sector":@(le32(p+20)),@"size":@(le32(p+24)),@"depth":@0}];
    NSMutableArray *files=[NSMutableArray array];NSMutableSet *seenDirs=[NSMutableSet set],*paths=[NSMutableSet set];
    for(NSUInteger i=0;i<todo.count;++i){
        NSDictionary *dir=todo[i];uint64_t sector=[dir[@"sector"] unsignedLongLongValue];NSUInteger n=[dir[@"size"] unsignedIntegerValue];
        NSString *identity=[NSString stringWithFormat:@"%llu:%lu",sector,(unsigned long)n];
        if([seenDirs containsObject:identity]||[dir[@"depth"] unsignedIntegerValue]>64||n>16*1024*1024){fail(e,@"Invalid or cyclic disc directory.");return nil;}
        [seenDirs addObject:identity];if(!n)continue;
        if(*base+sector*2048+n>size){fail(e,@"Directory lies outside the disc image.");return nil;}
        NSData *data=readAt(file,*base+sector*2048,n,e);if(!data)return nil;const unsigned char *b=data.bytes;
        NSMutableArray *nodes=[NSMutableArray arrayWithObject:@0];NSMutableSet *seen=[NSMutableSet set];
        for(NSUInteger j=0;j<nodes.count;++j){
            NSNumber *off=nodes[j];NSUInteger at=off.unsignedIntegerValue;
            if([seen containsObject:off]||at+14>n){fail(e,@"Invalid disc directory tree.");return nil;}[seen addObject:off];
            if(b[at]==255&&b[at+1]==255)continue;
            NSUInteger length=b[at+13];if(at+14+length>n){fail(e,@"Invalid disc filename length.");return nil;}
            NSString *name=[[[NSString alloc]initWithBytes:b+at+14 length:length encoding:NSISOLatin1StringEncoding]autorelease];
            if(!safeName(name)){fail(e,@"Unsafe filename in disc image.");return nil;}
            NSString *path=[dir[@"path"] stringByAppendingPathComponent:name],*key=path.lowercaseString;
            if([paths containsObject:key]||paths.count>=100000){fail(e,@"Duplicate filename or excessive disc entries.");return nil;}[paths addObject:key];
            uint32_t s=le32(b+at+4),sz=le32(b+at+8);
            if(*base+(uint64_t)s*2048+sz>size){fail(e,@"File lies outside the disc image.");return nil;}
            NSDictionary *entry=@{@"path":path,@"sector":@(s),@"size":@(sz),@"depth":@([dir[@"depth"] unsignedIntegerValue]+1)};
            if(b[at+12]&16)[todo addObject:entry];else[files addObject:entry];
            for(int k=0;k<2;++k){uint32_t child=le16(b+at+k*2);if(child)[nodes addObject:@(child*4)];}
        }
    }
    return files;
}
NSData *black_EuropePatch(NSData *original,NSData *data,NSError **e)
{
    const unsigned char *p=data.bytes;__block NSUInteger at=0;__block BOOL bad=NO;
    uint64_t (^number)(void)=^{uint64_t v=0;for(int s=0;s<64;s+=7){if(at>=data.length){bad=YES;return (uint64_t)0;}unsigned b=p[at++];if(s==63&&(b&126)){bad=YES;return (uint64_t)0;}v|=(uint64_t)(b&127)<<s;if(!(b&128))return v;}bad=YES;return (uint64_t)0;};
    NSMutableData *out=[NSMutableData data];int64_t pos=0;
    while(at<data.length&&!bad){
        uint64_t count=number(),changes=number();
        if(pos<0||(uint64_t)pos>original.length||count>original.length-pos||count>16*1024*1024-out.length||changes>data.length){bad=YES;break;}
        NSUInteger start=out.length;[out appendBytes:(const char*)original.bytes+pos length:(NSUInteger)count];uint64_t delta=0;
        for(uint64_t i=0;i<changes&&!bad;++i){uint64_t step=number();if(step>count||delta>=count-step||at>=data.length){bad=YES;break;}delta+=step;((unsigned char*)out.mutableBytes)[start+delta]+=p[at++];++delta;}
        uint64_t extra=number();if(extra>data.length-at||extra>16*1024*1024-out.length){bad=YES;break;}[out appendBytes:p+at length:(NSUInteger)extra];at+=(NSUInteger)extra;
        uint64_t move=number();if((move>>1)>16*1024*1024){bad=YES;break;}pos+=(int64_t)count+(move&1?-(int64_t)(move>>1)-1:(int64_t)(move>>1));
    }
    if(bad){fail(e,@"The European revision patch is invalid.");return nil;}return out;
}
static NSURL *xbeAt(NSURL *root)
{for(NSURL *p in [NSFileManager.defaultManager contentsOfDirectoryAtURL:root includingPropertiesForKeys:nil options:0 error:nil])if([p.lastPathComponent.lowercaseString isEqual:@"default.xbe"])return p;return [root URLByAppendingPathComponent:@"default.xbe"];}
static BOOL supported(NSData *xbe)
{return xbe&&xbe.length<=16*1024*1024&&([hash(xbe,NO)isEqual:usa]||[hash(xbe,YES)isEqual:europe]);}
BOOL black_ValidateInstalledGame(NSURL *root,NSError **e)
{NSData *xbe=[NSData dataWithContentsOfURL:xbeAt(root) options:0 error:e];if(!xbe)return NO;if(![hash(xbe,NO)isEqual:usa])return fail(e,@"BLACK requires the supported USA or European Original Xbox disc revision.");return YES;}
BOOL black_InstallGame(NSURL *source,NSURL *destination,NSURL *patch,NSError **e)
{
    NSFileManager *fm=NSFileManager.defaultManager;BOOL directory=NO;
    if(![fm fileExistsAtPath:source.path isDirectory:&directory])return fail(e,@"The selected game source is unavailable.");
    if(directory&&[[destination.URLByStandardizingPath.path stringByAppendingString:@"/"] hasPrefix:[source.URLByStandardizingPath.path stringByAppendingString:@"/"]])return fail(e,@"The installation folder must be outside the source game folder.");
    if([fm fileExistsAtPath:destination.path])return fail(e,@"A game is already installed here. Existing data was preserved.");
    NSURL *stage=[destination.URLByDeletingLastPathComponent URLByAppendingPathComponent:[@".install-" stringByAppendingString:NSUUID.UUID.UUIDString]];
    NSFileHandle *file=nil;NSArray *entries=nil;uint64_t base=0;NSData *original=nil;BOOL ok=NO;
    if(directory)original=[NSData dataWithContentsOfURL:xbeAt(source) options:0 error:e];
    else{
        file=[NSFileHandle fileHandleForReadingFromURL:source error:e];if(!file)return NO;
        entries=black_DiscFiles(file,&base,e);if(!entries)goto done;
        for(NSDictionary *entry in entries)if([entry[@"path"] caseInsensitiveCompare:@"default.xbe"]==NSOrderedSame){
            NSUInteger n=[entry[@"size"] unsignedIntegerValue];if(n<=16*1024*1024)original=readAt(file,base+[entry[@"sector"] unsignedLongLongValue]*2048,n,e);break;}
    }
    if(!supported(original)){fail(e,@"Unsupported game revision. Select your own USA or supported European Original Xbox BLACK disc.");goto done;}
    if(![fm createDirectoryAtURL:stage.URLByDeletingLastPathComponent withIntermediateDirectories:YES attributes:nil error:e])goto done;
    if(directory){
        /* Copy into private app data; normalization never writes to the source.
         * Reject symlinks so a copied install never points back into the source. */
        NSDirectoryEnumerator *walk=[fm enumeratorAtURL:source includingPropertiesForKeys:@[NSURLIsSymbolicLinkKey] options:0 errorHandler:nil];
        for(NSURL *p in walk){NSNumber *link=nil;if(![p getResourceValue:&link forKey:NSURLIsSymbolicLinkKey error:e])goto done;if(link.boolValue){fail(e,@"Select an extracted disc folder containing regular files, without symbolic links.");goto done;}}
        if(![fm copyItemAtURL:source toURL:stage error:e])goto done;
    }else{
        if(![fm createDirectoryAtURL:stage withIntermediateDirectories:NO attributes:nil error:e])goto done;
        for(NSDictionary *entry in entries){
            NSURL *target=[stage URLByAppendingPathComponent:entry[@"path"]];
            if(![fm createDirectoryAtURL:target.URLByDeletingLastPathComponent withIntermediateDirectories:YES attributes:nil error:e])goto done;
            if(![fm createFileAtPath:target.path contents:nil attributes:nil]){fail(e,@"Cannot create an installed game file.");goto done;}
            NSFileHandle *out=[NSFileHandle fileHandleForWritingToURL:target error:e];if(!out)goto done;
            uint64_t at=base+[entry[@"sector"] unsignedLongLongValue]*2048,left=[entry[@"size"] unsignedLongLongValue];
            while(left){NSUInteger n=(NSUInteger)MIN(left,1024*1024);NSData *chunk=readAt(file,at,n,e);if(!chunk||![out writeData:chunk error:e]){[out closeFile];goto done;}at+=n;left-=n;}[out closeFile];
        }
    }
    if([hash(original,YES)isEqual:europe]){
        NSData *patchData=[NSData dataWithContentsOfURL:patch options:0 error:e];
        if(!patchData||![hash(patchData,NO)isEqual:patchHash]){fail(e,@"The packaged Europe patch failed its integrity check.");goto done;}
        NSData *normalized=black_EuropePatch(original,patchData,e);if(!normalized)goto done;
        if(![hash(normalized,NO)isEqual:usa]){fail(e,@"The normalized executable failed its integrity check.");goto done;}
        if(![normalized writeToURL:xbeAt(stage) options:NSDataWritingAtomic error:e])goto done;
        NSURL *strings=[stage URLByAppendingPathComponent:@"language/strings"],*alias=[strings URLByAppendingPathComponent:@"MainUS.bin"];
        if(![fm fileExistsAtPath:alias.path]&&![fm copyItemAtURL:[strings URLByAppendingPathComponent:@"MainUK.bin"] toURL:alias error:e])goto done;
    }
    if(!black_ValidateInstalledGame(stage,e))goto done;
    ok=[fm moveItemAtURL:stage toURL:destination error:e];
done:
    [file closeFile];if(!ok)[fm removeItemAtURL:stage error:nil];return ok;
}
