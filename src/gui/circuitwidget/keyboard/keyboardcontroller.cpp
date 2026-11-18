/***************************************************************************
 *   Keyboard input on the canvas (specs/keyboard-input.md)                *
 ***( see copyright.txt file at root folder )*******************************/

#include <QKeyEvent>
#include <QPainter>
#include <QGuiApplication>
#include <QJsonArray>
#include <QtMath>
#include <algorithm>
#include <QDebug>
#include <QGraphicsItem>

#include "keyboardcontroller.h"
#include "circuitview.h"
#include "circuit.h"
#include "component.h"
#include "theme.h"
#include "pin.h"
#include "node.h"

KeyboardController* KeyboardController::self()
{
    static KeyboardController instance;
    return &instance;
}

KeyboardController::KeyboardController()
{
    m_cursorSet     = false;
    m_focused       = false;
    m_pointerHidden = false;
    m_lastMouseSet  = false;
    m_arrowBurst    = false;
    m_burstEdit     = -1;
}

QPointF KeyboardController::snap( QPointF p )
{
    return QPointF( 8*qRound( ( p.x()-4 )/8.0 )+4, 8*qRound( ( p.y()-4 )/8.0 )+4 );
}

void KeyboardController::reset()
{
    showPointer();
    m_cursorSet  = false;
    m_arrowBurst = false;
    m_pinFrom.clear();
    m_pinTo.clear();
}

static bool isModifierKey( int key )
{
    return key == Qt::Key_Shift || key == Qt::Key_Control || key == Qt::Key_Meta
        || key == Qt::Key_Alt   || key == Qt::Key_AltGr   || key == Qt::Key_CapsLock;
}

bool KeyboardController::keyPress( QKeyEvent* event )
{
    int key = event->key();
    if( isModifierKey( key ) ) return false;

    hidePointer();              // Typing on the canvas: the cross stays, the pointer goes

    // macOS sends arrow keys with the keypad modifier
    Qt::KeyboardModifiers mods = event->modifiers() & ~Qt::KeypadModifier;

    bool arrow = key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Up || key == Qt::Key_Down;
    if( arrow && ( mods == Qt::NoModifier || mods == Qt::ShiftModifier ) ) return arrowKey( event, mods );

    m_arrowBurst = false;       // Any other key ends a burst of arrow moves

    // < and > : next / previous pin of the selected part (Cmd: also the "from" pin).
    // The character decides (AZERTY: < unshifted, > with Shift); with Cmd macOS gives no text
    QString text = event->text();
    bool greater = text == ">" || ( text.isEmpty() && key == Qt::Key_Greater );
    bool less    = text == "<" || ( text.isEmpty() && key == Qt::Key_Less );
    if( ( greater || less ) && !( mods & ( Qt::AltModifier | Qt::MetaModifier ) ) )
    {
        qDebug() << "[kbd] pin key" << Qt::hex << key << Qt::dec << "text" << text << "mods" << (int)mods;
        return pinKey( greater ? 1 : -1, mods & Qt::ControlModifier );
    }
    return false;
}

Component* KeyboardController::singlePart()
{
    QList<QGraphicsItem*> sel = Circuit::self()->selectedItems();
    if( sel.size() != 1 || sel.first()->type() != QGraphicsItem::UserType+1 ) return nullptr;
    Component* comp = qgraphicsitem_cast<Component*>( sel.first() );
    if( !comp || comp->itemType() == "Node" ) return nullptr;
    return comp;
}

bool KeyboardController::pinKey( int dir, bool from )
{
    Component* comp = singlePart();
    if( !comp || !comp->selectedPin() ) return false;   // Not for us (old behaviour)
    Pin* pin = comp->stepSelectedPin( dir );
    if( from ) setPinFrom( pin );
    else       setPinTo( pin );
    return true;
}

Pin* KeyboardController::pinFrom() { return m_pinFrom.isEmpty() ? nullptr : Circuit::self()->pinById( m_pinFrom ); }
Pin* KeyboardController::pinTo()   { return m_pinTo.isEmpty()   ? nullptr : Circuit::self()->pinById( m_pinTo ); }

void KeyboardController::updatePin( QString id )
{
    if( id.isEmpty() ) return;
    if( Pin* p = Circuit::self()->pinById( id ) ) p->update();
}

void KeyboardController::setPinFrom( Pin* pin )
{
    QString old = m_pinFrom;
    m_pinFrom = pin ? pin->pinId() : QString();
    updatePin( old );
    updatePin( m_pinFrom );
}

void KeyboardController::setPinTo( Pin* pin )
{
    QString old = m_pinTo;
    m_pinTo = pin ? pin->pinId() : QString();
    updatePin( old );
    updatePin( m_pinTo );
}

KeyboardController::pinLook_t KeyboardController::pinLook( Pin* pin, bool* ring )
{
    *ring = false;
    Component* comp = pin->component();
    if( !comp || comp->itemType() == "Node" ) return lookNormal;

    QString id = pin->pinId();
    if( !m_pinFrom.isEmpty() && id == m_pinFrom ){ *ring = true; return lookHighlight; }
    if( !m_pinTo.isEmpty()   && id == m_pinTo ) return lookHighlight;

    if( comp->parentItem() ) return lookNormal;          // Parts inside a board
    if( comp->selectedPin() != pin ) return lookNormal;
    return comp->isSelected() ? lookHighlight : lookAccent;
}

