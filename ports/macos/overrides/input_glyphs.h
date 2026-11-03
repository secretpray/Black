/* Original vector button artwork, shared by the guest atlas and Cocoa hints. */
#pragma once
#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>
int black_InputGlyphValid(unsigned code);
CGImageRef black_InputGlyphCreate(unsigned code, unsigned height); /* caller releases */
NSData *black_InputGlyphFont(NSData *input, NSError **error);
