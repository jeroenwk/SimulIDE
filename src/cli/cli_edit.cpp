/***************************************************************************
 *   SimulIDE command line control interface                              *
 *   Editing commands: library, components, wires, undo/redo               *
 ***( see copyright.txt file at root folder )*******************************/

#include <QTreeWidgetItem>
#include <QRegularExpression>
#include <QSet>
#include <QDir>
#include <QFileInfo>
#include <QDebug>

#include "cliapi.h"
#include "clirefs.h"
#include "circuitwidget.h"
#include "circuit.h"
#include "component.h"
#include "connector.h"
#include "connectorline.h"
#include "node.h"
#include "pin.h"
#include "subcircuit.h"
#include "linker.h"
#include "comproperty.h"
#include "componentselector.h"
#include "simulator.h"
#include "utils.h"

namespace
{
struct CatalogItem { QString name, type, caption, category; };

// Every placeable item, as listed in the component selector (native items, MCUs, subcircuits)
QList<CatalogItem> catalog()
{
    QList<CatalogItem> list;
    ComponentSelector* sel = ComponentSelector::self();
    if( !sel ) return list;

    std::function<void(QTreeWidgetItem*, QString)> walk = [&]( QTreeWidgetItem* item, QString path )
    {
        QString type = item->data( 0, Qt::UserRole ).toString();
        QString name = item->data( 0, Qt::WhatsThisRole ).toString();
        if( !type.isEmpty() )
            list.append( { name, type, item->text( 0 ).trimmed(), path } );
        else{
            QString sub = path.isEmpty() ? name : path+"/"+name;
            for( int i=0; i<item->childCount(); ++i ) walk( item->child( i ), sub );
        }
    };
    for( int i=0; i<sel->topLevelItemCount(); ++i ) walk( sel->topLevelItem( i ), "" );
    return list;
}

CatalogItem findItem( QString name )
{
    QList<CatalogItem> items = catalog();
    for( CatalogItem& it : items ) if( it.name == name ) return it;
    for( CatalogItem& it : items ) if( it.name.compare( name, Qt::CaseInsensitive ) == 0 ) return it;
    for( CatalogItem& it : items ) if( it.caption.compare( name, Qt::CaseInsensitive ) == 0 ) return it;

    // A device in the open circuit's data/<name>/ folder (as SimulIDE loads them inside
    // subcircuits): <name>.mcu = scripted or MCU chip, <name>.sim1 + package = subcircuit
    QString circPath = Circuit::self() ? Circuit::self()->getFilePath() : QString();
    if( !circPath.isEmpty() )
    {
        QDir dir( QFileInfo( circPath ).absoluteDir().absoluteFilePath("data/"+name ) );
        if( dir.exists( name+".mcu" ) ) return { name, "MCU", name, "circuit data" };
        if( dir.exists( name+".sim1" ) && ( dir.exists( name+".package") || dir.exists( name+"_LS.package") ) )
            return { name, "Subcircuit", name, "circuit data" };
    }

    QStringList similar;
    for( CatalogItem& it : items )
        if( it.name.contains( name, Qt::CaseInsensitive ) && similar.size() < 8 ) similar.append( it.name );
    QString hint = similar.isEmpty() ? " (see: lib list)" : " (did you mean: "+similar.join(", ")+"?)";
    throw CliError("unknown component type: "+name+hint );
}

// Structural edits need the simulation off, as in the GUI
void stopSimForEdit()
{
    if( !Simulator::self()->isRunning() ) return;
    CircuitWidget::self()->powerCircOff();
    CliApi::self()->addMessage("simulation stopped to edit the circuit");
}

QPointF freeSpot()
{
    Circuit* circ = Circuit::self();
    if( circ->compList()->isEmpty() ) return QPointF( 0, 0 );
    QRectF r;
    for( Component* c : *circ->compList() ) r |= c->sceneBoundingRect();
    return toGrid( QPointF( r.right()+48, r.top()+16 ) );
}

bool hasProperty( Component* comp, QString name )
{
    for( propGroup& g : *comp->properties() )
        for( ComProperty* p : g.propList ) if( p->name() == name ) return true;
    return false;
}

QStringList visiblePropNames( Component* comp )
{
    QStringList names;
    for( propGroup& g : *comp->properties() ){
        if( g.flags & groupHidden ) continue;
        for( ComProperty* p : g.propList ) if( !(p->flags() & propHidden) ) names.append( p->name() );
    }
    return names;
}

// Property names are case sensitive in SimulIDE: accept any case from the CLI
QString propName( Component* comp, QString name )
{
    if( name == "label" || name == "id" ) return "label";
    for( propGroup& g : *comp->properties() )
        for( ComProperty* p : g.propList ) if( p->name().compare( name, Qt::CaseInsensitive ) == 0 ) return p->name();
    throw CliError("unknown property '"+name+"' for "+comp->getUid()+". Properties: "+visiblePropNames( comp ).join(", ") );
}

ComProperty* findProp( Component* comp, QString name )
{
    for( propGroup& g : *comp->properties() )
        for( ComProperty* p : g.propList ) if( p->name() == name ) return p;
    return nullptr;
}

// SimulIDE numeric properties take "<number> <multiplier><unit>" (e.g. "4.7 kΩ") and keep the
// previous multiplier if none is given. Accept engineering notation from the CLI instead:
// 1k, 4.7u, 10mA, 2.2 nF, 1e3, 0.5
QString normalizeValue( ComProperty* prop, QString val )
{
    if( !prop ) return val;
    QString type = prop->type();
    if( type != "double" && type != "int" ) return val;

    static const QString prefixes = "pnuµmkMGT";
    QString unit = prop->unit();
    QString base = unit;
    if( base.size() > 1 && prefixes.contains( base.at(0) ) ) base = base.mid( 1 );

    static const QRegularExpression re("^\\s*([-+]?[0-9]*\\.?[0-9]+(?:[eE][-+]?[0-9]+)?)\\s*([pnuµmkMGT]?)\\s*(.*?)\\s*$");
    QRegularExpressionMatch m = re.match( val );
    if( !m.hasMatch() ) throw CliError("invalid value for "+prop->name()+": "+val+" (examples: 470, 4.7k, 100n, 10m"+base+")");

    QString num    = m.captured( 1 );
    QString prefix = m.captured( 2 );
    QString rest   = m.captured( 3 );
    if( prefix == "u" ) prefix = "µ";

    if( !rest.isEmpty() && rest != base )
    {
        // "1m" could be a unit (e.g. meters): only accept exact base unit
        if( base.isEmpty() || rest.compare( base, Qt::CaseInsensitive ) != 0 )
            throw CliError("unit mismatch for "+prop->name()+": got '"+rest+"', expected '"+base+"'");
    }
    if( base.isEmpty() )            // Unitless: plain number
    {
        double mult = 1;
        if( !prefix.isEmpty() ) mult = QHash<QString,double>{{"p",1e-12},{"n",1e-9},{"µ",1e-6},{"m",1e-3},
                                                             {"k",1e3},{"M",1e6},{"G",1e9},{"T",1e12}}.value( prefix, 1 );
        double v = num.toDouble()*mult;
        return ( type == "int" ) ? QString::number( qRound64( v ) ) : QString::number( v, 'g', 15 );
    }
    return num+" "+prefix+base;
}

// Wire segment next to a pin, to attach a junction to
ConnectorLine* lineAt( Pin* pin, QPoint& point )
{
    Connector* con = pin->connector();
    QList<ConnectorLine*>* lines = con->lineList();
    QList<ConnectorLine*> order;
    if( con->startPin() == pin ) for( int i=0; i<lines->size(); ++i )   order.append( lines->at( i ) );
    else                         for( int i=lines->size()-1; i>=0; --i ) order.append( lines->at( i ) );

    for( ConnectorLine* line : order )
    {
        int len = qAbs( line->dx() ) + qAbs( line->dy() );
        if( len < 16 ) continue;                      // Too short to split cleanly
        if( line->dx() != 0 && line->dy() != 0 ) continue;   // Only straight segments
        point = toCompGrid( QPointF( ( line->p1()+line->p2() )/2 ) ).toPoint(); // On the 8 px grid
        return line;
    }
    throw CliError("no wire segment long enough near "+CliRefs::pinRef( pin )+" to add a junction: "
                   "move the components apart or connect to another pin of the same net");
}

// Wire segment passing through an explicit point ("@x,y"), to attach a junction to
ConnectorLine* lineThrough( QPoint pt )
{
    QList<ConnectorLine*> found;
    QStringList ends;
    for( Connector* con : *Circuit::self()->conList() )
    {
        QList<ConnectorLine*>* lines = con->lineList();
        for( int i=0; i<lines->size(); ++i )
        {
            ConnectorLine* line = lines->at( i );
            QPoint p1 = line->p1(), p2 = line->p2();
            bool on = ( p1.x() == p2.x() && pt.x() == p1.x() && pt.y() >= qMin( p1.y(), p2.y() ) && pt.y() <= qMax( p1.y(), p2.y() ) )
                   || ( p1.y() == p2.y() && pt.y() == p1.y() && pt.x() >= qMin( p1.x(), p2.x() ) && pt.x() <= qMax( p1.x(), p2.x() ) );
            if( !on ) continue;
            if( ( i == 0 && pt == p1 ) || ( i == lines->size()-1 && pt == p2 ) )   // End of the wire: a pin or a junction
            {
                Pin* pin = ( i == 0 ) ? con->startPin() : con->endPin();
                if( pin ) ends.append( CliRefs::pinRef( pin ) );
                continue;
            }
            if( found.isEmpty() || found.last()->connector() != con ) found.append( line ); // A corner touches two lines of one wire
        }
    }
    QString at = QString("%1,%2").arg( pt.x() ).arg( pt.y() );
    if( found.size() > 1 )
    {
        QStringList uids;
        for( ConnectorLine* l : found ) uids.append( l->connector()->getUid() );
        throw CliError("@"+at+" is on more than one wire ("+uids.join(", ")+"), where they cross: pick another point");
    }
    if( found.isEmpty() )
    {
        if( !ends.isEmpty() ) throw CliError("@"+at+" is the end of a wire, at "+ends.first()+": connect to that pin instead");
        throw CliError("no wire passes through @"+at+" (wire list shows the wire points)");
    }
    return found.first();
}

// A wire end: a pin, or "@x,y" for a new junction at that point on a wire
struct WireEnd
{
    Pin* pin = nullptr;
    ConnectorLine* line = nullptr;     // Junction on this wire segment
    QPoint point;
    Connector* net() { return line ? line->connector() : ( pin ? pin->connector() : nullptr ); }
};

WireEnd wireEnd( QString ref )
{
    WireEnd e;
    if( ref.startsWith('@') )
    {
        e.point = CliRefs::parsePoint( ref.mid( 1 ) ).toPoint();
        e.line  = lineThrough( e.point );
    }else{
        e.pin = CliRefs::pin( ref );
        if( e.pin->connector() ) e.line = lineAt( e.pin, e.point );   // Automatic junction next to the pin
    }
    return e;
}

// Things that look connected in the picture but are not: a wire lying on another
// wire, or running over a pin it doesn't end at. Reported as notes, not errors.
void layoutWarnings( Connector* con )
{
    auto onSeg = []( QPoint p, QPoint a, QPoint b ){
        if( a.x() == b.x() ) return p.x() == a.x() && p.y() >= qMin( a.y(), b.y() ) && p.y() <= qMax( a.y(), b.y() );
        if( a.y() == b.y() ) return p.y() == a.y() && p.x() >= qMin( a.x(), b.x() ) && p.x() <= qMax( a.x(), b.x() );
        return false;
    };
    QSet<QString> notes;
    for( ConnectorLine* line : *con->lineList() )
    {
        QPoint a = line->p1(), b = line->p2();
        if( a == b ) continue;
        bool vert = ( a.x() == b.x() ), horiz = ( a.y() == b.y() );
        if( !vert && !horiz ) continue;

        for( Connector* other : *Circuit::self()->conList() )   // Collinear overlap with another wire
        {
            if( other == con ) continue;
            for( ConnectorLine* ol : *other->lineList() )
            {
                QPoint c = ol->p1(), d = ol->p2();
                int lo, hi;
                if( vert && c.x() == d.x() && c.x() == a.x() ){
                    lo = qMax( qMin( a.y(), b.y() ), qMin( c.y(), d.y() ) );
                    hi = qMin( qMax( a.y(), b.y() ), qMax( c.y(), d.y() ) );
                    if( hi > lo ) notes.insert( QString("wire runs along %1 at x=%2 from y=%3 to %4: they look joined but are not").arg( other->getUid() ).arg( a.x() ).arg( lo ).arg( hi ) );
                }
                else if( horiz && c.y() == d.y() && c.y() == a.y() ){
                    lo = qMax( qMin( a.x(), b.x() ), qMin( c.x(), d.x() ) );
                    hi = qMin( qMax( a.x(), b.x() ), qMax( c.x(), d.x() ) );
                    if( hi > lo ) notes.insert( QString("wire runs along %1 at y=%2 from x=%3 to %4: they look joined but are not").arg( other->getUid() ).arg( a.y() ).arg( lo ).arg( hi ) );
                }
            }
        }
        for( Component* comp : *Circuit::self()->compList() )  // Pins the wire passes over
        {
            for( Pin* pin : comp->getPins() )
            {
                if( !pin || pin == con->startPin() || pin == con->endPin() || !pin->isVisible() ) continue;
                if( onSeg( CliRefs::pinPos( pin ), a, b ) )
                    notes.insert("wire passes over "+CliRefs::pinRef( pin )+" without connecting to it");
            }
        }
    }
    QStringList list = notes.values();
    list.sort();
    for( QString n : list ) CliApi::self()->addMessage( n+" (use --via to route around)" );
}

// Wire path from a to b through the --via points. Every leg is horizontal or vertical:
// where two points differ in x and y a corner is added, leaving the start pin along its
// own direction (startVertical) and then turning at each corner.
QStringList routePoints( QPoint a, QPoint b, QString via, bool startVertical )
{
    QList<QPoint> target;
    if( !via.isEmpty() )
    {
        QStringList v = via.split(',');
        if( v.size() % 2 ) throw CliError("--via needs x,y pairs");
        for( int i=0; i<v.size(); i+=2 ) target.append( QPoint( v[i].toInt(), v[i+1].toInt() ) );
    }
    target.append( b );

    QList<QPoint> pts{ a };
    bool vertical = startVertical;                 // Direction of the next leg
    for( QPoint p : target )
    {
        QPoint last = pts.last();
        if( p == last ) continue;
        if( p.x() != last.x() && p.y() != last.y() )   // Two legs: "vertical" then the other way,
            pts.append( vertical ? QPoint( last.x(), p.y() ) : QPoint( p.x(), last.y() ) ); // so the next turns back to "vertical"
        else vertical = ( p.x() != last.x() );           // Straight leg: the next one turns
        pts.append( p );
    }
    QStringList out;
    for( QPoint p : pts ) { out.append( QString::number( p.x() ) ); out.append( QString::number( p.y() ) ); }
    return out;
}
}

