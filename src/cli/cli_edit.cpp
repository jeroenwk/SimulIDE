/***************************************************************************
 *   SimulIDE command line control interface                              *
 *   Editing commands: library, components, wires, undo/redo               *
 ***( see copyright.txt file at root folder )*******************************/

#include <climits>
#include <cmath>

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
#include "wirestyledialog.h"
#include "connectorline.h"
#include "node.h"
#include "pin.h"
#include "subcircuit.h"
#include "linker.h"
#include "comproperty.h"
#include "componentselector.h"
#include "simulator.h"
#include "instrumentview.h"
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
    if( type == "enum" )            // unit holds "values;captions": match without case
    {
        QStringList parts = prop->unit().split(';');
        QStringList vals  = parts.value( 0 ).split(',', QString::SkipEmptyParts );
        QStringList capts = parts.value( 1 ).split(',', QString::SkipEmptyParts );
        if( vals.isEmpty() || vals.contains( val ) ) return val;
        QString v = val.trimmed();
        for( int i=0; i<vals.size(); ++i )
        {
            if( vals.at( i ).compare( v, Qt::CaseInsensitive ) == 0 ) return vals.at( i );
            if( i < capts.size() && capts.at( i ).compare( v, Qt::CaseInsensitive ) == 0 ) return vals.at( i );
        }
        bool isInt = false;
        int idx = v.toInt( &isInt );
        if( isInt && idx >= 0 && idx < vals.size() ) return v;        // an index: setters take "2"
        throw CliError("invalid value for "+prop->name()+": "+val+" (one of "+vals.join(", ")+")");
    }
    if( type != "double" && type != "int" && type != "uint" ) return val;

    static const QString prefixes = "pnuµmkMGT";
    QString unit = prop->unit();
    QString base = unit;
    if( base.size() > 1 && prefixes.contains( base.at(0) ) ) base = base.mid( 1 );
    QString shown = base.startsWith("_") ? base.mid( 1 ) : base; // "_%", "_º": the unit as typed

    static const QRegularExpression re("^\\s*([-+]?[0-9]*\\.?[0-9]+(?:[eE][-+]?[0-9]+)?)\\s*([pnuµmkMGT]?)\\s*(.*?)\\s*$");
    QRegularExpressionMatch m = re.match( val );
    if( !m.hasMatch() ) throw CliError("invalid value for "+prop->name()+": "+val+" (examples: 470, 4.7k, 100n, 10m"+base+")");

    QString num    = m.captured( 1 );
    QString prefix = m.captured( 2 );
    QString rest   = m.captured( 3 );
    if( prefix == "u" ) prefix = "µ";
    if( prefix.compare("m", Qt::CaseInsensitive ) == 0 && rest.startsWith("eg", Qt::CaseInsensitive ) )
    {   prefix = "M"; rest = rest.mid( 2 ).trimmed(); }    // SPICE "meg" (1meg, 1 Meg)

    bool degrees = ( shown == "º" || shown == "°" );
    if( !rest.isEmpty() && rest != base )
    {
        // "1m" could be a unit (e.g. meters): only accept exact base unit
        bool same = !base.isEmpty() && ( rest.compare( base, Qt::CaseInsensitive ) == 0
                                      || rest.compare( shown, Qt::CaseInsensitive ) == 0
                                      || ( degrees && ( rest == "º" || rest == "°" || rest == "deg" ) ) );
        if( !same )
            throw CliError("unit mismatch for "+prop->name()+": got '"+rest+"', expected '"+shown+"'");
    }
    double mult = 1;
    if( !prefix.isEmpty() ) mult = QHash<QString,double>{{"p",1e-12},{"n",1e-9},{"µ",1e-6},{"m",1e-3},
                                                         {"k",1e3},{"M",1e6},{"G",1e9},{"T",1e12}}.value( prefix, 1 );
    bool integer = ( type == "int" || type == "uint" );
    if( base.isEmpty() )            // Unitless: plain number
    {
        double v = num.toDouble()*mult;
        return integer ? QString::number( qRound64( v ) ) : QString::number( v, 'g', 15 );
    }
    if( base.startsWith("_") )      // A unit without SI prefixes: the value itself
        return QString::number( num.toDouble()*mult, 'g', 15 )+" "+base;
    if( integer )                   // Integer setters read "<int> <unit>": 4.7k -> 4700 Ω
        return QString::number( qRound64( num.toDouble()*mult ) )+" "+base;
    return num+" "+prefix+base;
}

