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
#include <QDialog>
#include "propdialog.h"
#include "comproperty.h"
#include "label.h"

#include "keyboardcontroller.h"
#include "circuitview.h"
#include "circuit.h"
#include "circuitwidget.h"
#include "simulator.h"
#include "connector.h"
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
    m_valueEdit     = false;
}

QPointF KeyboardController::snap( QPointF p )
{
    return QPointF( 8*qRound( ( p.x()-4 )/8.0 )+4, 8*qRound( ( p.y()-4 )/8.0 )+4 );
}

void KeyboardController::reset()
{
    m_panMode = false;
    m_valueEdit = false;
    m_valueUid.clear();
    m_valueProp.clear();
    m_valueBuf.clear();
    m_leavePin.clear();
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

    if( zoomKey( event, mods ) ) return true;

    // Cmd+R / Cmd+Shift+R: rotate; with an outgoing pin (pinFrom) on another part, the
    // rotated part is also moved so its closest pin shares that pin's row or column
    if( key == Qt::Key_R && ( mods & Qt::ControlModifier )
     && !( mods & ( Qt::AltModifier | Qt::MetaModifier ) ) )
        return rotateKey( mods & Qt::ShiftModifier );

    m_arrowBurst = false;       // Any other key ends a burst of arrow moves

    // Typing the selected part's value (K7). While editing, value keys build the buffer;
    // any other key reverts first and then does its normal action
    if( m_valueEdit )
    {
        if( valueKey( event, mods ) ) return true;
        endValueEdit();
    }
    else
    {
        QChar first = valueEntryChar( event, mods );           // A digit, - . , enters the edit
        if( first != '\0' && startValueEdit( first ) ) return true;
    }

    if( panKey ){ setPanMode( true ); return true; }

    if( mods == Qt::NoModifier || mods == Qt::ShiftModifier )
    {
        if( key == Qt::Key_Backtab || ( key == Qt::Key_Tab && mods == Qt::ShiftModifier ) ) return tabKey(-1 );
        if( key == Qt::Key_Tab ) return tabKey( 1 );
    }
    if( mods == Qt::NoModifier )
    {
        if( key == Qt::Key_Escape ) return escKey();
        if( key == Qt::Key_Return || key == Qt::Key_Enter ) return enterKey();
        if( key == Qt::Key_Space ) return spaceKey();
    }
    if( mods == Qt::ShiftModifier && ( key == Qt::Key_Return || key == Qt::Key_Enter ) )
        return enterKey( true );                         // Beyond the opposite pin (D16)

    // A lowercase letter bound to a part (Manage Components; defaults r, c, ...): place it
    QString t = event->text();
    if( mods == Qt::NoModifier && t.size() == 1 && t[0] >= 'a' && t[0] <= 'z' && partKey( t ) ) return true;

    // _ : wire pinFrom to pinTo (K8). Unshifted on AZERTY, shifted elsewhere: matched by text
    QString text = event->text();
    if( text == "_" && ( mods == Qt::NoModifier || mods == Qt::ShiftModifier ) ) return wireKey();

    // < and > : next / previous pin of the selected part (Cmd: also the "from" pin).
    // The character decides (AZERTY: < unshifted, > with Shift); with Cmd macOS gives no text
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
    moveCursorTo( comp->pos() );             // The (hidden) cross at the part: Esc shows it there
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

// A side's clockwise quarter: up 0, right 1, down 2, left 3 (a rotation of q*90° turns
// a pin pointing s into the side (q + cw(s)) % 4)
static int cwQuarter( QString side )
{ return side == "up" ? 0 : side == "right" ? 1 : side == "down" ? 2 : 3; }

// The outward direction a pin faces (unit vector)
static QPointF pinDir( Pin* p )
{
    QString s = p->side();
    if( s == "right" ) return QPointF( 1, 0 );
    if( s == "left"  ) return QPointF(-1, 0 );
    if( s == "down"  ) return QPointF( 0, 1 );
    return QPointF( 0,-1 );
}

// Cmd+R / Cmd+Shift+R: rotate the selected part. With an outgoing pin (pinFrom) on
// another part, the rotated part is then moved so that its pin closest to that pin
// shares its row or column (the smaller offset is eliminated), keeping a straight
// wire possible (D19). The shape does not matter: any pin can end up off the line.
bool KeyboardController::rotateKey( bool ccw )
{
    Component* comp = singlePart();
    Pin* f = pinFrom();
    if( !comp || !f || f->component() == comp ) return false;   // The plain rotation

    if( ccw ) comp->rotateCCW();
    else      comp->rotateCW();

    Pin* near = nullptr;                     // The rotated part's pin closest to the
    double best = 1e300;                     // outgoing pin
    for( Pin* q : comp->pinsClockwise() )
    {
        QPointF d = q->scenePos()-f->scenePos();
        double dd = QPointF::dotProduct( d, d );
        if( dd < best-1e-9 ){ best = dd; near = q; }
    }
    if( near )
    {
        QPointF d = near->scenePos()-f->scenePos();
        QPointF move( 0, 0 );
        if( qAbs( d.y() ) <= qAbs( d.x() ) ) move.ry() = -d.y();   // Side by side: one row
        else                                 move.rx() = -d.x();   // Stacked: one column
        if( !move.isNull() )
        {
            Circuit::self()->moveSelection( move, true );          // Wires follow, one undo step
            moveCursorTo( cursorPos()+move );                      // The cross follows the move
            refreshAutoPins();
        }
    }
    if( CircuitView* v = CircuitView::self() ) v->ensureVisible( comp->sceneBoundingRect(), 32, 32 );
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
    bool byPin = false;                      // Placed relative to a pin of the chain (D17)
    Pin* fromPin = nullptr;
    Component* sel = singlePart();
    if( sel ) fromPin = sel->selectedPin();  // A part selected: continue from its pin
    else if( !m_leavePin.isEmpty() && cursorPos() == m_leavePos )
        fromPin = Circuit::self()->pinById( m_leavePin );  // The cross still where Enter left it
    if( fromPin )
    {
        byPin = true;
        QPointF dir = pinDir( fromPin );     // The new part's first pin goes a big step
        at = snap( fromPin->scenePos() + dir*bigStep );   // beyond the pin, facing it in a
                                                          // straight line
        std::vector<Pin*> pins = comp->getPins();
        if( !pins.empty() )
        {   // Turn the new part so its first pin points back at the pin
            QString target;                  // The side that faces the pin: opposite of dir
            if( dir.x() > 0 ) target = "left";
            else if( dir.x() < 0 ) target = "right";
            else if( dir.y() > 0 ) target = "up";
            else target = "down";
            int q = ( cwQuarter( target ) - cwQuarter( pins.front()->side() ) + 4 ) % 4;
            if( q >= 2 )
            {   // The same orientation, reached as a flip plus a 0/90° rotation instead
                q = ( q+2 ) % 4;             // of 180/270°: a label counter-flips a flip,
                if( target == "left" || target == "right" )   // so it stays upright — the
                     comp->setHflip( -1 );   // flip mirrors the side the rotation lands
                else comp->setVflip( -1 );   // on (a flip is a scale: -1 mirrors) (D17)
            }
            comp->setRotation( q*90 );
        }
    }
    else if( sel ) at = sel->pos()+QPointF( bigStep, 0 ); // No pins: the old rule (D12)
    comp->setPos( toGrid( at ) );
    circ->addComponent( comp );
    if( byPin && !comp->getPins().empty() )              // The first pin exactly at its spot
        comp->setPos( comp->pos() + ( at-comp->getPins().front()->scenePos() ) );
    circ->saveCompChange( comp->getUid(), COMP_STATE_NEW, "" );
    qDebug() << "[kbd] key" << letter << "places" << name << "at" << comp->pos()
             << "rotation" << comp->rotation();
    moveCursorTo( comp->pos() );             // The cross goes with the part (D13)
    selectPart( comp );                      // Selected, pins paired with the last part
    if( fromPin )                            // Its selected pin: the one nearest to the
    {                                        // outgoing pin (D18), pinFrom that pin
        Pin* near = nullptr;
        double best = 1e300;
        for( Pin* q : comp->pinsClockwise() )
        {
            QPointF d = q->scenePos()-fromPin->scenePos();
            double dd = QPointF::dotProduct( d, d );
            if( dd < best-1e-9 ){ best = dd; near = q; }
        }
        if( near )
        {
            comp->setSelectedPin( near, false );
            setPinFrom( fromPin );
            setPinTo( near );
        }
    }
    return true;
}

// Space: open the selected part's properties; closing the dialog gives the canvas
// focus back (also with Esc: the dialog takes the key itself and rejects)
bool KeyboardController::spaceKey()
{
    Component* comp = singlePart();
    if( !comp ) return false;                 // Nothing, several parts, or a junction: nothing
    comp->openProperties();               // Public slotProperties (also clears legacy switch keys)
    if( PropDialog* d = comp->propDialog() )
    {
        if( m_focusDialog != d )              // Once per dialog: it is reused while the part lives
        {
            m_focusDialog = d;
            QObject::connect( d, &QDialog::finished, d, [](){
                if( CircuitView* v = CircuitView::self() ) v->setFocus( Qt::OtherFocusReason );
            });
        }
    }
    return true;
}

// _: wire pinFrom to pinTo (K8). The pins and the selection stay; the new wire is not
// selected. A line in Messages says why nothing was wired
bool KeyboardController::wireKey()
{
    auto say = []( QString text ){
        if( CircuitWidget* w = CircuitWidget::self() ) w->eventMessage( text, 0 );
    };
    Pin* f = pinFrom();
    Pin* t = pinTo();
    if( !f || !t )
    {
        say( "Keyboard: nothing to wire — choose the pins with > and < (one part selected)" );
        return true;
    }
    if( f == t )
    {
        say( "Keyboard: both ends are the same pin ("+f->pinId()+")" );
        return true;
    }
    if( f->connector() && f->connector() == t->connector() )
    {
        say( "Keyboard: "+f->pinId()+" and "+t->pinId()+" are already wired ("+f->connector()->getUid()+")" );
        return true;
    }

    bool running = Simulator::self()->isRunning();
    Connector* con = Circuit::self()->connectPins( f, t );
    if( !con )
    {
        say( "Keyboard: could not wire "+f->pinId()+" to "+t->pinId()
            +" (no wire segment of the net long enough for a junction dot)" );
        return true;
    }
    if( running && !Simulator::self()->isRunning() )
        say( "Keyboard: simulation stopped to wire" );
    if( Component* comp = singlePart() )     // The wired part continues out its other
    {                                        // side: its selected pin becomes the pin
        Pin* wired = nullptr;                // most opposite to the wired one (D18)
        if( f->component() == comp ) wired = f;
        else if( t->component() == comp ) wired = t;
        if( wired )
            if( Pin* op = oppositePin( comp, wired ) )
                comp->setSelectedPin( op, false );
    }
    setPinFrom( nullptr );                   // The wire is made: the wired pins are no
    setPinTo( nullptr );                     // longer the wiring pair, their marks go
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

// The pin pointing most opposite to sel (D16, Shift+Enter): the most opposite direction;
// ties (the dot product, exact for the 4 directions) → the one pointing more downwards;
// still tied → the nearest to sel. nullptr: no other visible pin
Pin* KeyboardController::oppositePin( Component* comp, Pin* sel )
{
    QPointF d0 = pinDir( sel );
    Pin* best = nullptr;
    double bestDot = 0, bestDown = 0;
    double bestDist = 0;
    for( Pin* p : comp->pinsClockwise() )
    {
        if( p == sel ) continue;
        QPointF d = pinDir( p );
        double dot  = QPointF::dotProduct( d, d0 );       // -1 = most opposite
        double down = d.y();                              // +1: points down (scene y grows down)
        QPointF dv  = p->scenePos()-sel->scenePos();
        double dist = QPointF::dotProduct( dv, dv );
        bool better = false;
        if( !best || dot < bestDot ) better = true;       // More opposite
        else if( dot == bestDot )
        {
            if( down > bestDown ) better = true;          // Ties: pointing more downwards
            else if( down == bestDown && dist < bestDist ) better = true;   // Then the nearest
        }
        if( better ){ best = p; bestDot = dot; bestDown = down; bestDist = dist; }
    }
    return best;
}

// Where the cross goes when a part is left (Enter, Shift+Enter, a committed value):
// a big step beyond its selected pin, in the pin's direction (Shift+Enter: beyond the
// most opposite pin). A part without pins: a big step right of the centre (D12)
QPointF KeyboardController::leavePos( Component* comp, bool opposite, Pin** used )
{
    if( used ) *used = nullptr;
    Pin* pin = comp->selectedPin();
    if( pin )
    {
        if( opposite ){
            Pin* op = oppositePin( comp, pin );
            if( op ) pin = op;
        }
        if( used ) *used = pin;
        return pin->scenePos() + pinDir( pin )*bigStep;
    }
    return comp->pos() + QPointF( bigStep, 0 );
}

// Enter: unselect the part, the cross beyond its (opposite: most opposite) pin.
// The pin is remembered (D17): a part placed while the cross is still there
// continues the chain, as with the part selected
bool KeyboardController::enterKey( bool opposite )
{
    Component* comp = singlePart();
    if( !comp ) return false;
    Pin* pin = nullptr;
    QPointF at = leavePos( comp, opposite, &pin );
    Circuit::self()->clearSelection();
    moveCursorTo( at );                                   // Beyond the selected pin (D16)
    m_leavePin = pin ? pin->pinId() : QString();
    m_leavePos = cursorPos();
    return true;
}

// The char a digit, - or . , key types ('\0': not a value key). D10: the AZERTY number
// row counts as digits with or without Shift, matched by its physical key (kVK_ANSI_0…9,
// the number row on every layout) — except "-" (the AZERTY 6 key), which is a minus,
// and "_", the wire key (K8)
QChar KeyboardController::valueEntryChar( QKeyEvent* event, Qt::KeyboardModifiers mods )
{
    if( !( mods == Qt::NoModifier || mods == Qt::ShiftModifier ) ) return '\0';
    QString t = event->text();
    if( t.size() == 1 )
    {
        if( t[0].isDigit() ) return t[0];
        if( t[0] == '-' ) return '-';
        if( t[0] == '.' || t[0] == ',' ) return '.';
        if( t[0] == '_' ) return '\0';
    }
    int key = event->key();
    if( key >= Qt::Key_0 && key <= Qt::Key_9 ) return QChar( '0'+key-Qt::Key_0 );  // é = Key_2
#ifdef Q_OS_MACOS
    quint32 vk = event->nativeVirtualKey();              // D10, as the zoom 0 key (K1 rev)
    if     ( vk == 0x12 ) return '1';
    else if( vk == 0x13 ) return '2';
    else if( vk == 0x14 ) return '3';
    else if( vk == 0x15 ) return '4';
    else if( vk == 0x16 ) return '6';
    else if( vk == 0x17 ) return '5';
    else if( vk == 0x19 ) return '9';
    else if( vk == 0x1A ) return '7';
    else if( vk == 0x1C ) return '8';
    else if( vk == 0x1D ) return '0';
#endif
    if( key == Qt::Key_Minus ) return '-';
    if( key == Qt::Key_Period || key == Qt::Key_Comma ) return '.';
    return '\0';
}

bool KeyboardController::startValueEdit( QChar first )
{
    Component* comp = singlePart();
    if( !comp ) return false;
    QString prop = comp->valueProperty();
    if( prop.isEmpty() ) return false;                   // This part takes no typed value

    m_valueEdit = true;
    m_valueUid  = comp->getUid();
    m_valueProp = prop;
    m_valueBuf  = ( first == '.' ) ? "0." : QString( first );
    qDebug() << "[kbd] value edit" << prop << "on" << m_valueUid;
    updateValueLabel();
    return true;
}

// A key while a value is typed. false: not a value key, the edit reverts and the key
// does its normal action (Tab: next part, r: place a part, ...)
bool KeyboardController::valueKey( QKeyEvent* event, Qt::KeyboardModifiers mods )
{
    int key = event->key();
    if( key == Qt::Key_Backspace && ( mods == Qt::NoModifier || mods == Qt::ShiftModifier ) )
    {
        if( !m_valueBuf.isEmpty() ) m_valueBuf.chop(1);
        if( m_valueBuf.isEmpty() ) endValueEdit();       // D8: no char left: the edit ends; the
        else                       updateValueLabel();   // next Backspace deletes the part
        return true;
    }
    if( mods == Qt::NoModifier )
    {
        if( key == Qt::Key_Escape ) { endValueEdit(); return true; }   // Revert, stay selected
        if( key == Qt::Key_Return || key == Qt::Key_Enter ) { commitValue(); return true; }
    }
    QChar c = valueEntryChar( event, mods );
    if( c != '\0' ){ valueChar( c ); return true; }
    QString t = event->text();
    if( t.size() == 1 && ( mods == Qt::NoModifier || mods == Qt::ShiftModifier ) )
    {   // Multipliers p n u m k M G (u = µ, M mega, m milli); capitals type the small
        // ones too (K = k), so a shifted keyboard works the same
        if( QString("pnumkMG").contains( t[0] ) )          { valueChar( t[0] );          return true; }
        if( QString("PNUK").contains( t[0] ) )             { valueChar( t[0].toLower() ); return true; }
    }
    return false;
}

// The buffer's multiplier letter, '\0' when none is typed
static QChar bufferMult( const QString& buf )
{
    for( QChar c : buf )
        if( !c.isDigit() && c != '-' && c != '.' && c != ',' ) return c;
    return '\0';
}

// One buffer char, per the grammar [-]digits[.digits][mult[digits]]. What does not
// fit (a second dot, a second multiplier, a sign mid-value) is ignored
void KeyboardController::valueChar( QChar c )
{
    bool hasMult = bufferMult( m_valueBuf ) != '\0';
    bool hasDot  = m_valueBuf.contains( '.' );
    int  digits  = 0;
    for( QChar ch : m_valueBuf ) if( ch.isDigit() ) digits++;
    if( c.isDigit() ) m_valueBuf.append( c );            // Digits always fit (4k7 too)
    else if( c == '.' )            { if( !hasDot && !hasMult && digits > 0 ) m_valueBuf.append( c ); }
    else if( c != '-' )            { if( !hasMult && digits > 0 ) m_valueBuf.append( c ); }
    // c == '-': the sign is only the entry key; mid-edit it is ignored
    updateValueLabel();
}

// The value label shows the buffer, an edit marker and the unit: 200k▏Ω, in highlight.
// While no multiplier is typed the part's current one is shown (200▏kΩ): it does not
// change unless the buffer types one
void KeyboardController::updateValueLabel()
{
    Component* comp = compByUid( m_valueUid );
    if( !comp ) return;
    QString unit;
    if( ComProperty* p = comp->property( m_valueProp ) )
    {
        unit = p->unit();
        if( bufferMult( m_valueBuf ) != '\0'             // A typed multiplier replaces it:
         && unit.size() > 1 && QString("pnµmkMGT").contains( unit.at(0) ) ) unit = unit.mid( 1 );
        if( unit.startsWith("_") ) unit = unit.mid( 1 );                 // "_%": shown without the _
    }
    comp->valLabel()->setDefaultTextColor( Theme::highlight() );
    comp->setValLabelText( m_valueBuf+QChar( 0x258F )+unit );
}

// Revert: the value from before the edit is shown again (the property was never changed)
void KeyboardController::endValueEdit()
{
    if( !m_valueEdit ) return;
    m_valueEdit = false;
    Component* comp = compByUid( m_valueUid );
    m_valueUid.clear();
    m_valueBuf.clear();
    if( !comp ) return;
    comp->valLabel()->setDefaultTextColor( Theme::valLabel() );
    if( ComProperty* p = comp->property( m_valueProp ) )
        comp->setValLabelText( p->getValStr() );
    m_valueProp.clear();
}

// The buffer as a value string the property takes: "200 kΩ", "4.7 µF", "100 nF";
// digits after the multiplier are the fraction (4k7 = 4.7 k). A buffer without a
// multiplier keeps the part's current unit (4.7 kΩ + 100 Enter = 100 kΩ): the
// multiplier only changes when the buffer types one
QString KeyboardController::valueString( Component* comp )
{
    ComProperty* p = comp->property( m_valueProp );
    if( !p ) return "";
    QString num = m_valueBuf;
    QChar mult = bufferMult( m_valueBuf );
    if( mult != '\0' )
    {
        int i = m_valueBuf.indexOf( mult );
        num  = m_valueBuf.left( i );
        QString tail = m_valueBuf.mid( i+1 );
        if( !tail.isEmpty() ) num = num.contains('.') ? num+tail : num+"."+tail;
    }
    num.replace( ',', '.' );

    QString unit = p->unit();
    if( mult == '\0' )                                   // Keep the current multiplier
        return num + ( unit.isEmpty() ? "" : " "+unit );
    QString base = unit;
    if( base.size() > 1 && QString("pnµmkMGT").contains( base.at(0) ) ) base = base.mid( 1 );
    if( base.startsWith("_") || base.isEmpty() )         // No SI prefixes: the value itself
        return QString::number( num.toDouble()*getMultiplier( QString( mult ) ), 'g', 15 )+" "+base;
    return num+" "+( mult == 'u' ? QString("µ") : QString( mult ) )+base;
}

// Enter: set the property (one undo step), leave the edit, unselect, cursor right.
// What the property refuses it clamps; the label shows what was set
void KeyboardController::commitValue()
{
    Component* comp = compByUid( m_valueUid );
    bool digits = false;
    for( QChar ch : m_valueBuf ) if( ch.isDigit() ) digits = true;
    if( comp && digits )
    {
        QString val = valueString( comp );
        QString old = comp->getPropStr( m_valueProp );
        Circuit* circ = Circuit::self();
        circ->beginUndoStep();
        comp->setPropStr( m_valueProp, val );
        circ->calcCircuitChanges();
        circ->addCompChange( comp->getUid(), m_valueProp, old );
        circ->endCircuitBatch();
        circ->setChanged();
        qDebug() << "[kbd] value" << val << "->" << m_valueProp << "on" << m_valueUid;
    }
    endValueEdit();
    if( comp )                                           // Then as Enter (K4): beyond the pin (D16)
    {
        Pin* pin = nullptr;
        QPointF at = leavePos( comp, false, &pin );
        Circuit::self()->clearSelection();
        moveCursorTo( at );
        m_leavePin = pin ? pin->pinId() : QString();
        m_leavePos = cursorPos();
    }
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
    if( m_valueEdit )                                    // The edit ends when its part is no
    {                                                    // longer the single selection (K7)
        Component* c = singlePart();
        if( !c || c->getUid() != m_valueUid ) endValueEdit();
    }
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
    // The wiring pair lives as long as both its pins: when a deleted part took one
    // (deleting the selected part clears the selection, which lands here), the pair
    // is gone and its marks go with it
    if( ( !m_pinFrom.isEmpty() && !pinFrom() ) || ( !m_pinTo.isEmpty() && !pinTo() ) )
    {
        setPinFrom( nullptr );
        setPinTo( nullptr );
    }
    Component* b = singlePart();
    if( b ) moveCursorTo( b->pos() );            // Whatever selects one part — Tab, a click,
    if( !b || b->getUid() == m_lastPart ) return;   // the CLI — parks the hidden cross on it

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

    moveCursorTo( cursorPos()+d );             // The (hidden) cross follows: Esc shows it
    refreshAutoPins();                         // where the part ended up
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

void KeyboardController::mousePressed() { endValueEdit(); setPanMode( false ); }   // A click reverts (K7)

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
    if( m_selPending ) settleSelection();     // First: a click's queued selection rule
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
    Component* lp = lastPart();
    o["lastPart"] = lp ? QJsonValue( lp->getUid() ) : QJsonValue();
    if( m_valueEdit )                                     // K7: the value being typed
        o["valueEdit"] = QJsonObject{ {"part", m_valueUid}, {"property", m_valueProp},
                                       {"buffer", m_valueBuf} };
    return o;      // The CLI adds the pins (pinFrom, pinTo, selectedPin) as "R1.lPin" refs
}
