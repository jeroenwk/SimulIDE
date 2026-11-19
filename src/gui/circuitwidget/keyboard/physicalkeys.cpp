/***************************************************************************
 *   Keyboard input: physical keys, whatever the layout                    *
 ***( see copyright.txt file at root folder )*******************************/

#include <QKeyEvent>

#include "physicalkeys.h"

int PhysicalKeys::numberRowDigit( QKeyEvent* event )
{
#if defined( Q_OS_MACOS )
    switch( event->nativeVirtualKey() )                 // kVK_ANSI_1 … kVK_ANSI_0
    {
        case 0x12: return 1;  case 0x13: return 2;  case 0x14: return 3;
        case 0x15: return 4;  case 0x17: return 5;  case 0x16: return 6;
        case 0x1A: return 7;  case 0x1C: return 8;  case 0x19: return 9;
        case 0x1D: return 0;
    }
    return -1;
#elif defined( Q_OS_WIN )
    quint32 sc = event->nativeScanCode();               // Set 1: 0x02 (1) … 0x0B (0)
    if( sc >= 0x02 && sc <= 0x0B ) return ( sc == 0x0B ) ? 0 : int( sc-1 );
    return -1;
#else
    quint32 kc = event->nativeScanCode();               // X11 / evdev+8: 10 (1) … 19 (0)
    if( kc >= 10 && kc <= 19 ) return ( kc == 19 ) ? 0 : int( kc-9 );
    return -1;
#endif
}