// The number a normalized double value asks for ("4.7 kΩ" -> 4700), NaN when it is not one
double requestedNumber( ComProperty* prop, QString val )
{
    if( !prop || prop->type() != "double" ) return qQNaN();
    static const QRegularExpression re("^\\s*([-+]?[0-9]*\\.?[0-9]+(?:[eE][-+]?[0-9]+)?)\\s*(.*?)\\s*$");
    QRegularExpressionMatch m = re.match( val );
    if( !m.hasMatch() ) return qQNaN();
    static const QString prefixes = "pnµmkMGT";
    QString base = prop->unit();                  // As normalizeValue writes it: <num> <prefix><base>
    if( base.size() > 1 && prefixes.contains( base.at(0) ) ) base = base.mid( 1 );
    QString rest = m.captured( 2 );
    double mult = 1;
    if( !rest.isEmpty() && rest != base && prefixes.contains( rest.at(0) ) )
        mult = QHash<QChar,double>{{'p',1e-12},{'n',1e-9},{QChar(0xB5),1e-6},{'m',1e-3},
                                   {'k',1e3},{'M',1e6},{'G',1e9},{'T',1e12}}.value( rest.at(0), 1 );
    return m.captured( 1 ).toDouble()*mult;
}

// A setter that clamps or rounds a value stores another one than asked: say so, as Battery
// Voltage=-9 stored 1e-12 V without a word (create-circuit #293)
QString storedNote( Component* comp, QString name, ComProperty* prop, QString val )
{
    double want = requestedNumber( prop, val );
    if( std::isnan( want ) ) return "";
    ComProperty* now = findProp( comp, name );
    if( !now ) return "";
    double got = now->getValue();
    if( qAbs( got-want ) <= 1e-9*qMax( qAbs( want ), qAbs( got ) ) ) return "";
    qDebug() << "[cli:comp set]" << comp->getUid() << name << "asked" << want << "stored" << got;
    return comp->getUid()+" "+name+": asked "+val+", stored "+now->getValStr()+" (the part limits this value)";
}

// Settings come sorted by name; a Model loads its own defaults, so set it first and the other
// settings given with it on top (Eta, BrkDownV next to Model= were lost: create-circuit #229, #243)
QStringList modelFirst( QStringList keys )
{
    QStringList first, rest;
    for( QString k : keys ) ( k.compare("Model", Qt::CaseInsensitive ) == 0 ? first : rest ).append( k );
    return first + rest;
}

// Every wire of the net a pin's wire belongs to, following junction nodes (no simulation needed)
QList<Connector*> netWires( Connector* first ) { return Circuit::netWires( first ); }

