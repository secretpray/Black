#import <Foundation/Foundation.h>
/* Read-only disc import. Destination is created atomically and never replaced. */
BOOL black_InstallGame(NSURL *source, NSURL *destination, NSURL *patch, NSError **error);
BOOL black_ValidateInstalledGame(NSURL *directory, NSError **error);
/* Shared production parser/patcher, also exercised with synthetic fixtures. */
NSArray<NSDictionary *> *black_DiscFiles(NSFileHandle *file, uint64_t *base, NSError **error);
NSData *black_EuropePatch(NSData *original, NSData *patch, NSError **error);

/* What the setup shows before copying: the checked revision, size, languages
 * and the game's menu fonts, read from the player's image or folder. */
@interface BlackDiscInfo : NSObject
@property (retain) NSURL *source;
@property BOOL directory;
@property uint64_t base;                       /* image offset of the XDVDFS volume */
@property (retain) NSArray<NSDictionary *> *files;  /* path, size (and sector in an image) */
@property uint64_t totalBytes;
@property (retain) NSString *region;           /* "USA" or "Europe" */
@property (retain) NSArray<NSString *> *languages;  /* Main??.bin suffixes on the disc */
@property (retain) NSData *bigFont, *smallFont;
@property (retain) NSData *original;           /* default.xbe as on the disc */
@end
BlackDiscInfo *black_InspectDisc(NSURL *source, NSError **error);

/* Written by the copy, read by the window; `cancelled` stops between chunks. */
@interface BlackInstallProgress : NSObject
@property (atomic) uint64_t bytesDone, bytesTotal;
@property (atomic) NSUInteger filesDone, filesTotal;
@property (atomic, retain) NSString *currentFile;
@property (atomic) BOOL cancelled;
@end
enum { BLACK_INSTALL_CANCELLED = 2 };
/* Copies into a partial folder beside `destination`, kept when cancelled and
 * resumed by the next call for the same source; complete files are skipped. */
BOOL black_InstallDisc(BlackDiscInfo *info, NSURL *destination, NSURL *patch, BlackInstallProgress *progress, NSError **error);
uint64_t black_FreeSpace(NSURL *folder);
/* Bytes of complete files a stopped copy of this source left beside `destination`. */
uint64_t black_PartialBytes(BlackDiscInfo *info, NSURL *destination);
