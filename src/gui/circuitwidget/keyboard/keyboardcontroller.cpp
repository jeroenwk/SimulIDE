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
#include <QScrollBar>
#include <QCursor>

#include "keyboardcontroller.h"
#include "circuitview.h"
#include "circuit.h"
#include "component.h"
#include "theme.h"
#include "pin.h"
#include "node.h"
#include "linker.h"
#include "componentselector.h"
#include "utils.h"

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
    m_panMode       = false;
    m_override      = -1;
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
    m_panMode = false;
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

    // macOS sends arrow keys with the keypad modifier
    Qt::KeyboardModifiers mods = event->modifiers() & ~Qt::KeypadModifier;

    bool arrow = key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Up || key == Qt::Key_Down;
    bool panKey = mods == Qt::NoModifier && event->text() == "h";
    if( m_panMode && !arrow && !panKey ) setPanMode( false );   // Any other key ends pan mode

    hidePointer();              // Typing on the canvas: the pointer goes (pan mode shows its symbol)

    if( arrow && ( mods == Qt::NoModifier || mods == Qt::ShiftModifier ) ) return arrowKey( event, mods );

    if( panKey ){ setPanMode( true ); return true; }

    if( zoomKey( event, mods ) ) return true;

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

    // A lowercase letter bound to a part (Manage Components; defaults r, c, ...): place it
    QString t = event->text();
    if( mods == Qt::NoModifier && t.size() == 1 && t[0] >= 'a' && t[0] <= 'z' && partKey( t ) ) return true;

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

bool KeyboardController::partKey( QString letter )
{
    QHash<QString, QString> keys = ComponentSelector::self()->getShortCuts();
    QString name = keys.key( letter );
    if( name.isEmpty() ) return false;

    Circuit* circ = Circuit::self();
    Component* comp = circ->createItem( name, name+"-"+circ->newSceneId() );
    if( !comp ) return false;
    QPointF at = cursorPos();
    if( Component* sel = singlePart() )      // A part selected: a big step right of it (D12)
    {
        at = sel->pos()+QPointF( bigStep, 0 );
        moveCursorTo( at );
    }
    comp->setPos( toGrid( at ) );
    circ->addComponent( comp );
    circ->saveCompChange( comp->getUid(), COMP_STATE_NEW, "" );
    qDebug() << "[kbd] key" << letter << "places" << name << "at" << comp->pos();
    selectPart( comp );                      // Selected, pins paired with the last part
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
    Component* comp = singlePart();
    if( !comp ) return false;
    Circuit::self()->clearSelection();
    moveCursorTo( comp->pos()+QPointF( bigStep, 0 ) );   // A big step right of the part (D11)
    return true;
}

bool KeyboardController::anyPartSelected()
{
    Circuit* circ = Circuit::self();
    if( !circ ) return false;
    for( QGraphicsItem* item : circ->selectedItems() )
    {
        if( item->type() != QGraphicsItem::UserType+1 ) continue;
        Component* c = qgraphicsitem_cast<Component*>( item );
        if( c && c->itemType() != "Node" ) return true;
    }
    return false;
}

bool KeyboardController::cursorVisible() { return m_focused && !anyPartSelected(); }

void KeyboardController::setPanMode( bool on )
{
    if( on == m_panMode ) return;
    m_panMode = on;
    applyPointer();
    qDebug() << "[kbd] pan mode" << on;
}

// Cmd + / Cmd = : zoom in, Cmd - : zoom out, Cmd 0 : 100 %. On AZERTY 0 needs Shift:
// Cmd 0 is also matched by its physical key (number row)
bool KeyboardController::zoomKey( QKeyEvent* event, Qt::KeyboardModifiers mods )
{
    if( !( mods & Qt::ControlModifier ) || ( mods & ( Qt::AltModifier | Qt::MetaModifier ) ) ) return false;
    int key = event->key();
    if( key == Qt::Key_Plus || key == Qt::Key_Equal ){ zoomAround( 1.25 );     return true; }
    if( key == Qt::Key_Minus )                       { zoomAround( 1/1.25 );   return true; }
#ifdef Q_OS_MACOS
    bool zero = key == Qt::Key_0 || event->nativeVirtualKey() == 0x1D;   // kVK_ANSI_0
#else
    bool zero = key == Qt::Key_0;
#endif
    if( zero && !( mods & ~( Qt::ControlModifier | Qt::ShiftModifier ) ) )
    {
        CircuitView* v = CircuitView::self();
        if( !v ) return true;
        QPointF c = cursorVisible() ? cursorPos() : v->mapToScene( v->viewport()->rect().center() );
        v->zoomOne();
        v->centerOn( c );
        return true;
    }
    return false;
}

