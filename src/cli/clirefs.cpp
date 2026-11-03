/***************************************************************************
 *   SimulIDE command line control interface                              *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QJsonArray>

#include "clirefs.h"
#include "cliapi.h"
#include "circuit.h"
#include "component.h"
#include "connector.h"
#include "node.h"
#include "pin.h"
#include "comproperty.h"
#include "simulator.h"

namespace CliRefs
{

Component* comp( QString ref )
{
    if( ref.isEmpty() ) throw CliError("empty component reference");
    Circuit* circ = Circuit::self();

    for( Component* c : *circ->compList() ) if( c->getUid() == ref ) return c;
    for( Component* c : *circ->nodeList() ) if( c->getUid() == ref ) return c;

    Component* found = nullptr;
    for( Component* c : *circ->compList() )
    {
        if( c->idLabel() != ref ) continue;
        if( found ) throw CliError("ambiguous label '"+ref+"': use the component uid ("+found->getUid()+", "+c->getUid()+")");
        found = c;
    }
    if( found ) return found;
    throw CliError("component not found: "+ref+" (see: comp list)");
}

// Top level component that exposes this pin (Pin::component() can be an internal
// part of a subcircuit)
static Component* owner( Pin* p )
{
    Circuit* circ = Circuit::self();
    Component* c = p->component();
    if( c && ( circ->compList()->contains( c ) || circ->nodeList()->contains( (Node*)c ) ) ) return c;

    for( Component* comp : *circ->compList() )
        for( Pin* pin : comp->getPins() ) if( pin == p ) return comp;
    return c;
}

static QString pinShortName( Pin* p, Component* c=nullptr )
{
    if( !c ) c = owner( p );
    QString id = p->pinId();
    if( c && id.startsWith( c->getUid()+"-" ) ) return id.mid( c->getUid().size()+1 );
    return id;
}

Pin* pin( QString ref )
{
    int dot = ref.lastIndexOf('.');
    if( dot <= 0 || dot == ref.size()-1 )
        throw CliError("invalid pin reference: "+ref+" (expected <component>.<pin>, e.g. R1.lPin, U1.PB5, R1.0)");

    Component* c = comp( ref.left( dot ) );
    QString name = ref.mid( dot+1 );
    std::vector<Pin*> pins = c->getPins();

    for( Pin* p : pins ) if( p && ( p->pinId() == name || pinShortName( p, c ) == name ) ) return p;
    for( Pin* p : pins ) if( p && p->getLabelText() == name ) return p;
    for( Pin* p : pins ) if( p && pinShortName( p, c ).compare( name, Qt::CaseInsensitive ) == 0 ) return p;
    for( Pin* p : pins ) if( p && p->getLabelText().compare( name, Qt::CaseInsensitive ) == 0 ) return p;

    bool isNum = false;
    int idx = name.toInt( &isNum );
    if( isNum ){
        if( idx >= 0 && idx < (int)pins.size() && pins[idx] ) return pins[idx];
        throw CliError("pin index out of range: "+ref+" ("+QString::number( pins.size() )+" pins)");
    }
    QStringList names;
    for( Pin* p : pins ) if( p ) names.append( pinShortName( p, c ) );
    throw CliError("pin '"+name+"' not found on "+c->getUid()+". Pins: "+names.join(", ") );
}

Connector* wire( QString ref )
{
    for( Connector* con : *Circuit::self()->conList() )
        if( con->getUid() == ref ) return con;
    throw CliError("wire not found: "+ref+" (see: wire list)");
}

QString pinRef( Pin* p )
{
    Component* c = owner( p );
    if( !c ) return p->pinId();
    QString label = c->idLabel();
    QString name = label.isEmpty() ? c->getUid() : label;
    // Use the uid when the label is not unique
    int count = 0;
    for( Component* o : *Circuit::self()->compList() ) if( o->idLabel() == label ) count++;
    if( count != 1 || label.contains(' ') || label.contains('.') ) name = c->getUid();
    return name+"."+pinShortName( p, c );
}

QPoint pinPos( Pin* p ) { return p->scenePos().toPoint(); }

QString pinSide( Pin* p )
{
    // The pin's tip is its local origin and its body runs along local +x, towards the component
    QPointF out = p->mapToScene( QPointF( 0, 0 ) ) - p->mapToScene( QPointF( 1, 0 ) );
    if( qAbs( out.x() ) >= qAbs( out.y() ) ) return out.x() > 0 ? "right" : "left";
    return out.y() > 0 ? "down" : "up";
}

QJsonObject pinJson( Pin* p, int index )
{
    QJsonObject o{
        {"index", index },
        {"id",    pinShortName( p ) },
        {"ref",   pinRef( p ) } };
    if( !p->getLabelText().isEmpty() ) o["label"] = p->getLabelText();
    QPoint pos = pinPos( p );
    o["pos"]  = QJsonArray{ pos.x(), pos.y() };
    o["side"] = pinSide( p );

    Connector* con = p->connector();
    if( con ){
        o["wire"] = con->getUid();
        Pin* other = ( con->startPin() == p ) ? con->endPin() : con->startPin();
        if( other ) o["to"] = pinRef( other );
    }
    if( Simulator::self()->isRunning() && p->isConnected() ) o["voltage"] = p->getVoltage();
    return o;
}

// A leading "_" marks units without SI prefixes ("_º", "_Steps"): show them without it
static QString shownValue( ComProperty* p )
{
    QString unit  = p->unit();
    QString value = p->getValStr();
    if( unit.startsWith('_') && value.endsWith(" "+unit) ){
        value.chop( unit.size() );
        value.append( unit.mid( 1 ) );
    }
    return value;
}

QString propValue( Component* comp, QString name )
{
    for( propGroup& group : *comp->properties() )
        for( ComProperty* p : group.propList )
            if( p->name() == name ) return shownValue( p );
    return comp->getPropStr( name );
}

QJsonObject compJson( Component* c, bool detail, bool all )
{
    QJsonObject o{
        {"uid",   c->getUid() },
        {"type",  c->itemType() },
        {"label", c->idLabel() },
        {"pos",   QJsonArray{ c->x(), c->y() } } };
    if( c->rotation() != 0 ) o["rotation"] = c->rotation();

    if( !detail ){
        o["pins"] = (int)c->getPins().size();
        return o;
    }
    QJsonArray props;
    for( propGroup& group : *c->properties() )
    {
        bool hiddenGroup = group.flags & groupHidden;
        if( hiddenGroup && !all ) continue;
        // Generic bookkeeping of every component (uid, position, label placement,
        // board placement): covered by other commands, not useful to list
        if( hiddenGroup && ( group.name == "CompBase" || group.name == "CompGraphic" || group.name == "Board") ) continue;
        for( ComProperty* p : group.propList )
        {
            bool hidden = hiddenGroup || ( p->flags() & propHidden );
            if( hidden && !all ) continue;
            if( p->name().isEmpty() ) continue;
            QString unit = p->unit();
            QString value = shownValue( p );
            if( unit.startsWith('_') ) unit = unit.mid( 1 );
            QJsonObject po{
                {"name",  p->name() },
                {"value", value },
                {"group", group.name } };
            if( !p->capt().isEmpty() ) po["caption"] = p->capt();
            if( !p->type().isEmpty() ) po["type"]    = p->type();
            if( p->type() == "enum" )                    // unit holds "values;captions"
            {
                QJsonArray opts;
                for( QString v : p->unit().split(';').first().split(',', QString::SkipEmptyParts ) ) opts.append( v );
                po["options"] = opts;
            }
            else if( !unit.isEmpty() ) po["unit"] = unit;
            if( hidden ) po["hidden"] = true;
            props.append( po );
    }   }
    o["properties"] = props;

    QJsonArray pins;
    std::vector<Pin*> pinList = c->getPins();
    for( int i=0; i<(int)pinList.size(); ++i )
        if( pinList[i] ) pins.append( pinJson( pinList[i], i ) );
    o["pins"] = pins;
    return o;
}

QJsonObject placedJson( Component* c )
{
    QJsonObject o = compJson( c, false );
    QJsonArray pins;
    for( Pin* p : c->getPins() )
    {
        if( !p ) continue;
        QJsonObject po{ {"ref", pinRef( p ) } };
        if( !p->getLabelText().isEmpty() ) po["label"] = p->getLabelText();
        QPoint pos = pinPos( p );
        po["pos"]  = QJsonArray{ pos.x(), pos.y() };
        po["side"] = pinSide( p );
        pins.append( po );
    }
    o["pins"] = pins;
    return o;
}

QJsonObject wireJson( Connector* con )
{
    QJsonObject o{ {"uid", con->getUid() } };
    if( con->startPin() ) o["from"] = pinRef( con->startPin() );
    if( con->endPin()   ) o["to"]   = pinRef( con->endPin() );
    QJsonArray pts;
    for( QString s : con->pointList() ) pts.append( s.toInt() );
    o["points"] = pts;
    return o;
}

QPointF parsePoint( QString s )
{
    QStringList xy = s.split(',');
    if( xy.size() != 2 ) throw CliError("invalid position: "+s+" (expected x,y)");
    bool okx, oky;
    double x = xy[0].trimmed().toDouble( &okx );
    double y = xy[1].trimmed().toDouble( &oky );
    if( !okx || !oky ) throw CliError("invalid position: "+s+" (expected x,y)");
    return QPointF( x, y );
}

}
