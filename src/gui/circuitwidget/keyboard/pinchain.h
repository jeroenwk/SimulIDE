/***************************************************************************
 *   Keyboard input: pin geometry for building chains of parts             *
 *   (specs/keyboard-input.md D16-D19)                                     *
 *                                                                         *
 *   Where a pin points, which pin is nearest or most opposite, how a new  *
 *   part is turned to face a pin, where the cross goes when a part is     *
 *   left. Pure geometry on scene positions: no keyboard state.            *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef PINCHAIN_H
#define PINCHAIN_H

#include <QPointF>
#include <QString>

class Component;
class Pin;

namespace PinChain
{
    // A side's clockwise quarter: up 0, right 1, down 2, left 3
    int cwQuarter( QString side );

    // The outward direction a pin faces (unit vector, scene y grows down)
    QPointF dir( Pin* pin );

    // The part's pin nearest to a scene point (first clockwise on ties), or nullptr
    Pin* nearest( Component* comp, QPointF to );

    // The pin pointing most opposite to sel (D16): the most opposite direction; ties →
    // pointing more downwards → nearest to sel → first clockwise. nullptr: no other pin
    Pin* opposite( Component* comp, Pin* sel );

    // Turns a part (not yet placed) so its first pin faces back along -outDir, towards
    // the pin it continues from (D17). An upside-down result (180/270°) is made as a
    // flip plus a 0/90° rotation instead, so label text stays upright
    void faceBack( Component* comp, QPointF outDir );

    // Where the cross goes when a part is left (D16): step beyond its selected pin in
    // the pin's direction (opposite: beyond its most opposite pin). A part without pins:
    // step right of its position. *used: the pin left from (nullptr without pins)
    QPointF leavePos( Component* comp, bool opposite, double step, Pin** used = nullptr );
}

#endif
