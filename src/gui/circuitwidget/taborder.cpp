/***************************************************************************
 *   Tab order of the parts (specs/parts-pane.md §12)                      *
 ***( see copyright.txt file at root folder )*******************************/

#include <QCoreApplication>
#include <QDebug>
#include <QHash>
#include <QMenu>
#include <QKeySequence>
#include <algorithm>

#include "taborder.h"
#include "circuit.h"
#include "component.h"
#include "keyboardcontroller.h"

#define tr(str) QCoreApplication::translate("TabOrder",str)

QList<Component*> TabOrder::walk()
{
    QList<Component*> reading = KeyboardController::self()->readingOrder();   // Unlocked, visible
    QHash<Component*, int> pos;
    for( int i=0; i<reading.size(); ++i ) pos[reading.at( i )] = i;
    QList<Component*> out = reading;
    std::stable_sort( out.begin(), out.end(), [&pos]( Component* a, Component* b ){
        int ia = a->tabIndex(), ib = b->tabIndex();
        if( ( ia > 0 ) != ( ib > 0 ) ) return ia > 0;      // Indexed parts first
        if( ia != ib ) return ia < ib;
        return pos.value( a ) < pos.value( b );
    });
    return out;
}

bool TabOrder::custom()
{
    if( !Circuit::self() ) return false;
    for( Component* c : *Circuit::self()->compList() ) if( c->tabIndex() > 0 ) return true;
    return false;
}

QList<Component*> TabOrder::inWalk( QList<Component*> parts, QList<Component*> walk, QStringList* left )
{
    QList<Component*> out;
    for( Component* c : parts )
    {
        if( !c || out.contains( c ) ) continue;
        if( walk.contains( c ) ) out << c;
        else if( left ) *left << c->idLabel();             // Locked or hidden: not in the walk
    }
    return out;
}

QString TabOrder::apply( QList<Component*> order, QString what )
{
    Circuit* circ = Circuit::self();
    QList<Component*> todo;
    for( int i=0; i<order.size(); ++i ) if( order.at( i )->tabIndex() != i+1 ) todo << order.at( i );
    if( todo.isEmpty() ){
        QString msg = tr("Tab order unchanged");
        qInfo().noquote() << msg;
        return msg;
    }
    circ->beginUndoStep();
    QList<QPair<QString, QString>> old;
    for( int i=0; i<order.size(); ++i )
    {
        Component* c = order.at( i );
        if( c->tabIndex() == i+1 ) continue;
        old.append( { c->getUid(), c->tabIndexStr() } );
        c->setTabIndex( i+1 );
    }
    circ->calcCircuitChanges();
    for( auto& o : old ) circ->addCompChange( o.first, "tabIndex", o.second );
    circ->endCircuitBatch();
    circ->setChanged();
    qInfo().noquote() << what;
    return what;
}

namespace {
QString placeText( QList<Component*> moved, QList<Component*> order )
{
    QStringList labels;
    for( Component* c : moved ) labels << c->idLabel();
    int first = order.indexOf( moved.first() )+1, last = order.indexOf( moved.last() )+1;
    QString at = first == last ? QString::number( first ) : QString("%1–%2").arg( first ).arg( last );
    return QCoreApplication::translate("TabOrder", "Tab order: %1 → %2").arg( labels.join(", ") ).arg( at );
}
QString noteLeft( QStringList left )
{
    if( left.isEmpty() ) return "";
    return " ("+QCoreApplication::translate("TabOrder", "not in the Tab walk, locked or hidden: %1").arg( left.join(", ") )+")";
}
}

QString TabOrder::asSelected( QList<Component*> inSelectionOrder )
{
    QList<Component*> w = walk();
    QStringList left;
    QList<Component*> sel = inWalk( inSelectionOrder, w, &left );
    if( sel.size() < 2 ){
        QString msg = tr("Tab order as selected: select two or more parts (in the order wanted)")+noteLeft( left );
        qInfo().noquote() << msg;
        return msg;
    }
    int at = w.size();
    for( Component* c : sel ) at = qMin( at, w.indexOf( c ) );   // Where the first of them was
    QList<Component*> rest;
    for( Component* c : w ) if( !sel.contains( c ) ) rest << c;
    int before = 0;                                          // Unselected parts before that place
    for( int i=0; i<at; ++i ) if( !sel.contains( w.at( i ) ) ) before++;
    QList<Component*> order = rest.mid( 0, before )+sel+rest.mid( before );
    return apply( order, placeText( sel, order )+noteLeft( left ) );
}

