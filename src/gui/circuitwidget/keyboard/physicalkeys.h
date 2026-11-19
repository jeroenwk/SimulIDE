/***************************************************************************
 *   Keyboard input: physical keys, whatever the layout                    *
 *   (specs/keyboard-input.md D10, D21)                                    *
 *                                                                         *
 *   Commands match the character a key types, so they follow the user's   *
 *   layout. Only the number row is also read by position: on AZERTY its   *
 *   unshifted characters are & é " ' ( - è _ ç à, not digits.             *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef PHYSICALKEYS_H
#define PHYSICALKEYS_H

class QKeyEvent;

namespace PhysicalKeys
{
    // The digit of the number-row key pressed (0-9), by its position: macOS virtual
    // key codes, Windows set-1 scan codes, X11/evdev key codes. -1: another key
    int numberRowDigit( QKeyEvent* event );
}

#endif
