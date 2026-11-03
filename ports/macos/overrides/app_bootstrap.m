/* Finder launch: install owned game data once, then enter the same native host.
 * No Python/Homebrew dependency or development-checkout path at runtime. */
#import <Cocoa/Cocoa.h>
#import "app_install.h"
#import "app_setup.h"
#import "game_translation.h"
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
        NSURL *logs=[[root URLByAppendingPathComponent:@"Logs"] URLByAppendingPathComponent:[@"run-" stringByAppendingString:NSUUID.UUID.UUIDString]];
        if(![fm createDirectoryAtURL:logs withIntermediateDirectories:YES attributes:nil error:&error]){message(@"Cannot create a session log",error.localizedDescription);return NULL;}
        int fd=open([[logs URLByAppendingPathComponent:@"game.log"].path fileSystemRepresentation],O_WRONLY|O_CREAT|O_EXCL,0600);
        if(fd<0){message(@"Cannot create a session log",@"Check available space and access to your Library folder.");return NULL;}dup2(fd,STDOUT_FILENO);dup2(fd,STDERR_FILENO);close(fd);
        if(chdir(logs.path.fileSystemRepresentation)){message(@"Cannot open session folder",@"Check access to your Library folder.");return NULL;}
        /* Before setup: its language and graphics pages save into the same settings. */
        setenv("BLACK_SETTINGS_FILE",[[root URLByAppendingPathComponent:@"settings.ini"].path fileSystemRepresentation],1);
        if(!black_ValidateInstalledGame(game,&error)){
            if([fm fileExistsAtPath:game.path]){message(@"Game files need attention",@"The installed executable is missing or does not match the supported disc revision. Your saves were preserved. See the installation guide before replacing game data.");return NULL;}
            /* First run: the setup window installs the game from the player's own copy. */
            NSURL *patch=[NSBundle.mainBundle URLForResource:@"europe" withExtension:@"patch"];
            /* Outside the app bundle (development checks) the patch is named explicitly. */
            if(!patch&&getenv("BLACK_EUROPE_PATCH"))patch=[NSURL fileURLWithPath:@(getenv("BLACK_EUROPE_PATCH"))];
            if(!black_SetupRun(game,patch))return NULL;
        }
        /* Russian is built into this copy when it is chosen and MainUS.bin set by the
         * chosen language, every start, so changing it in settings needs nothing else.
         * The other languages leave the fonts as installed. */
        NSURL *russian=black_RussianStrings();NSError *issue=nil;
        if(russian)setenv("BLACK_RUSSIAN_STRINGS",russian.path.fileSystemRepresentation,1);
        BOOL wants=black_SettingsGameLanguage()==BLACK_LANG_RUSSIAN&&russian;
        if(wants&&!black_PrepareRussian(game,russian,&issue)){wants=NO;fprintf(stderr,"[LANG] Russian not prepared: %s\n",issue.localizedDescription.UTF8String);}
        if(!black_ApplyGameText(game,wants,&issue))fprintf(stderr,"[LANG] game text not set: %s\n",issue.localizedDescription.UTF8String);
        else fprintf(stderr,"[LANG] MainUS.bin is the %s bank\n",wants?"Russian":"English");
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