// Arrows: nothing selected: pan (left to the view), Shift: move the cursor.
// A selection: move it 64 px, Shift: 8 px, with the wires attached, as a mouse drag does
bool KeyboardController::arrowKey( QKeyEvent* event, Qt::KeyboardModifiers mods )
{
    int key = event->key();
    QPointF dir;
    if     ( key == Qt::Key_Left  ) dir = QPointF(-1, 0 );
    else if( key == Qt::Key_Right ) dir = QPointF( 1, 0 );
    else if( key == Qt::Key_Up    ) dir = QPointF( 0,-1 );
    else                            dir = QPointF( 0, 1 );

    Circuit* circ = Circuit::self();
    QList<QGraphicsItem*> sel = circ->selectedItems();
    if( sel.isEmpty() )
    {
        m_arrowBurst = false;
        if( mods != Qt::ShiftModifier ) return false;      // Pan
        moveCursorTo( cursorPos()+dir*smallStep );
        return true;
    }
    QPointF d = dir*( mods == Qt::ShiftModifier ? smallStep : bigStep );

    std::sort( sel.begin(), sel.end() );
    bool newStep = !m_arrowBurst || m_burstEdit != circ->editCount() || sel != m_burstSelection;
    circ->moveSelection( d, newStep );
    m_arrowBurst     = true;
    m_burstEdit      = circ->editCount();
    m_burstSelection = sel;

    moveCursorTo( cursorPos()+d );                          // The cursor follows the selection
    if( CircuitView* v = CircuitView::self() ) v->ensureVisible( v->selectedRect(), 32, 32 );
    return true;
}

void KeyboardController::mouseMoved( QPoint viewPos, QPointF scenePos )
{
    // Ignore "moves" to the same place (sent when the pointer is hidden or shown)
    if( m_lastMouseSet && viewPos == m_lastMousePos ) return;
    m_lastMousePos = viewPos;
    m_lastMouseSet = true;

    showPointer();
    QPointF p = snap( scenePos );
    if( m_cursorSet && p == m_cursor ) return;
    QPointF old = m_cursor;
    bool wasSet = m_cursorSet;
    m_cursor    = p;
    m_cursorSet = true;
    if( wasSet ) updateCross( old );
    updateCross( p );
}

void KeyboardController::setFocused( bool focused )
{
    if( focused == m_focused ) return;
    m_focused = focused;
    if( !focused ) showPointer();
    if( m_cursorSet ) updateCross( m_cursor );
    qDebug() << "[kbd] canvas focus" << focused;
}

QPointF KeyboardController::cursorPos()
{
    if( !m_cursorSet )               // Never placed: the middle of the view
    {
        CircuitView* v = CircuitView::self();
        m_cursor = v ? snap( v->mapToScene( v->viewport()->rect().center() ) ) : QPointF( 4, 4 );
        m_cursorSet = true;
    }
    return m_cursor;
}

void KeyboardController::moveCursorTo( QPointF scenePos )
{
    QPointF old = cursorPos();
    m_cursor = snap( scenePos );
    updateCross( old );
    updateCross( m_cursor );
    if( CircuitView* v = CircuitView::self() ) v->ensureVisible( crossRect( m_cursor ), 32, 32 );
}

void KeyboardController::hidePointer()
{
    if( m_pointerHidden ) return;
    m_pointerHidden = true;
    QGuiApplication::setOverrideCursor( Qt::BlankCursor );
}

void KeyboardController::showPointer()
{
    if( !m_pointerHidden ) return;
    m_pointerHidden = false;
    QGuiApplication::restoreOverrideCursor();
}

QRectF KeyboardController::crossRect( QPointF p )
{
    return QRectF( p.x()-8, p.y()-8, 16, 16 );
}

void KeyboardController::updateCross( QPointF p )
{
    if( Circuit* c = Circuit::self() ) c->update( crossRect( p ) );
}

void KeyboardController::paint( QPainter* painter )
{
    if( !m_focused ) return;
    CircuitView* v = CircuitView::self();
    if( !v || painter->device() != v->viewport() ) return;  // Not in exports and captures of the circuit

    QPointF p = cursorPos();
    painter->save();
    painter->setRenderHint( QPainter::Antialiasing, true );
    painter->setPen( QPen( Theme::highlight(), 1.5, Qt::SolidLine, Qt::RoundCap ) );
    painter->drawLine( QPointF( p.x()-5.5, p.y() ), QPointF( p.x()+5.5, p.y() ) );
    painter->drawLine( QPointF( p.x(), p.y()-5.5 ), QPointF( p.x(), p.y()+5.5 ) );
    painter->restore();
}

QJsonObject KeyboardController::state()
{
    QJsonObject o;
    QPointF p = cursorPos();
    o["focused"]       = m_focused;
    o["cursor"]        = QJsonArray{ p.x(), p.y() };
    o["cursorVisible"] = cursorVisible();
    o["pointerHidden"] = m_pointerHidden;

    QJsonArray sel;
    if( Circuit* c = Circuit::self() )
        for( Component* comp : *c->compList() )
            if( comp->isSelected() ) sel.append( comp->getUid() );
    o["selection"] = sel;
    return o;      // The CLI adds the pins (pinFrom, pinTo, selectedPin) as "R1.lPin" refs
}
