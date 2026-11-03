/* First-run setup in the look of BLACK's own menu pages: the player's disc
 * image or folder, its check, the copy with progress, stop and resume. */
#pragma once
#import <Cocoa/Cocoa.h>
#import "app_install.h"

/* Shows the setup until the game is installed into `game` and the player
 * chooses to play (YES), or quits (NO). Main thread, before the game window. */
BOOL black_SetupRun(NSURL *game, NSURL *patch);

/* Previews and tests: the view alone, and a page shown with given state. */
typedef enum { BLACK_SETUP_DISC, BLACK_SETUP_CHECKING, BLACK_SETUP_REVIEW, BLACK_SETUP_COPYING,
               BLACK_SETUP_STOPPED, BLACK_SETUP_FAILED, BLACK_SETUP_LANGUAGE, BLACK_SETUP_GRAPHICS,
               BLACK_SETUP_DONE } BlackSetupPage;
NSView *black_SetupMakeView(NSURL *game, NSURL *patch);
void black_SetupShow(NSView *view, BlackSetupPage page, BlackDiscInfo *info, BlackInstallProgress *progress,
    NSString *message, int selected, double age);
/* Inspects and installs `source` as the window would, then continues past
 * language and graphics as shown. */
BlackSetupPage black_SetupDrive(NSView *view, NSURL *source, double timeout);
