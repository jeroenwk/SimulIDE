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

class QKeyEvent;
class QGraphicsItem;
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

        // The mouse moved over the canvas (viewport position, scene position)
        void mouseMoved( QPoint viewPos, QPointF scenePos );

        void setFocused( bool focused );    // Canvas got / lost keyboard focus
        bool focused() { return m_focused; }

        QPointF cursorPos();
        bool cursorVisible() { return m_focused; }
        bool pointerHidden() { return m_pointerHidden; }

        void moveCursorTo( QPointF scenePos );   // Snapped; scrolls the view to keep it visible

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
        bool partKey( QString letter );
        bool escKey();
        bool enterKey();
        void selectPart( Component* comp );
        Component* compByUid( QString uid );
        QList<Component*> readingOrder();
        void pairPins( Component* a, Component* b );

        void updatePin( QString id );

        void hidePointer();
        void showPointer();
        void updateCross( QPointF p );
        QRectF crossRect( QPointF p );

        QPointF m_cursor;
        bool    m_cursorSet;
        bool    m_focused;
        bool    m_pointerHidden;
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
};

#endif