QString TabOrder::toFront( QList<Component*> parts )
{
    QList<Component*> w = walk();
    QStringList left;
    QList<Component*> sel;
    for( Component* c : w ) if( parts.contains( c ) ) sel << c;   // Their order among themselves kept
    inWalk( parts, w, &left );
    if( sel.isEmpty() ){ QString m = tr("Tab order: nothing selected in the walk")+noteLeft( left ); qInfo().noquote() << m; return m; }
    QList<Component*> order = sel;
    for( Component* c : w ) if( !sel.contains( c ) ) order << c;
    return apply( order, placeText( sel, order )+noteLeft( left ) );
}

QString TabOrder::toBack( QList<Component*> parts )
{
    QList<Component*> w = walk();
    QStringList left;
    QList<Component*> sel;
    for( Component* c : w ) if( parts.contains( c ) ) sel << c;
    inWalk( parts, w, &left );
    if( sel.isEmpty() ){ QString m = tr("Tab order: nothing selected in the walk")+noteLeft( left ); qInfo().noquote() << m; return m; }
    QList<Component*> order;
    for( Component* c : w ) if( !sel.contains( c ) ) order << c;
    order += sel;
    return apply( order, placeText( sel, order )+noteLeft( left ) );
}

QString TabOrder::moveTo( QList<Component*> parts, int place )
{
    QList<Component*> w = walk();
    QStringList left;
    QList<Component*> sel;
    for( Component* c : w ) if( parts.contains( c ) ) sel << c;
    inWalk( parts, w, &left );
    if( sel.isEmpty() ){ QString m = tr("Tab order: not in the walk (locked or hidden)")+noteLeft( left ); qInfo().noquote() << m; return m; }
    QList<Component*> rest;
    for( Component* c : w ) if( !sel.contains( c ) ) rest << c;
    int at = qBound( 0, place-1, rest.size() );
    QList<Component*> order = rest.mid( 0, at )+sel+rest.mid( at );
    return apply( order, placeText( sel, order )+noteLeft( left ) );
}

QString TabOrder::reset()
{
    Circuit* circ = Circuit::self();
    QList<Component*> todo;
    for( Component* c : *circ->compList() ) if( c->tabIndex() > 0 ) todo << c;
    if( todo.isEmpty() ){ QString m = tr("Tab order: already by position"); qInfo().noquote() << m; return m; }
    circ->beginUndoStep();
    QList<QPair<QString, QString>> old;
    for( Component* c : todo ){ old.append( { c->getUid(), c->tabIndexStr() } ); c->setTabIndex( 0 ); }
    circ->calcCircuitChanges();
    for( auto& o : old ) circ->addCompChange( o.first, "tabIndex", o.second );
    circ->endCircuitBatch();
    circ->setChanged();
    QString msg = tr("Tab order reset: by position (rows top to bottom, left to right)");
    qInfo().noquote() << msg;
    return msg;
}

void TabOrder::addMenu( QMenu* menu, Component* on )
{
    QMenu* sub = menu->addMenu( tr("Tab order") );
    auto parts = [on]() -> QList<Component*> {
        QList<Component*> sel = Circuit::self()->selectedParts();
        if( on && !sel.contains( on ) ) return { on };
        return sel;
    };
    auto keys = []( int k ){ return "\t"+QKeySequence( k ).toString( QKeySequence::NativeText ); };
    QAction* a = sub->addAction( tr("As selected")+keys( Qt::CTRL | Qt::SHIFT | Qt::Key_T ) );
    a->setToolTip( tr("The selected parts follow each other in the order they were selected") );
    a->setEnabled( parts().size() > 1 );
    QObject::connect( a, &QAction::triggered, [](){ asSelected( Circuit::self()->selectionOrder() ); } );
    a = sub->addAction( tr("To front")+keys( Qt::CTRL | Qt::ALT | Qt::Key_T ) );
    QObject::connect( a, &QAction::triggered, [parts](){ toFront( parts() ); } );
    a = sub->addAction( tr("To back")+keys( Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_T ) );
    QObject::connect( a, &QAction::triggered, [parts](){ toBack( parts() ); } );
    sub->addSeparator();
    a = sub->addAction( tr("Reset (by position)") );
    a->setEnabled( custom() );
    QObject::connect( a, &QAction::triggered, [](){ reset(); } );
}
