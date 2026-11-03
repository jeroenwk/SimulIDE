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
