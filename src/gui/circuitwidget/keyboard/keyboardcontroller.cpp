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
#include <QSet>
#include <QGraphicsItem>
#include <QScrollBar>
#include <QCursor>
#include <QDialog>
#include "propdialog.h"
#include "comproperty.h"
#include "label.h"

#include "keyboardcontroller.h"
#include "wirestyledialog.h"
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
#include "subcircuit.h"
#include "pinchain.h"
#include "physicalkeys.h"
#include "connectorline.h"
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
    m_walkSelecting = false;
    m_drawing       = false;
    m_drawSrcVertical = false;
}

QPointF KeyboardController::snap( QPointF p )
{
    return QPointF( 8*qRound( ( p.x()-4 )/8.0 )+4, 8*qRound( ( p.y()-4 )/8.0 )+4 );
}

void KeyboardController::reset()
{
    m_panMode = false;
    m_value.reset();
    m_walk.stop();
    m_drawing = false;                   // The circuit (and its wire being drawn) is gone
    m_drawPts.clear();
    m_drawMarks.clear();
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

    if( zoomKey( event, mods ) ) return true;   // The view only: a value being typed stays

    if( m_rect )                                         // The selection rectangle: Shift+arrows
    {                                                    // grow it; any other key ends it (Esc:
        if( rectKey( event, mods ) ) return true;        // cancelled), then acts
        bool esc = key == Qt::Key_Escape;
        endRect( !esc );
        if( esc ) return true;
    }

    if( m_drawing ) return drawKey( event, mods );   // K9b: every key goes to the wire being drawn

    // Contexts, in order (spec §4.1): EditValue, network mode (K9), parts mode.
    // EditValue (K7): value keys build the buffer; any other key (arrows and Cmd+R too)
    // reverts first and then does its normal action
    if( m_value.active() )
    {
        QString uid = m_value.uid();
        ValueEditor::result_t r = m_value.key( event, mods );
        if( r == ValueEditor::committed ){ leavePart( compByUid( uid ), false ); return true; }
        if( r == ValueEditor::handled ) return true;
        m_value.revert();
    }
    else
    {
        QChar first = ValueEditor::entryChar( event, mods );   // A digit, - . , enters the edit
        if( first != '\0' && m_value.start( singlePart(), first ) ) return true;
    }

    // Network mode (K9): the walk's keys; any other key ends it (as Esc), then acts in
    // parts mode
    if( m_walk.active() )
    {
        if( networkKey( event, mods ) ) return true;
        leaveNetwork( true );
    }

    // Parts mode

    if( arrow && !( mods & ( Qt::AltModifier | Qt::MetaModifier ) ) )   // Plain, Shift, Cmd, Cmd+Shift
    {
        if( ( mods & Qt::ShiftModifier ) && !m_panMode && Circuit::self()->selectedItems().isEmpty() )
        {
            m_rect = true;                               // Shift+arrows from the cross: a rectangle
            m_rectAnchor = cursorPos();
            qDebug() << "[kbd] selection rectangle from" << m_rectAnchor;
            return rectKey( event, mods );
        }
        return arrowKey( event, mods );
    }

    // Cmd+R / Cmd+Shift+R: rotate; with an outgoing pin (pinFrom) on another part, the
    // rotated part is also moved so its closest pin shares that pin's row or column
    if( key == Qt::Key_R && ( mods & Qt::ControlModifier )
     && !( mods & ( Qt::AltModifier | Qt::MetaModifier ) ) )
        return rotateKey( mods & Qt::ShiftModifier );

    m_arrowBurst = false;       // Any other key ends a burst of arrow moves

    if( panKey ){ setPanMode( true ); return true; }

    if( mods == Qt::NoModifier || mods == Qt::ShiftModifier )
    {
        if( key == Qt::Key_Backtab || ( key == Qt::Key_Tab && mods == Qt::ShiftModifier ) ) return tabKey(-1 );
        if( key == Qt::Key_Tab ) return tabKey( 1 );
    }
    if( event->text() == "=" && ( mods == Qt::NoModifier || mods == Qt::ShiftModifier ) )
        return enterNetwork();
    if( event->text() == "+" && ( mods == Qt::NoModifier || mods == Qt::ShiftModifier ) )
        return plusKey();                                // K9b: draw a wire from the wire here
    if( mods == Qt::NoModifier )
    {
        if( key == Qt::Key_Escape ) return escKey();
        if( key == Qt::Key_Return || key == Qt::Key_Enter ) return enterKey();
        if( key == Qt::Key_Space ) return spaceKey();
    }
    if( mods == Qt::ShiftModifier && ( key == Qt::Key_Return || key == Qt::Key_Enter ) )
        return enterKey( true );                         // Beyond the opposite pin (D16)

    // w : wire pinFrom to pinTo (K8, D20: reserved, never a part shortcut)
    QString text = event->text();
    if( text == "w" && mods == Qt::NoModifier ) return wireKey();

    // The component list (K9e): k places its selected part, : / select the next / previous
    if( text == "k" && mods == Qt::NoModifier ) return libraryPartKey();
    if( ( text == ":" || text == "/" ) && ( mods == Qt::NoModifier || mods == Qt::ShiftModifier ) )
    {
        QString name = ComponentSelector::self()->stepPart( text == ":" ? 1 : -1 );
        if( name.isEmpty() ) say("Keyboard: the component list is empty");
        return true;
    }

    // A lowercase letter bound to a part (Manage Components; defaults r, c, ...): place it
    if( mods == Qt::NoModifier && text.size() == 1 && text[0] >= 'a' && text[0] <= 'z' && partKey( text ) ) return true;

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
    settleSelection();                       // Now, not queued: the keys that follow see it
                                             // (it parks the hidden cross at the part)
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

// Cmd+R / Cmd+Shift+R: rotate the selected part. With an outgoing pin (pinFrom) on
// another part, the rotated part is then moved so that its pin closest to that pin
// shares its row or column (the smaller offset is eliminated), keeping a straight
// wire possible (D19). The shape does not matter: any pin can end up off the line.
bool KeyboardController::rotateKey( bool ccw )
{
    Component* comp = singlePart();
    Pin* f = pinFrom();
    if( !comp || !f || f->component() == comp ) return false;   // The plain rotation

    Circuit* circ = Circuit::self();
    int undoIndex = circ->undoIndex();       // The rotation and the shift: one undo step
    if( ccw ) comp->rotateCCW();
    else      comp->rotateCW();

    Pin* near = PinChain::nearest( comp, f->scenePos() );   // The pin closest to the outgoing one
    if( near )
    {
        QPointF d = near->scenePos()-f->scenePos();
        QPointF move( 0, 0 );
        if( qAbs( d.y() ) <= qAbs( d.x() ) ) move.ry() = -d.y();   // Side by side: one row
        else                                 move.rx() = -d.x();   // Stacked: one column
        if( !move.isNull() )
        {
            circ->moveSelection( move, true );                     // Wires follow, the cross
            refreshAutoPins();                                     // moves along
        }
    }
    circ->joinUndoSteps( undoIndex );
    if( CircuitView* v = CircuitView::self() ) v->ensureVisible( comp->sceneBoundingRect(), 32, 32 );
    return true;
}

bool KeyboardController::partKey( QString letter )
{
    QHash<QString, QString> keys = ComponentSelector::self()->getShortCuts();
    QString name = keys.key( letter );
    if( name.isEmpty() ) return false;
    return placePart( name, name );
}

// k (K9e): the part selected in the component list, placed as a part letter places;
// on a group row it expands or collapses the group instead
bool KeyboardController::libraryPartKey()
{
    QString type, name;
    if( ComponentSelector::self()->toggleSelectedGroup() ) return true;
    if( !ComponentSelector::self()->selectedPart( &type, &name ) )
    {
        say("Keyboard: no part selected in the component list — choose one with : and /");
        return true;
    }
    if( !placePart( type, name ) ) say("Keyboard: could not place "+name );
    return true;
}

// type, name: as the component list gives them (a native part: both its type; a
// subcircuit or MCU: "Subcircuit" / "MCU" and its name). At the cross, or continuing
// the chain from the selected part's pin (D17)
bool KeyboardController::placePart( QString type, QString name )
{
    Circuit* circ = Circuit::self();
    Component* comp = circ->createItem( type, name+"-"+circ->newSceneId() );
    if( !comp ) return false;
    if( type == "Subcircuit" )                           // As a drop from the list does
        if( SubCircuit* subC = static_cast<SubCircuit*>( comp ) )
            if( !subC->isBoard() ) subC->setLogicSymbol( true );
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
        QPointF dir = PinChain::dir( fromPin );   // The new part's first pin goes a big step
        at = snap( fromPin->scenePos() + dir*bigStep );   // beyond the pin, facing it in a
        PinChain::faceBack( comp, dir );                  // straight line
    }
    else if( sel ) at = sel->pos()+QPointF( bigStep, 0 ); // No pins: the old rule (D12)
    comp->setPos( toGrid( at ) );
    circ->addComponent( comp );
    if( byPin && !comp->getPins().empty() )              // The first pin exactly at its spot
        comp->setPos( comp->pos() + ( at-comp->getPins().front()->scenePos() ) );
    circ->saveCompChange( comp->getUid(), COMP_STATE_NEW, "" );
    qDebug() << "[kbd] places" << name << "at" << comp->pos()
             << "rotation" << comp->rotation();
    selectPart( comp );                      // Selected (the hidden cross goes with it, D13),
                                             // pins paired with the last part
    if( fromPin )                            // Its selected pin: the one nearest to the
    {                                        // outgoing pin (D18), pinFrom that pin
        if( Pin* near = PinChain::nearest( comp, fromPin->scenePos() ) )
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
    if( !comp )                               // Only wire segments selected (rubber band): their
    {                                         // colours (specs/wire-colors.md)
        QList<Connector*> wires;
        for( QGraphicsItem* item : Circuit::self()->selectedItems() )
        {
            if( item->type() == QGraphicsItem::UserType+2 )
            {
                Connector* con = qgraphicsitem_cast<ConnectorLine*>( item )->connector();
                if( !wires.contains( con ) ) wires.append( con );
            }
            else if( Component* c = qgraphicsitem_cast<Component*>( item ) )
            {
                if( c->itemType() != "Node" ) return false;   // A part too: nothing
            }
        }
        if( wires.isEmpty() ) return false;   // Nothing, several parts, or a junction: nothing
        editWireStyle( wires );
        return true;
    }
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

// w: wire pinFrom to pinTo (K8). The pins and the selection stay; the new wire is not
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
            if( Pin* op = PinChain::opposite( comp, wired ) )
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
    if( Component* comp = singlePart() )     // The cross is shown where the part is NOW,
        moveCursorTo( comp->pos() );         // also when it moved without the selection
    circ->clearSelection();                  // (a library drop): read at unselect time
    return true;
}

// Enter: unselect the part, the cross beyond its (opposite: most opposite) pin
bool KeyboardController::enterKey( bool opposite )
{
    Component* comp = singlePart();
    if( !comp ) return false;
    leavePart( comp, opposite );
    return true;
}

// Unselect a part (Enter, Shift+Enter, a committed value) and put the cross beyond its
// pin (D16). The pin is remembered (D17): a part placed while the cross is still there
// continues the chain, as with the part selected
void KeyboardController::leavePart( Component* comp, bool opposite )
{
    if( !comp ) return;
    Pin* pin = nullptr;
    QPointF at = PinChain::leavePos( comp, opposite, bigStep, &pin );
    Circuit::self()->clearSelection();
    moveCursorTo( at );
    m_leavePin = pin ? pin->pinId() : QString();
    m_leavePos = cursorPos();
}

// ----- Network mode (K9) -----------------------------------------------------

void KeyboardController::say( QString text )
{
    if( CircuitWidget* w = CircuitWidget::self() ) w->eventMessage( text, 0 );
}

// =: walk the network of the selected part's selected pin. No wire: a message, parts mode.
// Nothing selected (K9c): from the dot, corner or wire nearest to the cross
bool KeyboardController::enterNetwork()
{
    Component* comp = singlePart();
    if( !comp )
    {
        if( !Circuit::self()->selectedItems().isEmpty() ) return false;   // Several parts, a wire...
        NetworkWalk::Step seed;
        if( !NetworkWalk::nearestItem( cursorPos(), &seed ) || !m_walk.startFrom( seed ) )
        {
            say("Keyboard: no wire in the circuit to walk");
            return true;
        }
        qDebug() << "[kbd] network mode from the" << NetworkWalk::kindName( seed.kind ) << seed.uid
                 << "nearest to the cross," << m_walk.size() << "steps";
        m_arrowBurst = false;
        showStep();
        return true;
    }
    Pin* pin = comp->selectedPin();
    if( !pin ){ say("Keyboard: "+comp->getUid()+" has no pin to walk from"); return true; }
    if( !m_walk.start( pin ) )
    {
        say("Keyboard: "+pin->pinId()+" has no wire — nothing to walk (choose a wired pin with > or <)");
        return true;
    }
    qDebug() << "[kbd] network mode from" << pin->pinId() << m_walk.size() << "steps";
    m_arrowBurst = false;
    showStep();
    return true;
}

void KeyboardController::leaveNetwork( bool reselect )
{
    if( !m_walk.active() ) return;
    QString uid = m_walk.partUid();
    bool fromPart = !uid.isEmpty();
    QPoint at = m_walk.size() ? m_walk.current().pos : cursorPos().toPoint();
    markCorner( false );
    m_walk.stop();
    if( !m_marked.isEmpty() ){ m_marked.clear(); Circuit::self()->update(); }
    m_arrowBurst = false;
    m_walkSelecting = true;
    Circuit::self()->clearSelection();
    m_walkSelecting = false;
    qDebug() << "[kbd] parts mode";
    if( reselect && fromPart )
    {
        if( Component* comp = compByUid( uid ) ) selectPart( comp );   // Its pin unchanged
    }
    else if( reselect ) moveCursorTo( at );              // K9c, no part: the cross where it was
}

// Repaints where the step's mark was (on: and where it is now)
void KeyboardController::markCorner( bool on )
{
    Circuit* circ = Circuit::self();
    circ->update( m_stepMark );                          // Where it was
    m_stepMark = QRectF();
    if( !on || !m_walk.active() || !m_walk.size() ) return;
    NetworkWalk::Step s = m_walk.current();
    m_stepMark = QRectF( s.pos.x()-8, s.pos.y()-8, 16, 16 );
    if( Connector* con = m_walk.wire( s ) )
        if( s.kind == NetworkWalk::stepWire )
            for( ConnectorLine* l : *con->lineList() ) m_stepMark |= l->sceneBoundingRect().adjusted(-4,-4,4,4);
    circ->update( m_stepMark );
}

// scroll false: from a mouse press (the item is in view). Scrolling under a pressed
// button sends the wire a mouse move, which drags its segment
void KeyboardController::showStep( bool scroll )
{
    Circuit* circ = Circuit::self();
    NetworkWalk::Step s = m_walk.current();
    m_walkSelecting = true;
    circ->clearSelection();
    QRectF show( s.pos.x()-8, s.pos.y()-8, 16, 16 );
    if( s.kind == NetworkWalk::stepWire )
    {
        if( Connector* con = m_walk.wire( s ) )
        {
            con->setSelected( true );                    // All its segments highlighted
            for( ConnectorLine* l : *con->lineList() ) show |= l->sceneBoundingRect();
        }
    }
    else if( s.kind == NetworkWalk::stepDot )
    {
        if( Node* node = m_walk.dot( s ) ) node->setSelected( true );
    }
    pruneMarks();
    for( QString uid : m_marked )                        // Marked wires stay selected
        if( Connector* con = NetworkWalk::wire( NetworkWalk::Step{ NetworkWalk::stepWire, uid, -1, QPoint(), 0 } ) )
            con->setSelected( true );
    m_walkSelecting = false;
    if( !m_marked.isEmpty() ) circ->update();            // Marks and the dashed step
    markCorner( true );
    if( scroll )
        if( CircuitView* v = CircuitView::self() ) v->ensureVisible( show, 32, 32 );
}

void KeyboardController::walkFromMouse( int kind, QString uid, int index, QPoint pos )
{
    m_value.revert();
    if( m_drawing ) cancelDraw();
    QStringList marks;
    if( m_walk.active() )
    {
        if( kind == NetworkWalk::stepWire )              // Several wires (specs/wire-colors.md):
        {                                                // the current wire is marked first,
            marks = m_marked;                            // then the clicked one toggles
            NetworkWalk::Step cur = m_walk.current();
            if( marks.isEmpty() && cur.kind == NetworkWalk::stepWire ) marks.append( cur.uid );
            if( marks.contains( uid ) ) marks.removeAll( uid );
            else                        marks.append( uid );
        }
        else marks = m_marked;                           // A corner or dot: the marks stay
        leaveNetwork( false );
    }
    NetworkWalk::Step seed{ (NetworkWalk::kind_t)kind, uid, index, pos, 0 };
    if( !m_walk.startFrom( seed ) ) return;
    qDebug() << "[kbd] network mode from a click on the" << NetworkWalk::kindName( seed.kind ) << uid;
    m_arrowBurst = false;
    m_marked = marks;
    showStep( false );
    if( CircuitView* v = CircuitView::self() ) v->setFocus( Qt::MouseFocusReason );   // Keys work at once
}

bool KeyboardController::networkKey( QKeyEvent* event, Qt::KeyboardModifiers mods )
{
    int key = event->key();
    QString text = event->text();
    bool plain = mods == Qt::NoModifier;
    bool arrow = key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Up || key == Qt::Key_Down;

    if( text == "=" && ( plain || mods == Qt::ShiftModifier ) ){ leaveNetwork( true ); return true; }
    if( key == Qt::Key_Escape && plain ){ leaveNetwork( true ); return true; }
    if( text == "h" && plain ){ setPanMode( true ); return true; }   // Panning keeps the walk

    if( key == Qt::Key_Backtab || ( key == Qt::Key_Tab && ( plain || mods == Qt::ShiftModifier ) ) )
    {
        m_arrowBurst = false;
        m_walk.stepBy( ( key == Qt::Key_Tab && plain ) ? 1 : -1 );
        showStep();
        return true;
    }
    if( arrow && !( mods & ( Qt::AltModifier | Qt::MetaModifier ) ) )   // Plain, Cmd, (Shift)
    {
        if( m_panMode ) return arrowKey( event, mods );
        NetworkWalk::Step s = m_walk.current();
        if( s.kind == NetworkWalk::stepWire ) return true;          // Arrows do nothing on a wire
        QPointF dir;
        if     ( key == Qt::Key_Left  ) dir = QPointF(-1, 0 );
        else if( key == Qt::Key_Right ) dir = QPointF( 1, 0 );
        else if( key == Qt::Key_Up    ) dir = QPointF( 0,-1 );
        else                            dir = QPointF( 0, 1 );
        QPointF d = dir*( ( mods & Qt::ControlModifier ) ? bigStep : smallStep );   // 8 / 64 px (D23)
        if( s.kind == NetworkWalk::stepCorner ) moveCorner( d );
        else                                                        // A dot: as a drag, wires follow
        {
            m_burstWire.clear();
            arrowKey( event, mods );
            Node* node = m_walk.dot( s );
            QPoint at = node ? node->getPin( 0 )->scenePos().toPoint() : s.pos;
            if( !m_walk.rebuild( at, NetworkWalk::stepDot ) ){ leaveNetwork( true ); return true; }
            showStep();
        }
        return true;
    }
    if( ( key == Qt::Key_Backspace || key == Qt::Key_Delete ) && plain )
    {
        if( !m_marked.isEmpty() || m_walk.current().kind == NetworkWalk::stepWire ) deleteWire();
        return true;                                    // Dots and corners: nothing
    }
    // Several wires (specs/wire-colors.md): Enter marks, * the net, Space the colours
    if( ( key == Qt::Key_Return || key == Qt::Key_Enter ) && plain )
    {
        NetworkWalk::Step s = m_walk.current();
        if( s.kind != NetworkWalk::stepWire ){ say("Keyboard: Enter marks wires — this is a "+NetworkWalk::kindName( s.kind ) ); return true; }
        toggleMark( s.uid );
        return true;
    }
    if( text == "*" && ( plain || mods == Qt::ShiftModifier ) ) return markNet();
    if( key == Qt::Key_Space && plain )
    {
        QList<Connector*> wires = wireSelection();
        if( wires.isEmpty() ) say("Keyboard: no wire selected — Tab to a wire or mark some with Enter");
        else editWireStyle( wires );
        return true;
    }
    return false;
}

// ----- Several wires (specs/wire-colors.md) -----------------------------------

void KeyboardController::toggleMark( QString uid )
{
    if( m_marked.contains( uid ) ) m_marked.removeAll( uid );
    else                           m_marked.append( uid );
    qDebug() << "[kbd] marked wires" << m_marked;
    showStep( false );
    Circuit::self()->update();
}

// The walk's wires without instrument leads (specs/wire-colors.md, W5): a wire on a
// scope's, meter's or probe's input (Connector::leadPin, through tunnels too), and
// every wire of a branch that only leads to instruments: a dot whose wires are all
// leads but one makes that one a lead too (two scope channels joined, then to the net).
// A net of leads only: all its wires
static QStringList netWires( NetworkWalk& walk )
{
    QList<Connector*> wires;
    for( QString uid : walk.wireUids() )
        if( Connector* con = NetworkWalk::wire( NetworkWalk::Step{ NetworkWalk::stepWire, uid, -1, QPoint(), 0 } ) )
            wires.append( con );
    QSet<Connector*> leads;
    for( Connector* con : wires ) if( con->leadPin() ) leads.insert( con );

    for( bool changed = !leads.isEmpty(); changed; )
    {
        changed = false;
        for( Connector* con : wires )
            for( Pin* end : { con->startPin(), con->endPin() } )
            {
                Node* dot = end ? dynamic_cast<Node*>( end->component() ) : nullptr;
                if( !dot ) continue;
                QList<Connector*> others;
                for( Pin* p : dot->getPins() )
                    if( p && p->connector() && !leads.contains( p->connector() ) ) others.append( p->connector() );
                if( others.size() == 1 && !leads.contains( others.first() ) )
                {
                    leads.insert( others.first() );
                    changed = true;
                }
            }
    }
    QStringList uids;
    for( Connector* con : wires ) if( !leads.contains( con ) ) uids.append( con->getUid() );
    if( uids.isEmpty() ) for( Connector* con : wires ) uids.append( con->getUid() );
    return uids;
}

bool KeyboardController::markNet()
{
    QStringList net = netWires( m_walk );
    bool all = !net.isEmpty();
    for( QString uid : net ) if( !m_marked.contains( uid ) ){ all = false; break; }
    if( all ) for( QString uid : net ) m_marked.removeAll( uid );
    else      for( QString uid : net ) if( !m_marked.contains( uid ) ) m_marked.append( uid );
    qDebug() << "[kbd] net" << ( all ? "unmarked:" : "marked:" ) << m_marked.size() << "wires marked";
    showStep( false );
    Circuit::self()->update();
    return true;
}

void KeyboardController::pruneMarks()
{
    QStringList keep;
    for( QString uid : m_marked )
        if( NetworkWalk::wire( NetworkWalk::Step{ NetworkWalk::stepWire, uid, -1, QPoint(), 0 } ) ) keep.append( uid );
    m_marked = keep;
}

void KeyboardController::netFromMouse( QString uid )
{
    if( !m_walk.active() || m_walk.wireUids().indexOf( uid ) < 0 ) return;   // The press started it
    for( QString w : netWires( m_walk ) ) if( !m_marked.contains( w ) ) m_marked.append( w );
    qDebug() << "[kbd] net of" << uid << "marked:" << m_marked.size() << "wires";
    showStep( false );
    Circuit::self()->update();
}

QList<Connector*> KeyboardController::wireSelection()
{
    QList<Connector*> wires;
    if( !m_walk.active() ) return wires;
    pruneMarks();
    if( m_marked.isEmpty() )
    {
        if( m_walk.size() )
            if( Connector* con = m_walk.wire( m_walk.current() ) )
                if( m_walk.current().kind == NetworkWalk::stepWire ) wires.append( con );
        return wires;
    }
    for( QString uid : m_marked )
        if( Connector* con = NetworkWalk::wire( NetworkWalk::Step{ NetworkWalk::stepWire, uid, -1, QPoint(), 0 } ) )
            wires.append( con );
    return wires;
}

// The dialog shows the wires as they will look: no selection grey, no highlight
void KeyboardController::editWireStyle( QList<Connector*> wires )
{
    if( wires.isEmpty() ) return;
    Circuit* circ = Circuit::self();
    QList<QGraphicsItem*> selected = circ->selectedItems();
    m_styling = true;
    m_walkSelecting = true;
    circ->clearSelection();
    m_walkSelecting = false;
    circ->update();
    WireStyleDialog::edit( wires, CircuitView::self() );
    m_styling = false;
    m_walkSelecting = true;
    for( QGraphicsItem* item : selected ) if( item->scene() == circ ) item->setSelected( true );
    m_walkSelecting = false;
    circ->update();
    if( CircuitView* v = CircuitView::self() ) v->setFocus( Qt::OtherFocusReason );
}

void KeyboardController::wireStyleFromMenu( Connector* clicked )
{
    QList<Connector*> wires = wireSelection();
    if( !wires.contains( clicked ) ) wires = { clicked };
    editWireStyle( wires );
}

// A corner moves with its two segments, the wire staying H/V; a burst of arrow
// presses on the same wire is one undo step (as a mouse drag of a segment)
void KeyboardController::moveCorner( QPointF delta )
{
    NetworkWalk::Step s = m_walk.current();
    Connector* con = m_walk.wire( s );
    QStringList pl = m_walk.cornerMoved( s, delta.toPoint() );
    if( !con || pl.isEmpty() ) return;

    Circuit* circ = Circuit::self();
    bool newStep = !m_arrowBurst || m_burstEdit != circ->editCount() || m_burstWire != s.uid;
    if( newStep ) circ->saveCompChange( s.uid, "pointList", con->pointList().join(",") );
    markCorner( false );
    con->setPointList( pl );
    circ->setChanged();
    m_arrowBurst = true;
    m_burstEdit  = circ->editCount();
    m_burstWire  = s.uid;
    m_burstSelection.clear();

    QPoint at = s.pos+delta.toPoint();
    if( !m_walk.rebuild( at, NetworkWalk::stepCorner ) ){ leaveNetwork( true ); return; }
    showStep();
}

// Backspace on a wire: delete it (one undo step; a dot left with two wires is
// dissolved, as the mouse Remove does). The walk is rebuilt and goes to the nearest
// remaining step, or back to parts mode when none is left
void KeyboardController::deleteWire()
{
    NetworkWalk::Step s = m_walk.current();
    QList<Connector*> wires = wireSelection();          // The marked wires, or the current one
    if( wires.isEmpty() ) return;
    Circuit* circ = Circuit::self();
    m_walkSelecting = true;
    circ->clearSelection();
    for( Connector* con : wires ) con->setSelected( true );
    circ->removeItems();                                 // One undo step
    m_walkSelecting = false;
    m_arrowBurst = false;
    m_marked.clear();
    qDebug() << "[kbd] deleted" << wires.size() << "wire(s), current" << s.uid;
    if( !m_walk.rebuild( s.pos, NetworkWalk::stepWire ) )
    {
        say( m_walk.pinId().isEmpty() ? "Keyboard: no wire left — back to parts mode"
                                      : "Keyboard: no wire left on the pin — back to parts mode" );
        leaveNetwork( true );
        return;
    }
    showStep();
}

// ----- Drawing a wire from a wire (K9b) ---------------------------------------

// The straight wire segment nearest to p within 8 px (its pins' ends included in the
// range), skipping the wire `skip`. *at: p brought onto the segment
static ConnectorLine* lineNear( QPointF p, Connector* skip, QPoint* at )
{
    ConnectorLine* best = nullptr;
    double bestD = 8.5;
    for( Connector* con : *Circuit::self()->conList() )
    {
        if( con == skip ) continue;
        for( ConnectorLine* l : *con->lineList() )
        {
            if( !l->isVisible() ) continue;
            QPoint a = l->p1(), b = l->p2();
            QPoint q;
            if( a.y() == b.y() && a.x() != b.x() )       // Horizontal
            {
                if( p.x() < qMin( a.x(), b.x() ) || p.x() > qMax( a.x(), b.x() ) ) continue;
                q = QPoint( qRound( p.x() ), a.y() );
            }
            else if( a.x() == b.x() && a.y() != b.y() )  // Vertical
            {
                if( p.y() < qMin( a.y(), b.y() ) || p.y() > qMax( a.y(), b.y() ) ) continue;
                q = QPoint( a.x(), qRound( p.y() ) );
            }
            else continue;
            double d = qAbs( q.x()-p.x() )+qAbs( q.y()-p.y() );
            if( d < bestD ){ bestD = d; best = l; *at = q; }
        }
    }
    return best;
}

// Where a dot goes on line for the point q, as ConnectorLine::connectToWire picks it:
// a corner within 8 px, else q. false: within 8 px of the wire's own end (a pin or a
// dot), where no dot can go
static bool joinPoint( ConnectorLine* l, QPoint q, QPoint* at )
{
    QList<ConnectorLine*>* lines = l->connector()->lineList();
    int i = lines->indexOf( l ), n = lines->size();
    bool horiz = l->dy() == 0;
    auto near = [&]( QPoint c ){ return horiz ? qAbs( q.x()-c.x() ) < 8 : qAbs( q.y()-c.y() ) < 8; };
    if( near( l->p2() ) ){ if( i == n-1 ) return false; *at = l->p2(); return true; }
    if( near( l->p1() ) ){ if( i == 0 )   return false; *at = l->p1(); return true; }
    *at = q;
    return true;
}

// +: a dot on the wire under the cross (within 8 px) and a new wire from it, drawn
// with the arrows. One undo step with its end (Esc cancels both)
bool KeyboardController::plusKey()
{
    if( !cursorVisible() )
    {
        say("Keyboard: + draws a wire from the wire under the cross — unselect first (Esc)");
        return true;
    }
    QPoint q, at;
    ConnectorLine* line = lineNear( cursorPos(), nullptr, &q );
    if( !line ){ say("Keyboard: no wire under the cross (8 px) to draw from"); return true; }
    if( !joinPoint( line, q, &at ) )
    {
        say("Keyboard: too close to the end of the wire for a dot — move the cross along it");
        return true;
    }
    Circuit* circ = Circuit::self();
    bool vertical = line->dx() == 0;
    line->connectToWire( at );                           // A dot, a new wire started there
    Connector* con = circ->getNewConnector();
    if( !circ->is_constarted() || !con || !con->startPin() ) return true;

    m_drawing = true;
    m_drawSrcVertical = vertical;
    QPoint start = con->startPin()->scenePos().toPoint();
    m_drawPts = { start };
    m_drawMarks.clear();
    parkCursor( start, false );
    qDebug() << "[kbd] drawing a wire from" << start;
    updateDraw();
    return true;
}

// The fixed points, then an H/V L to end: its first leg perpendicular to the last
// segment (to the wire it started on); with endSide (a pin), the L that does not
// enter the pin from behind
QList<QPoint> KeyboardController::drawRoute( QPoint end, QString endSide )
{
    QList<QPoint> pts = m_drawPts;
    QPoint a = pts.last();
    if( end == a ) return pts;
    if( a.x() != end.x() && a.y() != end.y() )
    {
        bool lastVertical = pts.size() >= 2 ? pts.at( pts.size()-2 ).x() == a.x() : m_drawSrcVertical;
        bool vertical = !lastVertical;                   // First leg: turn off the last one
        QPoint k = vertical ? QPoint( a.x(), end.y() ) : QPoint( end.x(), a.y() );
        if( !endSide.isEmpty() && Circuit::backwards( endSide, k-end ) )
            k = vertical ? QPoint( end.x(), a.y() ) : QPoint( a.x(), end.y() );
        pts.append( k );
    }
    pts.append( end );
    return pts;
}

void KeyboardController::updateDraw()
{
    Connector* con = Circuit::self()->getNewConnector();
    if( !con ) return;
    QList<QPoint> pts = drawRoute( cursorPos().toPoint(), "" );
    if( pts.size() < 2 ) pts.append( pts.last() );
    QStringList l;
    for( QPoint p : pts ){ l.append( QString::number( p.x() ) ); l.append( QString::number( p.y() ) ); }
    con->setPointList( l );
}

bool KeyboardController::drawKey( QKeyEvent* event, Qt::KeyboardModifiers mods )
{
    int key = event->key();
    bool plain = mods == Qt::NoModifier;
    bool arrow = key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Up || key == Qt::Key_Down;

    if( arrow && !( mods & ( Qt::AltModifier | Qt::MetaModifier ) ) )   // The cross moves 8 px
    {                                                       // (Cmd: 64), the wire follows (not snapped: it keeps the grid
        QPointF dir;                                        // of the dot it started on)
        if     ( key == Qt::Key_Left  ) dir = QPointF(-1, 0 );
        else if( key == Qt::Key_Right ) dir = QPointF( 1, 0 );
        else if( key == Qt::Key_Up    ) dir = QPointF( 0,-1 );
        else                            dir = QPointF( 0, 1 );
        parkCursor( cursorPos()+dir*( ( mods & Qt::ControlModifier ) ? bigStep : smallStep ), false );
        if( CircuitView* v = CircuitView::self() ) v->ensureVisible( crossRect( m_cursor ), 32, 32 );
        updateDraw();
        return true;
    }
    if( event->text() == "=" && ( plain || mods == Qt::ShiftModifier ) )   // A corner here
    {
        QList<QPoint> pts = drawRoute( cursorPos().toPoint(), "" );
        if( pts.size() > m_drawPts.size() ){ m_drawMarks.append( m_drawPts.size() ); m_drawPts = pts; }
        return true;
    }
    if( key == Qt::Key_Backspace && plain )                 // Undo the last =
    {
        if( !m_drawMarks.isEmpty() ){ while( m_drawPts.size() > m_drawMarks.last() ) m_drawPts.removeLast();
                                      m_drawMarks.removeLast(); updateDraw(); }
        return true;
    }
    if( ( key == Qt::Key_Return || key == Qt::Key_Enter ) && plain ){ finishDraw(); return true; }
    if( key == Qt::Key_Escape && plain ){ cancelDraw(); return true; }
    return true;                                            // Nothing else while drawing
}

// Enter: connect to a free pin within 8 px of the cross, else to a wire within 8 px
// (a dot there). Nothing near: a message, still drawing
void KeyboardController::finishDraw()
{
    Circuit* circ = Circuit::self();
    Connector* con = circ->getNewConnector();
    if( !con ) { m_drawing = false; return; }
    QPointF c = cursorPos();

    Pin* pin = nullptr;                                     // A free pin first
    double best = 8.5;
    for( Component* comp : *circ->compList() )
    {
        if( comp->itemType() == "Node" || !comp->isVisible() ) continue;
        for( Pin* p : comp->getPins() )
        {
            if( !p || p->connector() || !p->isVisible() ) continue;
            QPointF d = p->scenePos()-c;
            double dd = qMax( qAbs( d.x() ), qAbs( d.y() ) );
            if( dd < best ){ best = dd; pin = p; }
        }
    }
    QPoint end;
    ConnectorLine* line = nullptr;
    if( pin ) end = pin->scenePos().toPoint();
    else
    {
        QPoint q;
        line = lineNear( c, con, &q );
        if( !line || !joinPoint( line, q, &end ) )
        {
            say("Keyboard: nothing to connect to at the cross — move it onto a pin or a wire (Esc cancels)");
            return;
        }
    }
    if( end == m_drawPts.first() ){ say("Keyboard: the wire would end where it starts"); return; }

    QList<QPoint> pts = drawRoute( end, pin ? pin->side() : QString() );
    QStringList l;
    for( QPoint p : pts ){ l.append( QString::number( p.x() ) ); l.append( QString::number( p.y() ) ); }
    con->setPointList( l );                                 // The route first: it is saved with the step
    m_drawing = false;
    m_drawPts.clear();
    m_drawMarks.clear();
    if( pin ) circ->closeconnector( pin, true );
    else      line->connectToWire( end );                   // Ends on a new dot there
    parkCursor( end, false );
    qDebug() << "[kbd] wire drawn to" << end << ( pin ? pin->pinId() : QString("a wire") );
}

void KeyboardController::cancelDraw()
{
    if( !m_drawing ) return;
    m_drawing = false;
    m_drawPts.clear();
    m_drawMarks.clear();
    Circuit::self()->deleteNewConnector();                  // The dot and the split go too
    updateCross( m_cursor );
    qDebug() << "[kbd] wire drawing cancelled";
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

bool KeyboardController::cursorVisible()
{
    if( !m_focused ) return false;
    if( m_drawing ) return true;                         // K9b: the cross is the wire's end
    if( m_rect ) return true;                            // The rectangle's moving corner
    return !anyPartSelected() && !m_walk.active();
}

void KeyboardController::setPanMode( bool on )
{
    if( on == m_panMode ) return;
    m_panMode = on;
    applyPointer();
    qDebug() << "[kbd] pan mode" << on;
}

// Cmd + / Cmd = : zoom in, Cmd - : zoom out, Cmd 0 : 100 %, Opt Cmd 0 : zoom to fit,
// Ctrl Cmd 0 : zoom to the selection (the toolbar's zoom buttons). On AZERTY 0 needs
// Shift and Option changes the character: 0 is also matched by its physical key
bool KeyboardController::zoomKey( QKeyEvent* event, Qt::KeyboardModifiers mods )
{
    if( !( mods & Qt::ControlModifier ) ) return false;
    int key = event->key();
    bool zero0 = key == Qt::Key_0 || PhysicalKeys::numberRowDigit( event ) == 0;
    Qt::KeyboardModifiers extra = mods & ~( Qt::ControlModifier | Qt::ShiftModifier );
    if( zero0 && ( extra == Qt::AltModifier || extra == Qt::MetaModifier ) )
    {
        CircuitView* v = CircuitView::self();
        if( !v ) return true;
        if( extra == Qt::AltModifier ) v->zoomToFit();
        else                           v->zoomSelected();
        return true;
    }
    if( mods & ( Qt::AltModifier | Qt::MetaModifier ) ) return false;
    if( key == Qt::Key_Plus || key == Qt::Key_Equal ){ zoomAround( 1.25 );     return true; }
    if( key == Qt::Key_Minus )                       { zoomAround( 1/1.25 );   return true; }
    bool zero = key == Qt::Key_0 || PhysicalKeys::numberRowDigit( event ) == 0;  // AZERTY: Cmd+à
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
    if( m_walk.active() && !m_walkSelecting ) leaveNetwork( false );  // Selected by other means
    if( m_value.active() )                               // The edit ends when its part is no
    {                                                    // longer the single selection (K7)
        Component* c = singlePart();
        if( !c || c->getUid() != m_value.uid() ) m_value.revert();
    }
    if( m_cursorSet && !anyPartSelected() && m_cursor != snap( m_cursor ) ) m_cursor = snap( m_cursor );
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
    if( b && !m_rect ) parkCursor( b->pos() );   // Whatever selects one part — Tab, a click,
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

// Pan mode (h): the arrows pan the canvas (Shift: small steps). Otherwise the arrows move
// 8 px, Cmd+arrows 64 px (D23): the cross when nothing is selected, else the selection
// with the wires attached, as a mouse drag does (Shift changes nothing there)
bool KeyboardController::arrowKey( QKeyEvent* event, Qt::KeyboardModifiers mods )
{
    int key = event->key();
    QPointF dir;
    if     ( key == Qt::Key_Left  ) dir = QPointF(-1, 0 );
    else if( key == Qt::Key_Right ) dir = QPointF( 1, 0 );
    else if( key == Qt::Key_Up    ) dir = QPointF( 0,-1 );
    else                            dir = QPointF( 0, 1 );

    int step = ( mods & Qt::ShiftModifier ) ? smallStep : bigStep;      // Pan mode
    if( !m_panMode ) step = ( mods & Qt::ControlModifier ) ? bigStep : smallStep;
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
    circ->moveSelection( d, newStep );         // Also moves the (hidden) cross along
    m_arrowBurst     = true;
    m_burstEdit      = circ->editCount();
    m_burstSelection = sel;

    refreshAutoPins();
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

void KeyboardController::mousePressed( bool keepWalk )
{
    m_value.revert();                                    // A click reverts (K7)
    if( m_rect ) endRect( true );                        // and ends a selection rectangle
    if( m_drawing ) cancelDraw();                        // and cancels a wire being drawn (K9b)
    setPanMode( false );
    if( m_walk.active() && !keepWalk ) leaveNetwork( false );   // and ends network mode (K9);
}   // Cmd+click, right click: walkFromMouse(), the menu or the selection change decides

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
    if( !focused ){ m_panMode = false; showPointer(); if( m_rect ) endRect( true ); }
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

// The hidden cross follows a moved selection (arrows, a mouse drag, the rotation
// alignment), so Esc shows it where the parts ended up. One part: its position, snapped;
// several: shifted as they are, snapped only when shown (no drift from 4 px drag steps).
// The view never scrolls for the hidden cross: that made a mouse drag run away
void KeyboardController::selectionMoved( QPointF delta )
{
    if( Component* comp = singlePart() ) parkCursor( comp->pos() );
    else                                 parkCursor( cursorPos()+delta, false );
}

void KeyboardController::parkCursor( QPointF scenePos, bool snapIt )
{
    QPointF old = cursorPos();
    m_cursor = snapIt ? snap( scenePos ) : scenePos;
    updateCross( old );
    updateCross( m_cursor );
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
    CircuitView* v = CircuitView::self();
    if( !v || painter->device() != v->viewport() ) return;  // Not in exports and captures of the circuit

    if( m_walk.active() && m_walk.size() && !m_styling ) // The walk's step (K9), in highlight:
    {                                                    // a wire traced, a ring round a dot,
        NetworkWalk::Step s = m_walk.current();          // a small square on a corner
        painter->save();
        painter->setRenderHint( QPainter::Antialiasing, true );
        QPen solid( Theme::highlight(), 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin );
        painter->setPen( solid );
        painter->setBrush( Qt::NoBrush );
        auto trace = []( Connector* con ){
            QPolygonF poly;
            for( ConnectorLine* l : *con->lineList() )
            {
                if( poly.isEmpty() ) poly << l->p1();
                poly << l->p2();
            }
            return poly;
        };
        for( QString uid : m_marked )                    // Marked wires (specs/wire-colors.md)
            if( Connector* con = NetworkWalk::wire( NetworkWalk::Step{ NetworkWalk::stepWire, uid, -1, QPoint(), 0 } ) )
                painter->drawPolyline( trace( con ) );
        if( s.kind == NetworkWalk::stepWire )
        {
            if( Connector* con = m_walk.wire( s ) )
            {
                if( !m_marked.isEmpty() )                // With marks: the current one dashed
                {
                    QPen dash( Theme::highlight(), 1.2, Qt::DashLine, Qt::FlatCap, Qt::RoundJoin );
                    dash.setDashPattern( { 3, 2 } );
                    painter->setPen( m_marked.contains( s.uid ) ? QPen( Theme::canvas(), 1.0, Qt::DashLine ) : dash );
                }
                painter->drawPolyline( trace( con ) );
            }
        }
        else if( s.kind == NetworkWalk::stepDot ) painter->drawEllipse( QPointF( s.pos ), 5, 5 );
        else{
            painter->setPen( QPen( Theme::highlight(), 1.5 ) );
            painter->drawRect( QRectF( s.pos.x()-3.5, s.pos.y()-3.5, 7, 7 ) );
        }
        painter->restore();
    }
    if( m_rect )                                         // The selection rectangle, dashed
    {
        painter->save();
        QPen pen( Theme::highlight(), 1, Qt::DashLine );
        pen.setCosmetic( true );
        painter->setPen( pen );
        painter->setBrush( Qt::NoBrush );
        painter->drawRect( QRectF( m_rectAnchor, cursorPos() ).normalized() );
        painter->restore();
    }
    if( !cursorVisible() ) return;

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
    if( m_rect ){
        QRectF r = QRectF( m_rectAnchor, cursorPos() ).normalized();
        o["rect"] = QJsonArray{ r.x(), r.y(), r.width(), r.height() };
    }
    if( CircuitView* v = CircuitView::self() ) o["zoom"] = v->transform().m11();

    QJsonArray sel;
    if( Circuit* c = Circuit::self() )
        for( Component* comp : *c->compList() )
            if( comp->isSelected() ) sel.append( comp->getUid() );
    o["selection"] = sel;
    Component* lp = lastPart();
    o["lastPart"] = lp ? QJsonValue( lp->getUid() ) : QJsonValue();
    QString libType, libName;                             // K9e: the part selected in the list
    if( ComponentSelector::self() && ComponentSelector::self()->selectedPart( &libType, &libName ) )
        o["libraryPart"] = QJsonObject{ {"type", libType}, {"name", libName} };
    bool groupOpen = false;
    QString group = ComponentSelector::self() ? ComponentSelector::self()->selectedGroup( &groupOpen ) : QString();
    if( !group.isEmpty() ) o["libraryGroup"] = QJsonObject{ {"name", group}, {"expanded", groupOpen} };
    if( m_value.active() ) o["valueEdit"] = m_value.state();   // K7: the value being typed
    if( m_drawing )                                       // K9b: the wire being drawn
    {
        QJsonArray fixed;
        for( QPoint p : m_drawPts ) fixed.append( QJsonArray{ p.x(), p.y() } );
        QJsonObject d{ {"fixed", fixed} };
        if( Connector* con = Circuit::self()->getNewConnector() )
        {
            d["wire"] = con->getUid();
            QJsonArray pts;
            for( QString v : con->pointList() ) pts.append( v.toInt() );
            d["points"] = pts;
        }
        o["drawing"] = d;
    }
    if( m_walk.active() )                                 // K9: the walk, its step and what
    {                                                     // is highlighted in the scene
        QJsonObject n = m_walk.state();
        QJsonArray wires, dots;
        for( QGraphicsItem* item : Circuit::self()->selectedItems() )
        {
            if( item->type() == QGraphicsItem::UserType+2 )
            {
                QString uid = qgraphicsitem_cast<ConnectorLine*>( item )->connector()->getUid();
                if( !wires.contains( uid ) ) wires.append( uid );
            }
            else if( Component* c = qgraphicsitem_cast<Component*>( item ) )
                if( c->itemType() == "Node" ) dots.append( c->getUid() );
        }
        n["selectedWires"] = wires;
        n["marked"] = QJsonArray::fromStringList( m_marked );
        n["selectedDots"]  = dots;
        o["network"] = n;
    }
    return o;      // The CLI adds the pins (pinFrom, pinTo, selectedPin) as "R1.lPin" refs
}

// D23: Shift+arrows from the cross (nothing selected) draw a selection rectangle, as a
// mouse drag on empty canvas does: one corner stays where it started, the cross is the
// other (8 px steps, Cmd: 64 px). Every part it touches is selected while it grows.
// Releasing Shift (or any other key, a click) ends it, the parts staying selected; Esc
// cancels it (nothing selected, the cross where it is)
bool KeyboardController::rectKey( QKeyEvent* event, Qt::KeyboardModifiers mods )
{
    int key = event->key();
    bool arrow = key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Up || key == Qt::Key_Down;
    if( !arrow || !( mods & Qt::ShiftModifier ) || ( mods & ( Qt::AltModifier | Qt::MetaModifier ) ) ) return false;
    QPointF dir;
    if     ( key == Qt::Key_Left  ) dir = QPointF(-1, 0 );
    else if( key == Qt::Key_Right ) dir = QPointF( 1, 0 );
    else if( key == Qt::Key_Up    ) dir = QPointF( 0,-1 );
    else                            dir = QPointF( 0, 1 );
    moveCursorTo( cursorPos()+dir*( ( mods & Qt::ControlModifier ) ? bigStep : smallStep ) );
    rectSelect();
    return true;
}

void KeyboardController::rectSelect()
{
    Circuit* circ = Circuit::self();
    if( !circ ) return;
    QRectF r = QRectF( m_rectAnchor, cursorPos() ).normalized();
    QSet<Component*> in;
    if( r.width() > 0 || r.height() > 0 )
        for( QGraphicsItem* it : circ->items( r, Qt::IntersectsItemShape ) )
        {
            Component* c = dynamic_cast<Component*>( it );
            if( c && c->itemType() != "Node" && c->isVisible() && !c->parentItem() ) in.insert( c );
        }
    for( Component* c : *circ->compList() )             // Parts only: wires follow when moved
        if( c->isSelected() != in.contains( c ) ) c->setSelected( in.contains( c ) );
    for( QGraphicsItem* it : circ->selectedItems() )     // Nothing else (a wire, a dot)
        if( !dynamic_cast<Component*>( it ) || static_cast<Component*>( it )->itemType() == "Node" )
            it->setSelected( false );

    QRectF shown = r.adjusted( -2, -2, 2, 2 );           // Repaint the old and new outline
    circ->update( m_rectShown.united( shown ) );
    m_rectShown = shown;
}

void KeyboardController::endRect( bool keep )
{
    if( !m_rect ) return;
    m_rect = false;
    Circuit* circ = Circuit::self();
    if( circ ) circ->update( m_rectShown );
    m_rectShown = QRectF();
    if( !keep && circ ) circ->clearSelection();
    if( circ ) updateCross( cursorPos() );
    qDebug() << "[kbd] selection rectangle" << ( keep ? "done" : "cancelled" )
             << ( circ ? circ->selectedItems().size() : 0 ) << "items";
}

void KeyboardController::keyRelease( QKeyEvent* event )
{
    if( m_rect && event->key() == Qt::Key_Shift ) endRect( true );
}
