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

class QKeyEvent;
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

        QJsonObject state();                     // CLI: kbd state

    private:
        KeyboardController();

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
};

#endif
