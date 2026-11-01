/* Finder launch: install owned game data once, then enter the same native host.
 * No Python/Homebrew dependency or development-checkout path at runtime. */
#import <Cocoa/Cocoa.h>
#import "app_install.h"
#include "port_menu.h"
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
static int guard=-1;
static void message(NSString *title,NSString *detail)
{NSAlert *a=[[[NSAlert alloc]init]autorelease];a.messageText=title;a.informativeText=detail;[a runModal];}
static void value(const char *key,int n)
{char text[32];snprintf(text,sizeof text,"%d",n);setenv(key,text,1);}
char *black_AppPrepareGame(void)
{
    @autoreleasepool{
        [NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        const char *override=getenv("BLACK_APP_DATA_ROOT");
        NSURL *root=override?[NSURL fileURLWithPath:[NSString stringWithUTF8String:override]]:
            [[NSFileManager.defaultManager URLsForDirectory:NSApplicationSupportDirectory inDomains:NSUserDomainMask].firstObject URLByAppendingPathComponent:@"BLACK"];
        NSFileManager *fm=NSFileManager.defaultManager;NSError *error=nil;
        if(![fm createDirectoryAtURL:root withIntermediateDirectories:YES attributes:nil error:&error]){message(@"Cannot open BLACK data",error.localizedDescription);return NULL;}
        guard=open([[root URLByAppendingPathComponent:@".run.lock"].path fileSystemRepresentation],O_CREAT|O_RDWR,0600);
        if(guard<0||flock(guard,LOCK_EX|LOCK_NB)){message(@"BLACK is already running",@"Close the other BLACK window before starting another session.");return NULL;}
        NSURL *game=[root URLByAppendingPathComponent:@"Game"];
        if(!black_ValidateInstalledGame(game,&error)){
            if([fm fileExistsAtPath:game.path]){message(@"Game files need attention",@"The installed executable is missing or does not match the supported disc revision. Your saves were preserved. See the installation guide before replacing game data.");return NULL;}
            [NSApp activateIgnoringOtherApps:YES];
            NSOpenPanel *panel=NSOpenPanel.openPanel;panel.title=@"Install BLACK";panel.prompt=@"Install Game";
            panel.message=@"Choose your own Original Xbox BLACK ISO/XISO image or extracted disc folder (USA or Europe). Game files are copied into your Library; your source stays unchanged.";
            panel.canChooseFiles=YES;panel.canChooseDirectories=YES;panel.allowsMultipleSelection=NO;
            if([panel runModal]!=NSModalResponseOK)return NULL;
            NSURL *source=panel.URL,*patch=[NSBundle.mainBundle URLForResource:@"europe" withExtension:@"patch"];
            NSWindow *progress=[[[NSWindow alloc]initWithContentRect:NSMakeRect(0,0,480,150) styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO]autorelease];
            progress.title=@"Installing BLACK";[progress center];
            NSTextField *label=[NSTextField labelWithString:@"Copying your game files…\nThis can take a few minutes."];
            label.frame=NSMakeRect(28,70,424,50);[progress.contentView addSubview:label];
            NSProgressIndicator *bar=[[[NSProgressIndicator alloc]initWithFrame:NSMakeRect(28,35,424,18)]autorelease];
            bar.indeterminate=YES;bar.style=NSProgressIndicatorStyleBar;[bar startAnimation:nil];[progress.contentView addSubview:bar];[progress makeKeyAndOrderFront:nil];
            __block BOOL done=NO,ok=NO;__block NSError *problem=nil;
            dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{@autoreleasepool{
                NSError *issue=nil;BOOL success=black_InstallGame(source,game,patch,&issue);
                dispatch_async(dispatch_get_main_queue(),^{ok=success;problem=[issue retain];done=YES;});
            }});
            while(!done)[NSRunLoop.currentRunLoop runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:.05]];
            [progress orderOut:nil];if(!ok){message(@"BLACK could not be installed",problem.localizedDescription?:@"The selected source could not be read.");[problem release];return NULL;}[problem release];
        }
        NSURL *logs=[[root URLByAppendingPathComponent:@"Logs"] URLByAppendingPathComponent:[@"run-" stringByAppendingString:NSUUID.UUID.UUIDString]];
        if(![fm createDirectoryAtURL:logs withIntermediateDirectories:YES attributes:nil error:&error]){message(@"Cannot create a session log",error.localizedDescription);return NULL;}
        int fd=open([[logs URLByAppendingPathComponent:@"game.log"].path fileSystemRepresentation],O_WRONLY|O_CREAT|O_EXCL,0600);
        if(fd<0){message(@"Cannot create a session log",@"Check available space and access to your Library folder.");return NULL;}dup2(fd,STDOUT_FILENO);dup2(fd,STDERR_FILENO);close(fd);
        if(chdir(logs.path.fileSystemRepresentation)){message(@"Cannot open session folder",@"Check access to your Library folder.");return NULL;}
        setenv("BLACK_SETTINGS_FILE",[[root URLByAppendingPathComponent:@"settings.ini"].path fileSystemRepresentation],1);
        setenv("XDG_DATA_HOME",[[root URLByAppendingPathComponent:@"Data"].path fileSystemRepresentation],1);
        setenv("BLACK_GAME_DIR",game.path.fileSystemRepresentation,1);
        const char *ones[]={"SDL_NO_SIGNAL_HANDLERS","RECOMP_XEMU_GPU","RECOMP_VBLANK","RECOMP_AC97_READY","RECOMP_PB_EXEC","RECOMP_FB_WINDOW","RECOMP_FULLSCREEN","RECOMP_REAL_DSP","BLACK_CONSOLE_GAME_CLOCK","BLACK_STABLE_FRAMES"};
        for(unsigned i=0;i<sizeof ones/sizeof ones[0];++i)setenv(ones[i],"1",1);
        setenv("RECOMP_KEYBOARD","0",1);setenv("RECOMP_ASPECT","16:9",1);setenv("BLACK_INPUT_DEVICE","auto",1);setenv("BLACK_DSP_HEADROOM_DB","6",1);
        int scale=black_SettingsRenderScale();value("RECOMP_SCALE",scale);value("RECOMP_FOV",black_SettingsFov());value("RECOMP_AF",black_SettingsAniso());
        setenv("RECOMP_AA",(const char*[]){"0","1","smaa"}[black_SettingsAntiAliasing()],1);
        setenv("RECOMP_UPSCALE",black_SettingsUpscaler()?"metalfx":"off",1);if(scale>1)setenv("RECOMP_NO_FLIP_DOWNLOAD","1",1);else unsetenv("RECOMP_NO_FLIP_DOWNLOAD");
        fprintf(stderr,"[APP] version=%s private data and unique session log ready\n",[NSBundle.mainBundle.infoDictionary[@"CFBundleShortVersionString"] UTF8String]);
        return strdup(game.path.fileSystemRepresentation);
    }
}