// Wire segment next to a pin, to attach a junction to. First the pin's own wire, from the pin
// outwards; if all its segments are too short (bug hunt IMP-022), the splittable segment of the
// same net whose middle is nearest to the pin.
ConnectorLine* lineAt( Pin* pin, QPoint& point )
{
    bool own = true;
    ConnectorLine* best = Circuit::junctionLine( pin, &point, &own );
    if( best && own ) return best;
    QList<Connector*> net = netWires( pin->connector() );
    qDebug() << "[CLI] wire add: junction near" << CliRefs::pinRef( pin ) << "net of" << net.size()
             << "wire(s), nearest splittable segment" << ( best ? QString("%1,%2").arg( point.x() ).arg( point.y() ) : QString("none") );
    if( best )
    {
        CliApi::self()->addMessage( QString("the wire at %1 is too short for a junction: joined %2 at @%3,%4 instead (same net)")
                                    .arg( CliRefs::pinRef( pin ) ).arg( best->connector()->getUid() ).arg( point.x() ).arg( point.y() ) );
        return best;
    }
    throw CliError("no wire segment of the net at "+CliRefs::pinRef( pin )+" is long enough (16 px, straight) for a junction: "
                   "name a point on a wire (@x,y, see wire list), connect to another pin of the net, or move the components apart");
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

// Wire path from a to b through the --via points (Circuit::routePoints does the legs).
// Every leg is horizontal or vertical
QStringList routePoints( QPoint a, QPoint b, QString via, bool startVertical )
{
    QList<QPoint> target;
    if( !via.isEmpty() )
    {
        QStringList v = via.split(',');
        if( v.size() % 2 ) throw CliError("--via needs x,y pairs");
        for( int i=0; i<v.size(); i+=2 ) target.append( QPoint( v[i].toInt(), v[i+1].toInt() ) );
    }
    return Circuit::routePoints( a, b, target, startVertical );
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
        QStringList notes;
        for( QString k : modelFirst( c.sets.keys() ) )
        {
            QString name;
            try { name = propName( comp, k ); } catch( CliError& e ){ fail( e.msg ); }
            QString value;
            try { value = normalizeValue( findProp( comp, name ), c.sets.value( k ).toString() ); }
            catch( CliError& e ){ fail( e.msg ); }
            comp->setPropStr( name, value );
            QString note = storedNote( comp, name, findProp( comp, name ), value );
            if( !note.isEmpty() ) notes.append( note );
        }
        if( c.has("label") ) comp->setIdLabel( c.opt("label") );
        if( c.has("rot") ) comp->setPropStr("rotation", c.opt("rot") );
        circ->endUndoStep();

        QJsonObject placed = CliRefs::placedJson( comp );
        // A WaveGen or Clock with Always On off gives no output until its button is pressed: easy
        // to miss when the circuit is built from the CLI (bug hunt IMP-016)
        if( comp->getPropStr("Always_On") == "false" )
        {
            placed["note"] = comp->getUid()+" has no output until switched on: set Always_On=true, "
                             "or switch it on (ui set "+comp->getUid()+" on)";
            qDebug() << "[cli:comp add]" << comp->getUid() << "added with Always_On off: note sent";
        }
        if( !notes.isEmpty() )
            placed["note"] = ( placed.contains("note") ? placed["note"].toString()+"; " : QString() )+notes.join("; ");
        return placed;
    });

    add("comp set", "comp set <comp> prop=value [prop=value ...]", "Change component properties (undoable).",
        []( CliCtx& c ) -> QJsonValue
    {
        Component* comp = CliRefs::comp( c.arg( 0, "component") );
        if( c.pos.size() > 1 )   // "comp set A x=1 B x=2" set only A, with B's values (create-circuit #181)
        {
            bool isComp = true;  // "Duty=25 %" unquoted: the shell splits off the unit (create-circuit #339)
            try{ CliRefs::comp( c.pos.at( 1 ) ); }catch( CliError& ){ isComp = false; }
            if( !isComp )
                throw CliError("comp set: \""+c.pos.mid( 1 ).join(" ")+"\" is not a prop=value pair:"
                              " a value with a space needs quotes (\"Duty=25 %\") or no space (Duty=25%)");
            throw CliError("comp set changes one component; also given: "+c.pos.mid( 1 ).join(" ")
                          +" (one comp set per component)");
        }
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
        for( QString k : modelFirst( changes.keys() ) )
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
        QStringList notes;
        for( QString k : changes.keys() )
        {
            res[k] = CliRefs::propValue( now, k );
            QString note = storedNote( now, k, findProp( now, k ), changes[k] );
            if( !note.isEmpty() ) notes.append( note );
        }
        if( !notes.isEmpty() ) res["note"] = notes.join("; ");
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
        if( comp->isLocked() ) throw CliError( comp->idLabel()+" is locked: unlock it first (unlock "+comp->idLabel()+")");

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
        if( comp->isLocked() ) throw CliError( comp->idLabel()+" is locked: unlock it first (unlock "+comp->idLabel()+")");
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
        if( comp->isLocked() ) throw CliError( comp->idLabel()+" is locked: unlock it first (unlock "+comp->idLabel()+")");
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
        for( Component* comp : comps )
            if( comp->isLocked() ) throw CliError( comp->idLabel()+" is locked: unlock it first (unlock "+comp->idLabel()+")");

        stopSimForEdit();
        Circuit* circ = Circuit::self();
        circ->clearSelection();
        // Removal goes by the selection, and Qt does not select a hidden item: with instruments
        // hidden (settings instruments=false), a Probe and its leads stayed. Show them meanwhile
        InstrumentView* iv = InstrumentView::self();
        if( !iv->shown() ) iv->apply( true );
        QJsonArray removed;
        for( Component* comp : comps ){ comp->setSelected( true ); removed.append( comp->getUid() ); }
        circ->removeItems();
        if( !iv->shown() ) iv->apply( false );
        return removed;
    });

    add("comp copy", "comp copy <comp> [comp ...] [--by dx,dy]", "Copy and paste components as Edit → Copy / Paste "
        "do (their own triggers come along, specs/triggers.html §9), moved by dx,dy (default 0,80). Replies with the new uids.",
        []( CliCtx& c ) -> QJsonValue
    {
        if( c.pos.isEmpty() ) throw CliError("missing component");
        QList<Component*> comps;
        for( QString ref : c.pos ) comps.append( CliRefs::comp( ref ) );
        QPointF by( 0, 80 );
        if( c.has("by") ) by = CliRefs::parsePoint( c.opt("by") );

        stopSimForEdit();
        Circuit* circ = Circuit::self();
        circ->clearSelection();
        for( Component* comp : comps ) comp->setSelected( true );
        QList<Component*> before = *circ->compList();
        QPointF at = comps.first()->pos();
        circ->copy( at );
        circ->paste( at+by );
        QJsonArray added;
        for( Component* comp : *circ->compList() ) if( !before.contains( comp ) ) added.append( comp->getUid() );
        circ->clearSelection();
        return added;
    });

    //--- Wires ---------------------------------------------------------------

    add("wire add", "wire add <pinA|@x,y> <pinB|@x,y> [--via x,y,...]",
        "Connect two pins. A pin that already has a wire gets a junction node on that wire, in the middle of the "
        "segment next to the pin (or, if that wire is only a short stub, on the nearest long enough segment of the "
        "same net). To choose the junction yourself, give a point on a wire instead of a pin: "
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
            Pin* sp = con->startPin();
            Pin* ep = con->endPin();
            QPoint a = sp->scenePos().toPoint();
            QPoint b = ep->scenePos().toPoint();
            QString side = dynamic_cast<Node*>( sp->component() ) ? "" : sp->side();
            bool vertical = c.opt("via").isEmpty() ? Circuit::routeStartsVertical( sp, ep )   // As "_" does
                                                   : side == "up" || side == "down";
            con->setPointList( routePoints( a, b, c.opt("via"), vertical ) );
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

    add("wire style", "wire style <wire> [wire ...] [--color C|none] [--band C|none] [--opacity 0..100] [--width W|default]",
        "Wire colours (specs/wire-colors.md): the line colour and a translucent band behind the wire, "
        "of the given width (default 6 px). C: #rrggbb, #aarrggbb or a colour name; --opacity sets the "
        "band's alpha in % (default 50 for a new band without alpha). One undo step for all the wires. "
        "Without options: prints them.",
        []( CliCtx& c ) -> QJsonValue
    {
        if( c.pos.isEmpty() ) throw CliError("missing wire");
        QList<Connector*> cons;
        for( QString ref : c.pos ) cons.append( CliRefs::wire( ref ) );

        auto color = []( QString v, QString what ) -> QColor {
            QColor col( v.trimmed() );
            if( !col.isValid() ) throw CliError("invalid "+what+": "+v);
            return col;
        };
        QMap<QString,QString> props;
        if( c.has("color") )
        {
            QString v = c.opt("color");
            props["color"] = v == "none" ? "" : QColor( color( v, "colour" ).rgb() ).name();
        }
        if( c.has("band") || c.has("opacity") )
        {
            QString v = c.opt("band");
            if( v == "none" ) props["bandColor"] = "";
            else{
                QColor col = c.has("band") ? color( v, "band colour" ) : cons.first()->bandColor();
                if( !col.isValid() ) col = QColor( 120, 100, 160 );   // --opacity alone, no band yet
                bool hasAlpha = c.has("band") && v.startsWith('#') && v.length() == 9;
                if( c.has("opacity") )
                {
                    double pct = Cli::parseNum( c.opt("opacity"), "opacity");
                    if( pct < 0 || pct > 100 ) throw CliError("opacity must be 0..100");
                    col.setAlpha( qRound( pct*255/100.0 ) );
                }
                else if( !hasAlpha ) col.setAlpha( qRound( 50*255/100.0 ) );
                props["bandColor"] = col.name( QColor::HexArgb );
            }
        }
        if( c.has("width") )
        {
            QString v = c.opt("width");
            if( v == "default" ) props["bandWidth"] = "";
            else{
                double w = Cli::parseNum( v, "width");
                if( w < 1 || w > 100 ) throw CliError("width must be 1..100 px");
                props["bandWidth"] = QString::number( w );
            }
        }
        if( !props.isEmpty() ) WireStyleDialog::apply( cons, props );

        QJsonArray rows;
        for( Connector* con : cons )
        {
            QJsonObject o = CliRefs::wireJson( con );
            o.remove("points");
            rows.append( o );
        }
        return rows;
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
