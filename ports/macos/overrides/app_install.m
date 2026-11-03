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
@implementation BlackInstallProgress
@synthesize bytesDone,bytesTotal,filesDone,filesTotal,currentFile,cancelled;
- (void)dealloc{[currentFile release];[super dealloc];}
@end
@implementation BlackDiscInfo
@synthesize source,directory,base,files,totalBytes,region,languages,bigFont,smallFont,original;
- (void)dealloc{[source release];[files release];[region release];[languages release];[bigFont release];[smallFont release];[original release];[super dealloc];}
@end
static NSDictionary *entryNamed(NSArray *files,NSString *path)
{for(NSDictionary *f in files)if([f[@"path"] caseInsensitiveCompare:path]==NSOrderedSame)return f;return nil;}
/* One file's bytes from the image or the folder, for files the setup reads before copying. */
static NSData *readEntry(BlackDiscInfo *info,NSFileHandle *image,NSDictionary *entry,NSUInteger limit,NSError **e)
{
    if(!entry||[entry[@"size"] unsignedLongLongValue]>limit)return nil;
    if(info.directory)return [NSData dataWithContentsOfURL:[info.source URLByAppendingPathComponent:entry[@"path"]] options:0 error:e];
    return readAt(image,info.base+[entry[@"sector"] unsignedLongLongValue]*2048,[entry[@"size"] unsignedIntegerValue],e);
}
BlackDiscInfo *black_InspectDisc(NSURL *source,NSError **e)
{
    NSFileManager *fm=NSFileManager.defaultManager;BOOL directory=NO;
    if(![fm fileExistsAtPath:source.path isDirectory:&directory]){fail(e,@"The selected game source is unavailable.");return nil;}
    BlackDiscInfo *info=[[[BlackDiscInfo alloc]init]autorelease];info.source=source.URLByStandardizingPath;info.directory=directory;
    NSFileHandle *image=nil;NSMutableArray *files=[NSMutableArray array];
    if(directory){
        /* Reject symlinks so a copied install never points back into the source. */
        NSString *root=[info.source.path stringByAppendingString:@"/"];
        NSDirectoryEnumerator *walk=[fm enumeratorAtURL:info.source includingPropertiesForKeys:@[NSURLIsSymbolicLinkKey,NSURLIsRegularFileKey,NSURLFileSizeKey] options:0 errorHandler:nil];
        for(NSURL *p in walk){
            NSNumber *link=nil,*regular=nil,*size=nil;
            if(![p getResourceValue:&link forKey:NSURLIsSymbolicLinkKey error:e]||![p getResourceValue:&regular forKey:NSURLIsRegularFileKey error:e])return nil;
            if(link.boolValue){fail(e,@"Select an extracted disc folder containing regular files, without symbolic links.");return nil;}
            if(!regular.boolValue)continue;
            NSString *path=p.URLByStandardizingPath.path;
            if(![path hasPrefix:root]||files.count>=100000||![p getResourceValue:&size forKey:NSURLFileSizeKey error:e]){fail(e,@"The game folder could not be listed.");return nil;}
            [files addObject:@{@"path":[path substringFromIndex:root.length],@"size":size}];
        }
    }else{
        image=[NSFileHandle fileHandleForReadingFromURL:info.source error:e];if(!image)return nil;
        uint64_t base=0;NSArray *entries=black_DiscFiles(image,&base,e);if(!entries){[image closeFile];return nil;}
        info.base=base;[files addObjectsFromArray:entries];
    }
    info.files=files;
    uint64_t total=0;NSMutableArray *languages=[NSMutableArray array];
    for(NSDictionary *f in files){
        total+=[f[@"size"] unsignedLongLongValue];
        NSString *path=f[@"path"],*name=path.lastPathComponent;
        if([path.stringByDeletingLastPathComponent caseInsensitiveCompare:@"language/strings"]==NSOrderedSame&&name.length==10&&
           [name.lowercaseString hasPrefix:@"main"]&&[name.lowercaseString hasSuffix:@".bin"])[languages addObject:[name substringWithRange:NSMakeRange(4,2)]];
    }
    info.totalBytes=total;info.languages=[languages sortedArrayUsingSelector:@selector(caseInsensitiveCompare:)];
    info.original=readEntry(info,image,entryNamed(files,@"default.xbe"),16*1024*1024,e);
    if(!supported(info.original)){[image closeFile];fail(e,@"Unsupported game revision. Select your own USA or supported European Original Xbox BLACK disc.");return nil;}
    info.region=[hash(info.original,NO)isEqual:usa]?@"USA":@"Europe";
    info.bigFont=readEntry(info,image,entryNamed(files,@"language/fonts/Big.bin"),4*1024*1024,nil);
    info.smallFont=readEntry(info,image,entryNamed(files,@"language/fonts/Small.bin"),4*1024*1024,nil);
    [image closeFile];return info;
}
uint64_t black_FreeSpace(NSURL *folder)
{
    for(NSURL *at=folder;at.path.length>1;at=at.URLByDeletingLastPathComponent){
        NSNumber *n=nil;if([at getResourceValue:&n forKey:NSURLVolumeAvailableCapacityForImportantUsageKey error:nil]&&n)return n.unsignedLongLongValue;
    }
    return 0;
}
/* Copies one file to `target` through a .part file, so a file with the final
 * name is always complete; stops between 1 MiB chunks when cancelled. */
