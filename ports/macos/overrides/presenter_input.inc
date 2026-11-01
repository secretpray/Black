/* Native Cocoa events -> rebindable physical-key state. The checked presenter
 * overlay substitutes only XBFrameKeyView's input methods, outside submodules. */
- (void)flagsChanged:(NSEvent *)event
{
    if (getenv("RECOMP_BACKGROUND")) return;
    int code = black_KeyCanonical(event.keyCode);
    NSUInteger mask = code == 56 ? NSEventModifierFlagShift : code == 58 ? NSEventModifierFlagOption :
        code == 59 ? NSEventModifierFlagControl : 0;
    if (mask) black_KeyEvent(code, !!(event.modifierFlags & mask));
    if (event.modifierFlags & NSEventModifierFlagCommand) black_KeyReleaseAll();
}
- (void)keyDown:(NSEvent *)event { [self xek_handleKey:event down:YES]; }
- (void)keyUp:(NSEvent *)event { [self xek_handleKey:event down:NO]; }
- (void)xek_handleKey:(NSEvent *)event down:(BOOL)down
{
    if (getenv("RECOMP_BACKGROUND")) return;
    unsigned short code = event.keyCode;
    if (event.modifierFlags & NSEventModifierFlagCommand) {
        black_KeyReleaseAll();
        if (down && (((code == 36 || code == 76)) ||
            (code == 3 && (event.modifierFlags & NSEventModifierFlagControl)))) [s_window toggleFullScreen:nil];
        return;
    }
    if(black_GameTextActive() && code != 53){
        if(down){NSString *text=event.characters;
            for(NSUInteger i=0;i<text.length;++i){unsigned c=[text characterAtIndex:i];
                if(c==127)c='\b';if(c=='\n')c='\r';black_GameTextCharacter(c);
            }
        }
        if(code>=123&&code<=126)black_KeyEvent(code,down);
        return;
    }
    if (code == 53) { /* Release and pause in play; Back in a game menu. */
        if(black_GameMenuActive()){if(down)xek_capture_mouse(NO);black_KeyEvent(53,down);return;}
        if (down) { xek_capture_mouse(NO); xbox_KeyboardPressSlot(K_START); }
        else xbox_KeyboardReleaseSlot(K_START);
        return;
    }
    black_KeyEvent(code, down);
}
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)becomeFirstResponder { return YES; }
- (BOOL)resignFirstResponder
{
    xbox_KeyboardReleaseAll(); xbox_MouseReleaseAll(); xek_capture_mouse(NO);
    return YES;
}
- (void)viewDidMoveToWindow
{
    NSNotificationCenter *nc = NSNotificationCenter.defaultCenter; [nc removeObserver:self];
    if (!self.window) return;
    [nc addObserver:self selector:@selector(xek_focusLost:) name:NSWindowDidResignKeyNotification object:self.window];
    [nc addObserver:self selector:@selector(xek_focusLost:) name:NSApplicationDidResignActiveNotification object:nil];
}
- (void)xek_focusLost:(NSNotification *)note
{
    (void)note; xbox_KeyboardReleaseAll(); xbox_MouseReleaseAll(); xek_capture_mouse(NO);
}
- (void)xek_menuPointer:(NSEvent *)event
{
    NSPoint p=[self convertPoint:event.locationInWindow fromView:nil];float x=0,y=0;
    int inside=black_FramePointer(p.x,p.y,self.bounds.size.width,self.bounds.size.height,(float)display_aspect(),&x,&y);
    black_GamePointerPosition(x,y,inside&&self.window.keyWindow&&NSApp.active);
}
- (void)mouseMoved:(NSEvent *)event
{
    if(!getenv("RECOMP_BACKGROUND")&&black_GameMenuActive()){[self xek_menuPointer:event];return;}
    if (!getenv("RECOMP_BACKGROUND") && s_mouse_captured) black_MouseMotion(event.deltaX, event.deltaY);
}
- (void)mouseDragged:(NSEvent *)event { [self mouseMoved:event]; }
/* The pinned main-thread pump forwards queued motion to this selector. */
- (void)xek_motion:(NSEvent *)event { [self mouseMoved:event]; }
- (void)rightMouseDragged:(NSEvent *)event { [self mouseMoved:event]; }
- (void)otherMouseDragged:(NSEvent *)event { [self mouseMoved:event]; }
- (void)mouseDown:(NSEvent *)event
{
    if (getenv("RECOMP_BACKGROUND")) return;
    if(black_GameMenuActive()){[self xek_menuPointer:event];black_GamePointerButton(1);return;}
    if (!s_mouse_captured) { xek_capture_mouse(YES); return; } /* First click never fires. */
    black_KeyEvent(BLACK_MOUSE_LEFT, 1);
}
- (void)mouseUp:(NSEvent *)event { (void)event;black_GamePointerButton(0);black_KeyEvent(BLACK_MOUSE_LEFT, 0); }
- (void)rightMouseDown:(NSEvent *)event
{ (void)event; if (!getenv("RECOMP_BACKGROUND") && (s_mouse_captured||black_GameMenuActive())) black_KeyEvent(BLACK_MOUSE_RIGHT, 1); }
- (void)rightMouseUp:(NSEvent *)event { (void)event; black_KeyEvent(BLACK_MOUSE_RIGHT, 0); }
- (void)otherMouseDown:(NSEvent *)event
{
    if (!getenv("RECOMP_BACKGROUND") && s_mouse_captured && event.buttonNumber >= 2 && event.buttonNumber <= 4)
        black_KeyEvent(BLACK_MOUSE_MIDDLE + (int)event.buttonNumber - 2, 1);
}
- (void)otherMouseUp:(NSEvent *)event
{
    if (event.buttonNumber >= 2 && event.buttonNumber <= 4)
        black_KeyEvent(BLACK_MOUSE_MIDDLE + (int)event.buttonNumber - 2, 0);
}
- (void)scrollWheel:(NSEvent *)event
{
    if(!getenv("RECOMP_BACKGROUND")&&black_GameMenuActive()&&event.scrollingDeltaY!=0){black_GamePointerWheel(event.scrollingDeltaY>0);return;}
    if (!getenv("RECOMP_BACKGROUND") && event.scrollingDeltaY != 0)
        black_MouseWheel(event.scrollingDeltaY > 0, black_InputNowMs());
}