void KeyboardController::zoomAround( double factor )
{
    CircuitView* v = CircuitView::self();
    if( !v ) return;
    QPointF anchor = cursorVisible() ? cursorPos() : v->mapToScene( v->viewport()->rect().center() );
    QPoint before = v->mapFromScene( anchor );
    QGraphicsView::ViewportAnchor old = v->transformationAnchor();
    v->setTransformationAnchor( QGraphicsView::NoAnchor );
    v->zoomBy( factor );
    v->setTransformationAnchor( old );
    QPoint after = v->mapFromScene( anchor );        // Scroll so the anchor stays where it was
    QPoint d = after-before;
    v->horizontalScrollBar()->setValue( v->horizontalScrollBar()->value()+d.x() );
    v->verticalScrollBar()->setValue( v->verticalScrollBar()->value()+d.y() );
}

void KeyboardController::selectionChanged()
{
    if( m_cursorSet ) updateCross( m_cursor );   // Shown only while no part is selected
    if( m_panMode && anyPartSelected() ) setPanMode( false );
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

// Arrows (Shift: small step): pan mode (h): pan the canvas. Nothing selected: move the cross.
// A selection: move it 64 px, Shift: 8 px, with the wires attached, as a mouse drag does
bool KeyboardController::arrowKey( QKeyEvent* event, Qt::KeyboardModifiers mods )
{
    int key = event->key();
    QPointF dir;
    if     ( key == Qt::Key_Left  ) dir = QPointF(-1, 0 );
    else if( key == Qt::Key_Right ) dir = QPointF( 1, 0 );
    else if( key == Qt::Key_Up    ) dir = QPointF( 0,-1 );
    else                            dir = QPointF( 0, 1 );

    int step = ( mods == Qt::ShiftModifier ) ? smallStep : bigStep;
    Circuit* circ = Circuit::self();
    if( m_panMode )                                         // Scroll the view (screen pixels)
    {
        m_arrowBurst = false;
        if( CircuitView* v = CircuitView::self() ){
            v->horizontalScrollBar()->setValue( v->horizontalScrollBar()->value()+dir.x()*step );
            v->verticalScrollBar()->setValue(   v->verticalScrollBar()->value()  +dir.y()*step );
        }
        return true;
    }
    QList<QGraphicsItem*> sel = circ->selectedItems();
    if( sel.isEmpty() )
    {
        m_arrowBurst = false;
        moveCursorTo( cursorPos()+dir*step );
        return true;
    }
    QPointF d = dir*step;

    std::sort( sel.begin(), sel.end() );
    bool newStep = !m_arrowBurst || m_burstEdit != circ->editCount() || sel != m_burstSelection;
    circ->moveSelection( d, newStep );
    m_arrowBurst     = true;
    m_burstEdit      = circ->editCount();
    m_burstSelection = sel;

    refreshAutoPins();                                      // (The cross stays where it is)
    if( CircuitView* v = CircuitView::self() ) v->ensureVisible( v->selectedRect(), 32, 32 );
    return true;
}

void KeyboardController::mouseMoved( QPoint viewPos )
{
    // Ignore "moves" to the same place (sent when the pointer is hidden or shown)
    if( m_lastMouseSet && viewPos == m_lastMousePos ) return;
    m_lastMousePos = viewPos;
    m_lastMouseSet = true;
    showPointer();                          // The cross does not follow the mouse
}

void KeyboardController::mousePressed() { setPanMode( false ); }

void KeyboardController::clickedEmpty( QPointF scenePos )
{
    QPointF old = cursorPos();
    m_cursor = snap( scenePos );
    updateCross( old );
    updateCross( m_cursor );
}

void KeyboardController::setFocused( bool focused )
{
    if( focused == m_focused ) return;
    m_focused = focused;
    if( !focused ){ m_panMode = false; showPointer(); }
    applyPointer();
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
    applyPointer();
}

void KeyboardController::showPointer()
{
    if( !m_pointerHidden ) return;
    m_pointerHidden = false;
    applyPointer();
}

void KeyboardController::applyPointer()
{
    int want = -1;
    if     ( m_panMode && m_focused ) want = Qt::SizeAllCursor;   // Pan: a cross with arrows
    else if( m_pointerHidden )        want = Qt::BlankCursor;
    if( want == m_override ) return;
    if( m_override >= 0 ) QGuiApplication::restoreOverrideCursor();
    if( want >= 0 )       QGuiApplication::setOverrideCursor( QCursor( (Qt::CursorShape)want ) );
    m_override = want;
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
    if( !cursorVisible() ) return;
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
    o["panMode"]       = m_panMode;
    if( CircuitView* v = CircuitView::self() ) o["zoom"] = v->transform().m11();

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