static BOOL copyEntry(BlackDiscInfo *info,NSFileHandle *image,NSDictionary *entry,NSURL *target,BlackInstallProgress *progress,NSError **e)
{
    NSFileManager *fm=NSFileManager.defaultManager;NSURL *part=[target URLByAppendingPathExtension:@"part"];
    [fm removeItemAtURL:part error:nil];
    if(![fm createDirectoryAtURL:target.URLByDeletingLastPathComponent withIntermediateDirectories:YES attributes:nil error:e])return NO;
    if(![fm createFileAtPath:part.path contents:nil attributes:nil])return fail(e,@"Cannot create an installed game file.");
    NSFileHandle *out=[NSFileHandle fileHandleForWritingToURL:part error:e],*in=image;if(!out)return NO;
    uint64_t at=info.base+[entry[@"sector"] unsignedLongLongValue]*2048,left=[entry[@"size"] unsignedLongLongValue];BOOL ok=YES;
    if(info.directory){in=[NSFileHandle fileHandleForReadingFromURL:[info.source URLByAppendingPathComponent:entry[@"path"]] error:e];at=0;if(!in){[out closeFile];return NO;}}
    while(left&&ok){
        if(progress.cancelled){ok=NO;if(e)*e=[NSError errorWithDomain:@"BLACK.Install" code:BLACK_INSTALL_CANCELLED userInfo:@{NSLocalizedDescriptionKey:@"Copying stopped. What was copied is kept."}];break;}
        NSUInteger n=(NSUInteger)MIN(left,1024*1024);NSData *chunk=readAt(in,at,n,e);
        if(!chunk||![out writeData:chunk error:e]){ok=NO;break;}
        at+=n;left-=n;progress.bytesDone+=n;
    }
    [out closeFile];if(in!=image)[in closeFile];
    if(!ok)return NO;
    [fm removeItemAtURL:target error:nil];return [fm moveItemAtURL:part toURL:target error:e];
}
static NSString *const stageMarker=@".black-install.json";
static NSDictionary *stageIdentity(BlackDiscInfo *info)
{return @{@"source":info.source.path,@"xbe_sha1":hash(info.original,YES),@"files":@(info.files.count),@"bytes":@(info.totalBytes)};}
uint64_t black_PartialBytes(BlackDiscInfo *info,NSURL *destination)
{
    NSURL *stage=[destination.URLByDeletingLastPathComponent URLByAppendingPathComponent:@".install-partial"];
    NSData *old=[NSData dataWithContentsOfURL:[stage URLByAppendingPathComponent:stageMarker]];
    if(!old||![[NSJSONSerialization JSONObjectWithData:old options:0 error:nil] isEqual:stageIdentity(info)])return 0;
    uint64_t done=0;
    for(NSDictionary *entry in info.files){NSNumber *size=nil;
        if([[stage URLByAppendingPathComponent:entry[@"path"]] getResourceValue:&size forKey:NSURLFileSizeKey error:nil]&&[size isEqual:entry[@"size"]])done+=size.unsignedLongLongValue;}
    return done;
}
BOOL black_InstallDisc(BlackDiscInfo *info,NSURL *destination,NSURL *patch,BlackInstallProgress *progress,NSError **e)
{
    NSFileManager *fm=NSFileManager.defaultManager;
    if(info.directory&&[[destination.URLByStandardizingPath.path stringByAppendingString:@"/"] hasPrefix:[info.source.path stringByAppendingString:@"/"]])return fail(e,@"The installation folder must be outside the source game folder.");
    if([fm fileExistsAtPath:destination.path])return fail(e,@"A game is already installed here. Existing data was preserved.");
    /* One partial copy beside the destination, reused only for the same source and revision. */
    NSURL *stage=[destination.URLByDeletingLastPathComponent URLByAppendingPathComponent:@".install-partial"],*marker=[stage URLByAppendingPathComponent:stageMarker];
    NSDictionary *identity=stageIdentity(info);
    NSData *old=[NSData dataWithContentsOfURL:marker];NSDictionary *seen=old?[NSJSONSerialization JSONObjectWithData:old options:0 error:nil]:nil;
    if([fm fileExistsAtPath:stage.path]&&![seen isEqual:identity]&&![fm removeItemAtURL:stage error:e])return NO;
    if(![fm createDirectoryAtURL:stage withIntermediateDirectories:YES attributes:nil error:e])return NO;
    if(![[NSJSONSerialization dataWithJSONObject:identity options:0 error:e] writeToURL:marker options:NSDataWritingAtomic error:e])return NO;
    progress.bytesTotal=info.totalBytes;progress.filesTotal=info.files.count;progress.bytesDone=0;progress.filesDone=0;
    NSFileHandle *image=info.directory?nil:[NSFileHandle fileHandleForReadingFromURL:info.source error:e];
    BOOL ok=NO,keep=NO;if(!info.directory&&!image)goto done;
    for(NSDictionary *entry in info.files){
        NSURL *target=[stage URLByAppendingPathComponent:entry[@"path"]];NSNumber *size=nil;
        progress.currentFile=entry[@"path"];
        if([target getResourceValue:&size forKey:NSURLFileSizeKey error:nil]&&[size isEqual:entry[@"size"]])progress.bytesDone+=size.unsignedLongLongValue;
        else if(!copyEntry(info,image,entry,target,progress,e)){keep=progress.cancelled;goto done;}
        progress.filesDone+=1;
    }
    if([hash(info.original,YES)isEqual:europe]){
        NSData *patchData=[NSData dataWithContentsOfURL:patch options:0 error:e];
        if(!patchData||![hash(patchData,NO)isEqual:patchHash]){fail(e,@"The packaged Europe patch failed its integrity check.");goto done;}
        NSData *normalized=black_EuropePatch(info.original,patchData,e);if(!normalized)goto done;
        if(![hash(normalized,NO)isEqual:usa]){fail(e,@"The normalized executable failed its integrity check.");goto done;}
        if(![normalized writeToURL:xbeAt(stage) options:NSDataWritingAtomic error:e])goto done;
        NSURL *strings=[stage URLByAppendingPathComponent:@"language/strings"],*alias=[strings URLByAppendingPathComponent:@"MainUS.bin"];
        if(![fm fileExistsAtPath:alias.path]&&![fm copyItemAtURL:[strings URLByAppendingPathComponent:@"MainUK.bin"] toURL:alias error:e])goto done;
    }
    if(!black_ValidateInstalledGame(stage,e)||![fm removeItemAtURL:marker error:e])goto done;
    ok=[fm moveItemAtURL:stage toURL:destination error:e];
done:
    [image closeFile];if(!ok&&!keep)[fm removeItemAtURL:stage error:nil];return ok;
}
BOOL black_InstallGame(NSURL *source,NSURL *destination,NSURL *patch,NSError **e)
{
    BlackDiscInfo *info=black_InspectDisc(source,e);if(!info)return NO;
    BlackInstallProgress *progress=[[[BlackInstallProgress alloc]init]autorelease];
    return black_InstallDisc(info,destination,patch,progress,e);
}
