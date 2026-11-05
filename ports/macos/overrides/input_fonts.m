/* Private per-run font view: never write through a source symlink, never
 * replace an original font, bank or XBE. Button pictures occupy new cells. */
#import <Foundation/Foundation.h>
#import "game_translation.h"
#include "port_menu.h"
#include <stdio.h>
#include <stdlib.h>
static char *active_view;
void black_InputGameViewRemove(const char *path);
static void cleanup_view(void) { if (active_view) black_InputGameViewRemove(active_view); }
char *black_InputGameView(const char *source)
{
    @autoreleasepool {
        NSFileManager *fm = NSFileManager.defaultManager;
        NSURL *game = [NSURL fileURLWithPath:@(source)].URLByResolvingSymlinksInPath;
        const char *run=getenv("BLACK_RUN_DIRECTORY");
        NSString *directory=run?@(run):fm.currentDirectoryPath;
        NSString *cwd=[NSURL fileURLWithPath:directory].URLByResolvingSymlinksInPath.path;
        if ([cwd isEqual:game.path] || [cwd hasPrefix:[game.path stringByAppendingString:@"/"]]) {
            fprintf(stderr,"[PROMPTS] no private run directory; source left unchanged, named buttons\n");
            setenv("BLACK_PROMPT_TEXT_SYMBOLS","1",1); return NULL;
        }
        NSURL *view = [NSURL fileURLWithPath:[directory stringByAppendingPathComponent:
            [@"input-view-" stringByAppendingString:NSUUID.UUID.UUIDString]]];
        NSError *error = nil;
        BOOL ok = [fm createDirectoryAtURL:view withIntermediateDirectories:NO attributes:nil error:&error];
        for (NSString *part in @[@"", @"language", @"language/fonts", @"language/strings"]) {
            if (!ok) break;
            NSURL *from = [game URLByAppendingPathComponent:part].URLByResolvingSymlinksInPath, *to = [view URLByAppendingPathComponent:part];
            if (part.length) ok = [fm createDirectoryAtURL:to withIntermediateDirectories:NO attributes:nil error:&error];
            NSArray *items = ok ? [fm contentsOfDirectoryAtURL:from includingPropertiesForKeys:nil options:0 error:&error] : nil;
            if (!items) { ok = NO; break; }
            for (NSURL *item in items) {
                NSString *name = item.lastPathComponent;
                if ((!part.length && [name.lowercaseString isEqual:@"language"]) ||
                    ([part isEqual:@"language"] && ([name.lowercaseString isEqual:@"fonts"] ||
                                                    [name.lowercaseString isEqual:@"strings"]))) continue;
                NSURL *destination = [to URLByAppendingPathComponent:name];
                if (([part isEqual:@"language/fonts"] &&
                    ([name.lowercaseString isEqual:@"big.bin"] || [name.lowercaseString isEqual:@"small.bin"])) ||
                    ([part isEqual:@"language/strings"] &&
                    ([name.lowercaseString isEqual:@"mainus.bin"] || [name.lowercaseString isEqual:@"mainru.bin"]))) {
                    /* Own every file translation/glyph preparation can write.
                     * Reading bytes also follows a source file symlink safely. */
                    NSData *original = [NSData dataWithContentsOfURL:item options:0 error:&error];
                    ok = original && [original writeToURL:destination options:NSDataWritingAtomic error:&error];
                } else ok = [fm createSymbolicLinkAtURL:destination withDestinationURL:item error:&error];
                if (!ok) break;
            }
        }
        BOOL russian = black_SettingsGameLanguage() == BLACK_LANG_RUSSIAN;
        if (ok && russian) {
            NSURL *strings = black_RussianStrings();
            if (!strings) {
                error = [NSError errorWithDomain:@"BLACK.Translation" code:1
                    userInfo:@{NSLocalizedDescriptionKey:@"Russian selected but translation JSON is unavailable."}];
                ok = NO;
            } else ok = black_PrepareRussian(view, strings, &error);
        }
        if (ok) ok = black_ApplyGameText(view, russian, &error);
        /* Cyrillic must be present before expanding the host atlas for input
         * pictures. Reversing this order leaves mixed-language HUD prompts. */
        for (NSString *name in @[@"Big.bin", @"Small.bin"]) {
            if (!ok) break;
            NSURL *font = [view URLByAppendingPathComponent:[@"language/fonts" stringByAppendingPathComponent:name]];
            NSData *original = [NSData dataWithContentsOfURL:font options:0 error:&error];
            NSData *patched = original ? black_FontWithInputGlyphs(original, &error) : nil;
            ok = patched && [patched writeToURL:font options:NSDataWritingAtomic error:&error];
        }
        if (!ok) {
            [fm removeItemAtURL:view error:nil];
            fprintf(stderr,"[PROMPTS] font view unavailable: %s; named PlayStation buttons\n",error.description.UTF8String);
            setenv("BLACK_PROMPT_TEXT_SYMBOLS","1",1);
            return NULL;
        }
        if (russian) {
            fprintf(stderr, "[LANG] private run view: Russian bank and Cyrillic fonts prepared before input artwork\n");
            NSURL *films = black_RussianSubtitles();
            if (films && !getenv("BLACK_SUBTITLE_DIR")) {
                setenv("BLACK_SUBTITLE_DIR", films.path.fileSystemRepresentation, 1);
                fprintf(stderr, "[LANG] Russian film subtitles on\n");
            }
        }
        unsetenv("BLACK_PROMPT_TEXT_SYMBOLS");
        fprintf(stderr,"[PROMPTS] private font view ready; source fonts preserved\n");
        if (!active_view) {
            active_view=strdup(view.path.fileSystemRepresentation);
            static int registered;
            if (!registered) { atexit(cleanup_view); registered=1; }
        }
        return strdup(view.path.fileSystemRepresentation);
    }
}
void black_InputGameViewRemove(const char *path)
{
    @autoreleasepool {
        if (path) {
            [NSFileManager.defaultManager removeItemAtPath:@(path) error:nil];
            if (active_view && !strcmp(path,active_view)) { free(active_view); active_view=NULL; }
        }
    }
}
