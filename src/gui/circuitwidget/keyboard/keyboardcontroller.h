/***************************************************************************
 *   Keyboard input on the canvas (specs/keyboard-input.md)                *
 *                                                                         *
 *   Owns the keyboard state of the circuit editor: the keyboard cursor    *
 *   (a cross on the grid), the mouse pointer hidden while typing, and     *
 *   (in later features) modes, value editing and pin roles.               *
 *   Circuit::keyPressEvent asks it first; CircuitView reports focus and   *
 *   mouse moves; Circuit::drawForeground lets it paint.                   *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef KEYBOARDCONTROLLER_H
#define KEYBOARDCONTROLLER_H

#include <QPointF>
#include <QPoint>
#include <QRectF>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QPointer>

#include "valueeditor.h"

class QKeyEvent;
class QGraphicsItem;
class QDialog;
class Component;
class Pin;
class QPainter;

class KeyboardController
{
    public:
 static KeyboardController* self();

 static constexpr int smallStep = 8;    // px, Shift+Arrows
 static constexpr int bigStep   = 64;   // px, Arrows, Enter (later features)

        // Nearest point of the drawn grid (lines at 4 + 8k)
 static QPointF snap( QPointF p );

        void reset();                       // New or opened circuit

        // A key pressed on the canvas (text entry not active). true: handled, stop there
        bool keyPress( QKeyEvent* event );

        // The mouse moved over the canvas (viewport position): shows the pointer again
        void mouseMoved( QPoint viewPos );
        void mousePressed();                     // Any button: ends pan mode
        void clickedEmpty( QPointF scenePos );   // Left click (no drag) on empty canvas: the cross goes there

        void setFocused( bool focused );    // Canvas got / lost keyboard focus
        bool focused() { return m_focused; }

        QPointF cursorPos();
        bool cursorVisible();                    // Canvas focused and no part selected
        bool panMode() { return m_panMode; }
        bool pointerHidden() { return m_pointerHidden; }

        void moveCursorTo( QPointF scenePos );   // Snapped; scrolls the view to keep it visible
        void selectionMoved( QPointF delta );    // Circuit::moveSelection: the hidden cross follows

        void paint( QPainter* painter );         // From Circuit::drawForeground

        // Pins for wiring (K3, K4, K8), kept by id: they survive undo and vanish with their part
        Pin* pinFrom();
        Pin* pinTo();
        void setPinFrom( Pin* pin );
        void setPinTo( Pin* pin );

        // How Pin::paint draws a pin
        enum pinLook_t { lookNormal=0, lookAccent, lookHighlight };
        pinLook_t pinLook( Pin* pin, bool* ring );

        Component* singlePart();                 // The one part selected (not a junction), or nullptr

        // Selection change rule (K4, spec §4.3): when the selection settles on one part B,
        // pinFrom = the pin of the last part A, pinTo = B's pin (auto pins: nearest), lastPart = B.
        // Evaluated once the selection has settled (a click passes through partial states)
        void selectionChanged();
        void settleSelection();
        void refreshAutoPins();                  // After A or B moved: auto pins nearest again
        Component* lastPart();                   // nullptr: none, or deleted

        QJsonObject state();                     // CLI: kbd state

    private:
        KeyboardController();

        bool arrowKey( QKeyEvent* event, Qt::KeyboardModifiers mods );
        bool pinKey( int dir, bool from );
        bool tabKey( int dir );
        bool rotateKey( bool ccw );                // Cmd+R: rotate, then aligned to pinFrom (D19)
        bool partKey( QString letter );
        bool spaceKey();
        bool wireKey();                             // w: wire pinFrom to pinTo (K8)
        bool escKey();
        bool enterKey( bool opposite = false );     // Shift+Enter: leave by the opposite pin
        void leavePart( Component* comp, bool opposite );   // Unselect, cross beyond its pin (D16)

        void selectPart( Component* comp );
        Component* compByUid( QString uid );
        QList<Component*> readingOrder();
        void pairPins( Component* a, Component* b );

        void updatePin( QString id );

        void hidePointer();
        void showPointer();
        void applyPointer();                     // Override cursor: pan symbol, blank or none
        void setPanMode( bool on );
        bool zoomKey( QKeyEvent* event, Qt::KeyboardModifiers mods );
        void zoomAround( double factor );        // Keeps the cross (or the view centre) in place
        bool anyPartSelected();
        void updateCross( QPointF p );
        void parkCursor( QPointF scenePos, bool snapIt = true );   // The hidden cross: no scrolling
        QRectF crossRect( QPointF p );

        QPointF m_cursor;
        bool    m_cursorSet;
        bool    m_focused;
        bool    m_pointerHidden;
        bool    m_panMode;                      // h: arrows pan the canvas until another key
        int     m_override;                     // Qt::CursorShape set as override, -1: none
        QPoint  m_lastMousePos;
        bool    m_lastMouseSet;

        // Arrow presses on the same selection are one undo step, until anything else happens
        bool    m_arrowBurst;
        int     m_burstEdit;                    // Circuit::editCount() after the burst's last move
        QList<QGraphicsItem*> m_burstSelection;

        QString m_pinFrom;                      // Pin ids
        QString m_pinTo;

        QString m_lastPart;                     // Uid of the last single-selected part (B)
        QString m_prevPart;                     // Uid of the part before it (A)
        bool    m_selPending;                   // A settleSelection() is queued
        void*   m_circuit;                      // Circuit whose selectionChanged is connected

        ValueEditor m_value;                    // Typing the selected part's value (K7)

        QString m_leavePin;                     // The pin Enter left from (D17), and the
        QPointF m_leavePos;                     // cross spot beyond it: placement continues there

        QPointer<QDialog> m_focusDialog;        // Properties dialog that returns focus to the canvas
};

#endif
