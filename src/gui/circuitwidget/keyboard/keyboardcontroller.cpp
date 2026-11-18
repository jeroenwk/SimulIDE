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
#include <QTimer>
#include <QGraphicsItem>

#include "keyboardcontroller.h"
#include "circuitview.h"
#include "circuit.h"
#include "component.h"
#include "theme.h"
#include "pin.h"
#include "node.h"
#include "linker.h"

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
    m_selPending    = false;
    m_circuit       = nullptr;
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
    m_lastPart.clear();
    m_prevPart.clear();
    m_selPending = false;

    Circuit* circ = Circuit::self();     // A new Circuit (scene) for every new / opened circuit
    if( circ && circ != m_circuit )
    {
        m_circuit = circ;
        QObject::connect( circ, &QGraphicsScene::selectionChanged, [](){ KeyboardController::self()->selectionChanged(); } );
    }
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

    if( mods == Qt::NoModifier || mods == Qt::ShiftModifier )
    {
        if( key == Qt::Key_Backtab || ( key == Qt::Key_Tab && mods == Qt::ShiftModifier ) ) return tabKey(-1 );
        if( key == Qt::Key_Tab ) return tabKey( 1 );
    }
    if( mods == Qt::NoModifier )
    {
        if( key == Qt::Key_Escape ) return escKey();
        if( key == Qt::Key_Return || key == Qt::Key_Enter ) return enterKey();
    }

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

Component* KeyboardController::compByUid( QString uid )
{
    if( uid.isEmpty() ) return nullptr;
    for( Component* c : *Circuit::self()->compList() ) if( c->getUid() == uid ) return c;
    return nullptr;
}

Component* KeyboardController::lastPart() { return compByUid( m_lastPart ); }

// Parts (no junctions, no hidden parts, no parts inside boards) in reading order:
// rows top to bottom (centres within 16 px are one row), left to right in a row
QList<Component*> KeyboardController::readingOrder()
{
    QList<Component*> parts;
    for( Component* c : *Circuit::self()->compList() )
        if( c->itemType() != "Node" && c->isVisible() && !c->parentItem() ) parts.append( c );

    std::sort( parts.begin(), parts.end(), []( Component* a, Component* b ){
        return a->sceneBoundingRect().center().y() < b->sceneBoundingRect().center().y(); });

    QList<Component*> out, row;
    double rowY = 0;
    auto flush = [&](){
        std::sort( row.begin(), row.end(), []( Component* a, Component* b ){
            return a->sceneBoundingRect().center().x() < b->sceneBoundingRect().center().x(); });
        out += row;
        row.clear();
    };
    for( Component* c : parts )
    {
        double y = c->sceneBoundingRect().center().y();
        if( !row.isEmpty() && y-rowY > 16 ) flush();
        if( row.isEmpty() ) rowY = y;
        row.append( c );
    }
    flush();
    return out;
}

void KeyboardController::selectPart( Component* comp )
{
    Circuit* circ = Circuit::self();
    circ->clearSelection();
    comp->setSelected( true );
    settleSelection();                       // Now, not queued: the keys that follow see it
    if( CircuitView* v = CircuitView::self() ) v->ensureVisible( comp->sceneBoundingRect(), 32, 32 );
}

bool KeyboardController::tabKey( int dir )
{
    QList<Component*> order = readingOrder();
    if( order.isEmpty() ) return true;       // Keep Tab on the canvas anyway

    Component* cur = nullptr;
    for( Component* c : order ) if( c->isSelected() ){ cur = c; break; }   // First selected, in order

    Component* next;
    if( cur ){
        int n = order.size();
        next = order[ ( ( order.indexOf( cur )+dir ) % n + n ) % n ];
    }else{                                   // Nothing selected: the part nearest the cursor
        QPointF p = cursorPos();
        next = order.first();
        double best = 1e300;
        for( Component* c : order )
        {
            QPointF d = c->sceneBoundingRect().center()-p;
            double dd = QPointF::dotProduct( d, d );
            if( dd < best ){ best = dd; next = c; }
        }
    }
    selectPart( next );
    return true;
}

bool KeyboardController::escKey()
{
    if( Linker::m_selecComp ) return false;  // Old behaviour: stop linking
    Circuit* circ = Circuit::self();
    if( circ->selectedItems().isEmpty() ) return false;
    circ->clearSelection();                  // lastPart and the pins are kept (D3)
    return true;
}

bool KeyboardController::enterKey()
{
    if( !singlePart() ) return false;
    Circuit::self()->clearSelection();
    moveCursorTo( cursorPos()+QPointF( bigStep, 0 ) );
    return true;
}

void KeyboardController::selectionChanged()
{
    if( m_selPending ) return;
    m_selPending = true;
    QTimer::singleShot( 0, [](){ KeyboardController::self()->settleSelection(); } );
}

// The nearest pins of a and b, keeping a pin chosen by hand
void KeyboardController::pairPins( Component* a, Component* b )
{
    Pin* pa = a->selectedPin();
    Pin* pb = b->selectedPin();
    if( !pa || !pb ) return;
    bool ma = a->selectedPinManual(), mb = b->selectedPinManual();

    auto dist = []( Pin* x, Pin* y ){ QPointF d = x->scenePos()-y->scenePos(); return QPointF::dotProduct( d, d ); };
    std::vector<Pin*> la = ma ? std::vector<Pin*>{ pa } : a->pinsClockwise();
    std::vector<Pin*> lb = mb ? std::vector<Pin*>{ pb } : b->pinsClockwise();
    double best = 1e300;
    for( Pin* x : la ) for( Pin* y : lb )
    {
        double d = dist( x, y );
        if( d < best-1e-9 ){ best = d; pa = x; pb = y; }
    }
    if( !ma ) a->setSelectedPin( pa, false );
    if( !mb ) b->setSelectedPin( pb, false );
    setPinFrom( pa );
    setPinTo( pb );
}

void KeyboardController::settleSelection()
{
    m_selPending = false;
    Component* b = singlePart();
    if( !b || b->getUid() == m_lastPart ) return;

    Component* a = lastPart();
    if( a ){
        m_prevPart = m_lastPart;
        pairPins( a, b );
    }else{
        m_prevPart.clear();
        setPinTo( b->selectedPin() );
    }
    m_lastPart = b->getUid();
}

void KeyboardController::refreshAutoPins()
{
    Component* a = compByUid( m_prevPart );
    Component* b = lastPart();
    if( !a || !b || a == b ) return;
    Pin* f = pinFrom();
    Pin* t = pinTo();
    if( !f || !t || f->component() != a || t->component() != b ) return;   // Changed by hand since
    if( a->selectedPinManual() && b->selectedPinManual() ) return;
    pairPins( a, b );
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
    refreshAutoPins();
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
    if( m_selPending ) settleSelection();
    Component* lp = lastPart();
    o["lastPart"] = lp ? QJsonValue( lp->getUid() ) : QJsonValue();
    return o;      // The CLI adds the pins (pinFrom, pinTo, selectedPin) as "R1.lPin" refs
}
