/***************************************************************************
 *   macOS appearance (light / dark) for UiTheme                          *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#import <AppKit/AppKit.h>
#include <functional>

bool uiMacSystemDark()
{
    if( @available( macOS 10.14, * ) )
    {
        if( NSApp && NSApp.appearance == nil )      // Following the system: the app's effective appearance is the system's
        {
            NSAppearanceName n = [NSApp.effectiveAppearance bestMatchFromAppearancesWithNames:
                                  @[NSAppearanceNameAqua, NSAppearanceNameDarkAqua]];
            return [n isEqualToString:NSAppearanceNameDarkAqua];
        }
    }
    NSString* style = [[NSUserDefaults standardUserDefaults] stringForKey:@"AppleInterfaceStyle"];
    return style && [style caseInsensitiveCompare:@"Dark"] == NSOrderedSame;
}

void uiMacSetAppearance( int mode )   // 0 system, 1 light, 2 dark
{
    if( @available( macOS 10.14, * ) )
    {
        if( !NSApp ) return;
        if     ( mode == 1 ) NSApp.appearance = [NSAppearance appearanceNamed:NSAppearanceNameAqua];
        else if( mode == 2 ) NSApp.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
        else                 NSApp.appearance = nil;
    }
}

void uiMacObserve( std::function<void()> onChange ) // System light/dark switch
{
    std::function<void()> cb = onChange;
    [[NSDistributedNotificationCenter defaultCenter]
        addObserverForName:@"AppleInterfaceThemeChangedNotification" object:nil
        queue:[NSOperationQueue mainQueue] usingBlock:^( NSNotification* ){ cb(); }];
}

// A small grey text at the right end of the title bar (the build time)
void uiMacTitleNote( void* nsView, const char* text )
{
    NSView* view = (__bridge NSView*)nsView;
    NSWindow* win = view.window;
    if( !win ) return;
    for( NSTitlebarAccessoryViewController* a in win.titlebarAccessoryViewControllers )
        if( [a.identifier isEqualToString:@"buildNote"] ){ [(NSTextField*)a.view.subviews.firstObject setStringValue:@(text)]; return; }

    NSTextField* label = [NSTextField labelWithString:@(text)];
    label.font = [NSFont systemFontOfSize:[NSFont smallSystemFontSize]];
    label.textColor = NSColor.secondaryLabelColor;
    [label sizeToFit];
    CGFloat h = 32;   // The title bar's height: the text centred on the title
    NSView* box = [[NSView alloc] initWithFrame:NSMakeRect( 0, 0, label.frame.size.width+12, h )];
    label.frame = NSMakeRect( 0, ( h-label.frame.size.height )/2, label.frame.size.width, label.frame.size.height );
    [box addSubview:label];
    NSTitlebarAccessoryViewController* acc = [[NSTitlebarAccessoryViewController alloc] init];
    acc.identifier = @"buildNote";
    acc.view = box;
    acc.layoutAttribute = NSLayoutAttributeRight;
    [win addTitlebarAccessoryViewController:acc];
}
