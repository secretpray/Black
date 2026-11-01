#import <Foundation/Foundation.h>
/* Read-only disc import. Destination is created atomically and never replaced. */
BOOL black_InstallGame(NSURL *source, NSURL *destination, NSURL *patch, NSError **error);
BOOL black_ValidateInstalledGame(NSURL *directory, NSError **error);
/* Shared production parser/patcher, also exercised with synthetic fixtures. */
NSArray<NSDictionary *> *black_DiscFiles(NSFileHandle *file, uint64_t *base, NSError **error);
NSData *black_EuropePatch(NSData *original, NSData *patch, NSError **error);