void CliApi::registerEdit()
{
    //--- Library -------------------------------------------------------------

    add("lib select", "lib select <name> | lib select --none", "Select a category or component in the component list "
        "(by the name it shows; expands its category and scrolls to it), or clear the selection. "
        "capture window library grabs the list.",
        []( CliCtx& c ) -> QJsonValue
    {
        ComponentSelector* sel = ComponentSelector::self();
        if( c.has("none") ){ sel->clearSelection(); sel->setCurrentItem( nullptr ); return QJsonObject{ {"selected", ""} }; }
        QString name = c.arg( 0, "name");
        QList<QTreeWidgetItem*> found = sel->findItems( name, Qt::MatchFixedString | Qt::MatchRecursive );
        qDebug() << "[CLI] lib select" << name << "matches:" << found.size();
        if( found.isEmpty() ) throw CliError("no category or component named "+name+" in the component list");
        QTreeWidgetItem* item = found.first();
        for( QTreeWidgetItem* p = item->parent(); p; p = p->parent() ) p->setExpanded( true );
        sel->clearSelection();
        sel->setCurrentItem( item );
        item->setSelected( true );
        sel->scrollToItem( item );
        sel->setFocus();   // As a click: the current item gets its focus frame
        return QJsonObject{ {"selected", item->text( 0 ) }, {"category", item->childCount() > 0 } };
    });

    add("lib list", "lib list [--search TEXT] [--category CAT]", "Component types that can be added (use NAME with comp add).",
        []( CliCtx& c ) -> QJsonValue
    {
        QString search = c.opt("search", c.argOr( 0, "") );
        QString cat = c.opt("category");
        QJsonArray list;
        for( CatalogItem& it : catalog() )
        {
            if( !cat.isEmpty() && !it.category.contains( cat, Qt::CaseInsensitive ) ) continue;
            if( !search.isEmpty() && !it.name.contains( search, Qt::CaseInsensitive )
                                  && !it.caption.contains( search, Qt::CaseInsensitive ) ) continue;
            list.append( QJsonObject{ {"name",it.name}, {"type",it.type}, {"caption",it.caption}, {"category",it.category} } );
        }
        return list;
    });

    //--- Components ----------------------------------------------------------

    add("comp add", "comp add <name> [--at x,y] [--label L] [--rot DEG] [prop=value ...]",
        "Add a component (names from lib list). Returns its uid.",
        []( CliCtx& c ) -> QJsonValue
    {
        CatalogItem item = findItem( c.arg( 0, "component name") );
        QPointF pos = c.has("at") ? CliRefs::parsePoint( c.opt("at") ) : freeSpot();

        stopSimForEdit();
        Circuit* circ = Circuit::self();
        circ->beginUndoStep();

        Component* comp = circ->createItem( item.type, item.name+"-"+circ->newSceneId() );
        if( !comp ){
            circ->cancelUndoStep();
            throw CliError("could not create "+item.name );
        }
        if( item.type == "Subcircuit" )            // Same defaults as dropping it from the selector
        {
            SubCircuit* subC = static_cast<SubCircuit*>( comp );
            if( !subC->isBoard() ) subC->setLogicSymbol( true );
        }
        circ->addComponent( comp );
        comp->moveTo( toGrid( pos ) );

        auto fail = [&]( QString err )
        {
            circ->clearSelection();
            circ->removeComp( comp );
            circ->cancelUndoStep();
            throw CliError( err );
        };
        QStringList keys = c.sets.keys();
        for( QString k : keys )
        {
            QString name;
            try { name = propName( comp, k ); } catch( CliError& e ){ fail( e.msg ); }
            QString value;
            try { value = normalizeValue( findProp( comp, name ), c.sets.value( k ).toString() ); }
            catch( CliError& e ){ fail( e.msg ); }
            comp->setPropStr( name, value );
        }
        if( c.has("label") ) comp->setIdLabel( c.opt("label") );
        if( c.has("rot") ) comp->setPropStr("rotation", c.opt("rot") );
        circ->endUndoStep();

        return CliRefs::placedJson( comp );
    });

    add("comp set", "comp set <comp> prop=value [prop=value ...]", "Change component properties (undoable).",
        []( CliCtx& c ) -> QJsonValue
    {
        Component* comp = CliRefs::comp( c.arg( 0, "component") );
        if( c.sets.isEmpty() ) throw CliError("nothing to set: give prop=value pairs");

        QMap<QString, QString> changes;
        for( QString k : c.sets.keys() )
        {
            QString name = propName( comp, k );
            changes[name] = normalizeValue( findProp( comp, name ), c.sets.value( k ).toString() );
        }

        Circuit* circ = Circuit::self();
        QString uid = comp->getUid();
        circ->beginUndoStep();
        QMap<QString, QString> old;
        for( QString k : changes.keys() )
        {
            old[k] = comp->getPropStr( k );
            if( k == "label" ) comp->setIdLabel( changes[k] );
            else               comp->setPropStr( k, changes[k] );
        }
        circ->calcCircuitChanges();
        for( QString k : changes.keys() ) circ->addCompChange( uid, k, old[k] );
        circ->endCircuitBatch();
        circ->setChanged();

        Component* now = CliRefs::comp( uid );   // Some properties recreate pins: reread
        QJsonObject res{ {"uid", uid } };
        for( QString k : changes.keys() ) res[k] = CliRefs::propValue( now, k );
        return res;
    });

    add("comp link", "comp link <comp> [target ...] [--clear]",
        "Links of a linker component (scripted chips, MCUs): the components it drives, in order "
        "(index 0, 1, ... in the script's component.setLinkedValue). Targets are appended; --clear "
        "removes all links first. No target: list the links. Like the GUI's Link mode (not undoable).",
        []( CliCtx& c ) -> QJsonValue
    {
        Component* comp = CliRefs::comp( c.arg( 0, "component") );
        Linker* linker = dynamic_cast<Linker*>( comp );
        if( !linker ) throw CliError( comp->getUid()+" can't link to other components");

        QList<Component*> targets;
        for( int i=1; i<c.pos.size(); ++i )
        {
            Component* t = CliRefs::comp( c.pos.at( i ) );
            if( t == comp ) throw CliError("a component can't link to itself");
            targets.append( t );
        }
        bool changed = false;
        if( c.flag("clear") )
        {
            while( Component* t = linker->getLinkedComp( 0 ) ) { linker->compSelected( t ); changed = true; }
        }
        for( Component* t : targets )
        {
            bool found = false;
            for( int i=0; Component* l = linker->getLinkedComp( i ); ++i ) if( l == t ) found = true;
            if( found ) continue;
            linker->compSelected( t );                 // Adds the link (if the target accepts one)
            bool now = false;
            for( int i=0; Component* l = linker->getLinkedComp( i ); ++i ) if( l == t ) now = true;
            if( !now ) throw CliError( t->getUid()+" can't be linked (no linkable value)");
            changed = true;
        }
        linker->compSelected( nullptr );               // Leave link mode, hide the numbers
        if( changed ) Circuit::self()->setChanged();

        QJsonArray links;
        for( int i=0; Component* l = linker->getLinkedComp( i ); ++i )
            links.append( QJsonObject{ {"index", i}, {"uid", l->getUid()}, {"label", l->idLabel()} } );
        return QJsonObject{ {"uid", comp->getUid()}, {"links", links} };
    });

    add("comp move", "comp move <comp> <x,y>", "Move a component to a position (snapped to grid).",
        []( CliCtx& c ) -> QJsonValue
    {
        Component* comp = CliRefs::comp( c.arg( 0, "component") );
        QPointF pos = toGrid( CliRefs::parsePoint( c.arg( 1, "x,y") ) );

        Circuit* circ = Circuit::self();
        circ->beginCircuitBatch();
        circ->addCompChange( comp->getUid(), "Pos", comp->getPropStr("Pos") );
        for( Pin* pin : comp->getPins() )
        {
            if( !pin || !pin->connector() ) continue;
            Connector* con = pin->connector();
            circ->addCompChange( con->getUid(), "pointList", con->pListStr() );
        }
        comp->moveTo( pos );
        circ->endCircuitBatch();
        return CliRefs::placedJson( comp );
    });

    add("comp rotate", "comp rotate <comp> [cw|ccw|180]", "Rotate a component (default cw = 90° clockwise).",
        []( CliCtx& c ) -> QJsonValue
    {
        Component* comp = CliRefs::comp( c.arg( 0, "component") );
        QString dir = c.argOr( 1, "cw").toLower();
        if     ( dir == "cw"  ) comp->rotateCW();
        else if( dir == "ccw" ) comp->rotateCCW();
        else if( dir == "180" ) comp->rotateAngle( -180 );
        else throw CliError("rotation must be cw, ccw or 180");
        return CliRefs::placedJson( comp );
    });

    add("comp flip", "comp flip <comp> <h|v>", "Flip a component horizontally or vertically.",
        []( CliCtx& c ) -> QJsonValue
    {
        Component* comp = CliRefs::comp( c.arg( 0, "component") );
        QString dir = c.arg( 1, "h|v").toLower();
        if     ( dir == "h" ) comp->slotH_flip();
        else if( dir == "v" ) comp->slotV_flip();
        else throw CliError("flip must be h or v");
        return CliRefs::placedJson( comp );
    });

    add("comp remove", "comp remove <comp> [comp ...]", "Remove components (and their wires).",
        []( CliCtx& c ) -> QJsonValue
    {
        if( c.pos.isEmpty() ) throw CliError("missing component");
        QList<Component*> comps;
        for( QString ref : c.pos ) comps.append( CliRefs::comp( ref ) );

        stopSimForEdit();
        Circuit* circ = Circuit::self();
        circ->clearSelection();
        QJsonArray removed;
        for( Component* comp : comps ){ comp->setSelected( true ); removed.append( comp->getUid() ); }
        circ->removeItems();
        return removed;
    });

    //--- Wires ---------------------------------------------------------------

    add("wire add", "wire add <pinA|@x,y> <pinB|@x,y> [--via x,y,...]",
        "Connect two pins. A pin that already has a wire gets a junction node on that wire, in the middle of the "
        "segment next to the pin. To choose the junction yourself, give a point on a wire instead of a pin: "
        "wire add U1.Trg @160,64 joins the wire passing through 160,64 there (corners work too, crossings don't).",
        []( CliCtx& c ) -> QJsonValue
    {
        // Resolve both ends (and junction points) first so nothing changes on an error
        WireEnd a = wireEnd( c.arg( 0, "first pin or @x,y") );
        WireEnd b = wireEnd( c.arg( 1, "second pin or @x,y") );
        if( a.pin && a.pin == b.pin ) throw CliError("cannot connect a pin to itself");
        for( Pin* p : { a.pin, b.pin } )  // Hidden pins are not used in the part's current mode (the GUI
            if( p && !p->isVisibleTo( p->parentItem() ) )   // can't reach them): e.g. a WaveGen's ground
                throw CliError( CliRefs::pinRef( p )+" is hidden: the part does not use it in its current mode "
                                "(a Wave Generator's gndnod only with Bipolar on)");
        if( a.net() && a.net() == b.net() ) throw CliError("already connected by "+a.net()->getUid() );
        if( a.pin && b.pin && a.pin->isBus() != b.pin->isBus() ) throw CliError("cannot connect a bus pin to a non-bus pin");
        ConnectorLine* lineA = a.line;
        ConnectorLine* lineB = b.line;
        Pin* pinA = a.pin;
        Pin* pinB = b.pin;
        QPoint ptA = a.point, ptB = b.point;

        stopSimForEdit();
        Circuit* circ = Circuit::self();

        if( lineA ) lineA->connectToWire( ptA );      // Starts a new wire at a junction on A's wire
        else        circ->newconnector( pinA, true );
        Connector* con = circ->getNewConnector();

        if( lineB ) lineB->connectToWire( ptB );      // Ends the new wire at a junction on B's wire
        else        circ->closeconnector( pinB, true );

        if( con && con->startPin() && con->endPin() )
        {
            QPoint a = con->startPin()->scenePos().toPoint();
            QPoint b = con->endPin()->scenePos().toPoint();
            Pin* sp = con->startPin();
            QString side = dynamic_cast<Node*>( sp->component() ) ? "" : CliRefs::pinSide( sp );
            con->setPointList( routePoints( a, b, c.opt("via"), side == "up" || side == "down" ) );
        }
        if( !con || !circ->conList()->contains( con ) ) throw CliError("wire could not be created");
        layoutWarnings( con );
        return CliRefs::wireJson( con );
    });

    add("wire remove", "wire remove <wire> [wire ...]", "Remove wires (uids from wire list). A junction left with two wires is "
        "dissolved and they merge into one new wire (reported as merged); a wire left alone on a junction is removed too.",
        []( CliCtx& c ) -> QJsonValue
    {
        if( c.pos.isEmpty() ) throw CliError("missing wire");
        QList<Connector*> cons;
        for( QString ref : c.pos ) cons.append( CliRefs::wire( ref ) );

        stopSimForEdit();
        Circuit* circ = Circuit::self();
        QStringList before;
        for( Connector* con : *circ->conList() ) before.append( con->getUid() );

        circ->clearSelection();
        for( Connector* con : cons ) con->setSelected( true );
        circ->removeItems();

        // Junctions left with two wires are dissolved and those wires merged into a
        // new one; a junction left with one wire takes that wire with it.
        QStringList after;
        QJsonArray created;
        for( Connector* con : *circ->conList() ){
            after.append( con->getUid() );
            if( !before.contains( con->getUid() ) ) created.append( CliRefs::wireJson( con ) );
        }
        QJsonArray removed;
        for( QString uid : before ) if( !after.contains( uid ) ) removed.append( uid );
        QJsonObject o{ {"removed", removed } };
        if( !created.isEmpty() ) o["merged"] = created;
        return o;
    });

    //--- Undo / redo ---------------------------------------------------------

    add("undo", "undo", "Undo the last circuit change (same as Ctrl+Z).",
        []( CliCtx& ) -> QJsonValue
    {
        stopSimForEdit();
        Circuit::self()->undo();
        return "ok";
    });

    add("redo", "redo", "Redo the last undone change (same as Ctrl+Y).",
        []( CliCtx& ) -> QJsonValue
    {
        stopSimForEdit();
        Circuit::self()->redo();
        return "ok";
    });
}
