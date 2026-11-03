/***************************************************************************
 *   SimulIDE command line control interface                              *
 *   Editing commands: library, components, wires, undo/redo               *
 ***( see copyright.txt file at root folder )*******************************/

#include <QTreeWidgetItem>
#include <QRegularExpression>

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
        point = toGrid( QPointF( ( line->p1()+line->p2() )/2 ) ).toPoint();
        return line;
    }
    throw CliError("no wire segment long enough near "+CliRefs::pinRef( pin )+" to add a junction: "
                   "move the components apart or connect to another pin of the same net");
}

QStringList routePoints( QPoint a, QPoint b, QString via )
{
    QList<QPoint> pts{ a };
    if( !via.isEmpty() )
    {
        QStringList v = via.split(',');
        if( v.size() % 2 ) throw CliError("--via needs x,y pairs");
        for( int i=0; i<v.size(); i+=2 ) pts.append( QPoint( v[i].toInt(), v[i+1].toInt() ) );
    }
    else if( a.x() != b.x() && a.y() != b.y() ) pts.append( QPoint( b.x(), a.y() ) );  // L route
    pts.append( b );

    QStringList out;
    for( QPoint p : pts ) { out.append( QString::number( p.x() ) ); out.append( QString::number( p.y() ) ); }
    return out;
}
}

void CliApi::registerEdit()
{
    //--- Library -------------------------------------------------------------

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

        return CliRefs::compJson( comp, false );
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
        for( QString k : changes.keys() ) res[k] = now->getPropStr( k );
        return res;
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
        return CliRefs::compJson( comp, false );
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
        return CliRefs::compJson( comp, false );
    });

    add("comp flip", "comp flip <comp> <h|v>", "Flip a component horizontally or vertically.",
        []( CliCtx& c ) -> QJsonValue
    {
        Component* comp = CliRefs::comp( c.arg( 0, "component") );
        QString dir = c.arg( 1, "h|v").toLower();
        if     ( dir == "h" ) comp->slotH_flip();
        else if( dir == "v" ) comp->slotV_flip();
        else throw CliError("flip must be h or v");
        return CliRefs::compJson( comp, false );
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

    add("wire add", "wire add <pinA> <pinB> [--via x,y,...]",
        "Connect two pins. A pin that already has a wire gets a junction node on that wire.",
        []( CliCtx& c ) -> QJsonValue
    {
        Pin* pinA = CliRefs::pin( c.arg( 0, "first pin") );
        Pin* pinB = CliRefs::pin( c.arg( 1, "second pin") );
        if( pinA == pinB ) throw CliError("cannot connect a pin to itself");
        if( pinA->connector() && pinA->connector() == pinB->connector() )
            throw CliError("pins are already connected by "+pinA->connector()->getUid() );
        if( pinA->isBus() != pinB->isBus() ) throw CliError("cannot connect a bus pin to a non-bus pin");

        // Pick junction points first so nothing changes if one is not possible
        QPoint ptA, ptB;
        ConnectorLine* lineA = pinA->connector() ? lineAt( pinA, ptA ) : nullptr;
        ConnectorLine* lineB = pinB->connector() ? lineAt( pinB, ptB ) : nullptr;

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
            con->setPointList( routePoints( a, b, c.opt("via") ) );
        }
        if( !con || !circ->conList()->contains( con ) ) throw CliError("wire could not be created");
        return CliRefs::wireJson( con );
    });

    add("wire remove", "wire remove <wire> [wire ...]", "Remove wires (uids from wire list).",
        []( CliCtx& c ) -> QJsonValue
    {
        if( c.pos.isEmpty() ) throw CliError("missing wire");
        QList<Connector*> cons;
        for( QString ref : c.pos ) cons.append( CliRefs::wire( ref ) );

        stopSimForEdit();
        Circuit* circ = Circuit::self();
        circ->clearSelection();
        QJsonArray removed;
        for( Connector* con : cons ){
            removed.append( con->getUid() );
            for( ConnectorLine* line : *con->lineList() ) line->setSelected( true );
        }
        circ->removeItems();
        return removed;
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
