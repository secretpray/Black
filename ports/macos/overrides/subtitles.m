/* Optional locally supplied SRT captions. Game and AppKit use separate threads. */
#import <Cocoa/Cocoa.h>
#include "subtitles.h"
#include <pthread.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static struct { uint32_t film, movie; char name[9]; double start, seen; unsigned generation; int active; } state;
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }
void black_SubtitleFilm(uint32_t film, uint32_t movie, const char *name, int active)
{
    if (!getenv("BLACK_SUBTITLE_DIR")) return;
    pthread_mutex_lock(&lock);
    active = active && name && *name && movie;
    if ((state.active && !active) || (active && (!state.active || state.film != film || state.movie != movie || strncmp(state.name,name,8)))) {
        state.generation++;
        state.start = active ? now() : 0;
        state.film = film; state.movie = movie;
        snprintf(state.name,sizeof state.name,"%.8s",active ? name : "");
    }
    state.active = active;
    state.seen = active ? now() : 0;
    pthread_mutex_unlock(&lock);
}

static double stamp(NSString *s)
{
    int h,m,sec,ms; char extra;
    if (sscanf(s.UTF8String, "%d:%d:%d,%d%c", &h,&m,&sec,&ms,&extra) != 4 ||
        h < 0 || m < 0 || m > 59 || sec < 0 || sec > 59 || ms < 0 || ms > 999) return -1;
    return h*3600.0+m*60.0+sec+ms/1000.0;
}
@interface BlackSubtitleView : NSView { NSArray *cues; unsigned generation; NSString *current; }
@end
@implementation BlackSubtitleView
- (BOOL)isFlipped { return YES; }
- (NSView *)hitTest:(NSPoint)p { (void)p; return nil; }
- (void)tick:(NSTimer *)timer
{
    (void)timer; unsigned gen; int active; double start; char name[9];
    pthread_mutex_lock(&lock); gen=state.generation; active=state.active && now()-state.seen < 1.0; start=state.start; memcpy(name,state.name,9); pthread_mutex_unlock(&lock);
    if (gen != generation) {
        generation=gen; [cues release]; cues=nil;
        if (active) {
            NSString *stem=[NSString stringWithUTF8String:name];
            if ([stem hasPrefix:@"F"]) stem=[stem substringFromIndex:1];
            if (stem.length > 4) stem=[stem substringToIndex:4];
            NSCharacterSet *allowed=[NSCharacterSet characterSetWithCharactersInString:@"0123456789_Nn"];
            if (stem.length == 4 && [stem rangeOfCharacterFromSet:allowed.invertedSet].location == NSNotFound) {
                NSString *dir=[NSString stringWithUTF8String:getenv("BLACK_SUBTITLE_DIR")];
                NSString *path=[dir stringByAppendingPathComponent:[stem.uppercaseString stringByAppendingString:@".srt"]];
                NSString *data=[NSString stringWithContentsOfFile:path encoding:NSUTF8StringEncoding error:nil];
                data=[[data stringByReplacingOccurrencesOfString:@"\r\n" withString:@"\n"] stringByReplacingOccurrencesOfString:@"\r" withString:@"\n"];
                NSMutableArray *found=[NSMutableArray array];
                for (NSString *block in [data componentsSeparatedByString:@"\n\n"]) {
                    NSArray *lines=[block componentsSeparatedByString:@"\n"];
                    if (lines.count<3) continue;
                    NSArray *times=[lines[1] componentsSeparatedByString:@" --> "]; if(times.count!=2)continue;
                    double a=stamp(times[0]),b=stamp(times[1]); if(a<0||b<=a)continue;
                    NSString *text=[[lines subarrayWithRange:NSMakeRange(2,lines.count-2)] componentsJoinedByString:@"\n"];
                    if (text.length) [found addObject:@[@(a),@(b),text]];
                }
                cues=[found copy]; fprintf(stderr,"[SUBTITLE] %s: %lu cues\n",name,(unsigned long)cues.count);
            }
        }
    }
    NSString *next=nil; double elapsed=now()-start;
    if(active) for (NSArray *cue in cues) if(elapsed >= [cue[0] doubleValue] && elapsed < [cue[1] doubleValue]) {next=cue[2];break;}
    if (next != current && ![next isEqualToString:current]) { [current release];current=[next copy];[self setNeedsDisplay:YES]; if(next) fprintf(stderr,"[SUBTITLE] cue selected at %.2f seconds\n",elapsed); }
}
- (void)drawRect:(NSRect)dirty
{
    (void)dirty; if (!current.length) return;
    CGFloat w=self.bounds.size.width,h=self.bounds.size.height, size=MAX(16,MIN(32,h*0.036));
    NSMutableParagraphStyle *p=[[[NSMutableParagraphStyle alloc]init]autorelease];p.alignment=NSTextAlignmentCenter;
    NSShadow *shadow=[[[NSShadow alloc]init]autorelease];shadow.shadowColor=NSColor.blackColor;shadow.shadowBlurRadius=3;shadow.shadowOffset=NSMakeSize(0,1);
    NSDictionary *attr=@{NSFontAttributeName:[NSFont boldSystemFontOfSize:size],NSForegroundColorAttributeName:NSColor.whiteColor,NSParagraphStyleAttributeName:p,NSShadowAttributeName:shadow};
    NSRect bounds=[current boundingRectWithSize:NSMakeSize(w*0.78,CGFLOAT_MAX) options:NSStringDrawingUsesLineFragmentOrigin attributes:attr];
    CGFloat height=ceil(bounds.size.height)+12; NSRect box=NSMakeRect(w*.1,h*.86-height,w*.8,height);
    [[NSColor colorWithWhite:0 alpha:.62]setFill];[[NSBezierPath bezierPathWithRoundedRect:box xRadius:4 yRadius:4]fill];
    [current drawWithRect:NSInsetRect(box,6,4) options:NSStringDrawingUsesLineFragmentOrigin attributes:attr];
}
- (void)dealloc { [cues release];[current release];[super dealloc]; }
@end
void black_SubtitleAttach(void *parent)
{
    if (!getenv("BLACK_SUBTITLE_DIR") || !parent) return;
    NSView *container=(NSView *)parent;
    BlackSubtitleView *view=[[BlackSubtitleView alloc]initWithFrame:container.bounds];
    view.autoresizingMask=NSViewWidthSizable|NSViewHeightSizable;
    [container addSubview:view positioned:NSWindowBelow relativeTo:container.subviews.lastObject];
    [NSTimer scheduledTimerWithTimeInterval:0.05 target:view selector:@selector(tick:) userInfo:nil repeats:YES];
    [view release];
}
