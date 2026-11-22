/***************************************************************************
 *   SimulIDE command line control interface                              *
 *   trigger ...: the circuit's triggers (specs/triggers.html §11)         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QJsonArray>

#include "cliapi.h"
#include "clirefs.h"
#include "circuit.h"
#include "component.h"
#include "pin.h"
#include "simulator.h"
#include "triggers.h"

namespace
{
Triggers* trig()
{
    Triggers* t = Triggers::self();
    if( !t ) throw CliError("no circuit");
    return t;
}

// "IC1.VDD", "IC1.3" (pin number, 1-based), "R1.lPin": component uid and the pin's short name
void pinRef( const QString& ref, QString& uid, QString& pin )
{
    int dot = ref.lastIndexOf('.');
    if( dot <= 0 || dot == ref.size()-1 ) throw CliError("invalid pin: "+ref+" (expected <part>.<pin>, e.g. R1.lPin, IC1.VDD, IC1.3)");
    Component* c = CliRefs::comp( ref.left( dot ) );
    Pin* p = Triggers::findPin( c->getUid(), ref.mid( dot+1 ) );
    if( !p ){
        QStringList names;
        for( Pin* x : c->getPins() ) if( x ) names.append( Triggers::pinName( x, c ) );
        throw CliError("no pin "+ref.mid( dot+1 )+" on "+ref.left( dot )+". Pins: "+names.join(", "));
    }
    uid = c->getUid();
    pin = Triggers::pinName( p, c );
}

bool isPin( const QString& ref )
{
    try { QString u, p; pinRef( ref, u, p ); return true; }
    catch( CliError& ) { return false; }
}

// The watch (§11): IC1.VDD, IC1.3, Q1.G-Q1.S, IC1.5:i, R5:i, R5 (across), node:R5.rPin
void parseWatch( const QString& w, TrigCond& c )
{
    c.comp.clear(); c.pin.clear(); c.refComp.clear(); c.ref.clear(); c.node.clear();
    c.sig = "volt";
    if( w.startsWith("node:") )
    {
        QString uid, pin;
        pinRef( w.mid( 5 ), uid, pin );
        c.node = uid+":"+pin;
        return;
    }
    if( w.endsWith(":i") || w.endsWith(":I") )
    {
        c.sig = "curr";
        QString r = w.left( w.size()-2 );
        if( isPin( r ) ){ pinRef( r, c.comp, c.pin ); return; }
        Component* comp = CliRefs::comp( r );
        std::vector<Pin*> pins = comp->getPins();
        if( pins.size() != 2 ) throw CliError( r+" has "+QString::number( pins.size() )+" pins: name one (e.g. "+r+".1:i)");
        c.comp = comp->getUid();
        c.pin  = Triggers::pinName( pins[0], comp );   // Through the part, from pin 1 to pin 2
        return;
    }
    if( isPin( w ) ){ pinRef( w, c.comp, c.pin ); return; }
    for( int i=w.indexOf('-'); i > 0; i=w.indexOf('-', i+1) )   // A pin pair (uids hold '-' too)
    {
        QString a = w.left( i ), b = w.mid( i+1 );
        if( !isPin( a ) || !isPin( b ) ) continue;
        pinRef( a, c.comp, c.pin );
        QString rc;
        pinRef( b, rc, c.ref );
        if( rc != c.comp ) c.refComp = rc;
        return;
    }
    Component* comp = CliRefs::comp( w );       // A two-pin part: the voltage across it
    std::vector<Pin*> pins = comp->getPins();
    if( pins.size() != 2 ) throw CliError( w+" has "+QString::number( pins.size() )+" pins: name one (e.g. "+w+".1)");
    c.comp = comp->getUid();
    c.pin  = Triggers::pinName( pins[0], comp );
    c.ref  = Triggers::pinName( pins[1], comp );
}

double value( const QString& v, const QString& unit )
{
    bool ok = false;
    double d = Simulator::parseSi( v, unit, &ok );
    if( !ok ) throw CliError("invalid value: "+v+" (e.g. 4.5, 350mA, 2.5V)");
    return d;
}

// The condition (§11): rise:4, fall:8, cross:0, out:0..1.2, in:4.5..5.5
void parseCond( const QString& s, TrigCond& c )
{
    int colon = s.indexOf(':');
    if( colon <= 0 ) throw CliError("invalid condition: "+s+" (rise:L, fall:L, cross:L, out:LOW..HIGH, in:LOW..HIGH)");
    QString k = s.left( colon ).toLower(), v = s.mid( colon+1 );
    QString unit = ( c.sig == "curr" ) ? "A" : "V";
    if( k == "rise" || k == "fall" || k == "cross" || k == "either" )
    {
        c.kind  = "edge";
        c.edge  = k == "rise" ? 1 : k == "fall" ? -1 : 0;
        c.level = value( v, unit );
        return;
    }
    if( k == "out" || k == "in" )
    {
        QStringList lh = v.split("..");
        if( lh.size() != 2 ) throw CliError("a window is LOW..HIGH, e.g. out:0..1.2");
        c.kind = "window";
        c.outside = k == "out";
        c.low  = value( lh.at( 0 ), unit );
        c.high = value( lh.at( 1 ), unit );
        if( !( c.low < c.high ) ) throw CliError("the window's low must be below its high");
        return;
    }
    throw CliError("unknown condition: "+k+" (rise, fall, cross, out, in)");
}

void parseOptions( CliCtx& c, Trigger& t, bool isNew )
{
    if( c.has("pause") ) t.pause = c.flag("pause");
    if( c.has("keyframe") )
    {
        t.keyframe = c.flag("keyframe");
        if( t.keyframe ) throw CliError("--keyframe: the key frame action is not built yet (phase 3b, step 2)");
    }
    if( c.has("before") ) t.before = Cli::parseTime( c.opt("before") );
    if( c.has("mode") )
    {
        QString m = c.opt("mode");
        if( m != "single" && m != "normal" ) throw CliError("--mode is single or normal");
        t.single = m == "single";
    }
    if( c.has("holdoff") ){ t.holdoff = Cli::parseTime( c.opt("holdoff") ); if( isNew && !c.has("mode") ) t.single = false; }
    if( c.has("max") )
    {
        bool ok = false;
        int n = c.opt("max").toInt( &ok );
        if( !ok || n < 0 ) throw CliError("--max is a count (0: no limit)");
        t.max = n;
        if( isNew && !c.has("mode") ) t.single = false;
    }
    if( c.has("hyst") ) t.cond.hyst = value( c.opt("hyst"), t.cond.sig == "curr" ? "A" : "V" );
    if( c.has("name") ) t.name = c.opt("name");
}

QList<int> ids( CliCtx& c )
{
    if( c.pos.isEmpty() ) throw CliError("missing trigger id (see: trigger list)");
    QList<int> l;
    for( const QString& s : c.pos )
    {
        bool ok = false;
        int id = s.toInt( &ok );
        if( !ok || !trig()->find( id ) ) throw CliError("no trigger "+s+" (see: trigger list)");
        l.append( id );
    }
    return l;
}
}

QJsonObject Cli::triggerJson( int id )
{
    Triggers* tr = Triggers::self();
    Trigger* t = tr ? tr->find( id ) : nullptr;
    if( !t ) return QJsonObject();
    QJsonObject o{
        {"id", t->id}, {"armed", t->armed}, {"watches", tr->watchText( *t )}, {"condition", tr->condText( *t )},
        {"action", tr->actionText( *t )}, {"mode", tr->modeText( *t )}, {"hits", t->hits.size()},
        {"last", t->hits.isEmpty() ? QJsonValue() : QJsonValue( Cli::timeStr( t->hits.last() ) )} };
    if( !t->name.isEmpty() ) o["name"] = t->name;
    QString err = tr->check( *t );
    if( !err.isEmpty() ) o["warning"] = err;
    return o;
}

void CliApi::registerTrigger()
{
    add("trigger list", "trigger list", "The circuit's triggers: id, armed, what each watches, its condition, "
        "action, mode, hits in this run and the time of the last one (specs/triggers.html).",
        []( CliCtx& ) -> QJsonValue
    {
        QJsonArray a;
        for( const Trigger& t : trig()->list() ) a.append( Cli::triggerJson( t.id ) );
        return a;
    });

    add("trigger add", "trigger add <watch> <condition> [--pause] [--mode single|normal] [--holdoff T] [--max N] "
        "[--hyst V] [--name TEXT]",
        "A new trigger. Watch: IC1.VDD or IC1.3 (pin number) = a pin to ground; Q1.G-Q1.S = a pin pair; "
        "R5 = across a two-pin part; IC1.5:i = current into a pin; R5:i = current through a two-pin part (pin 1 to 2); "
        "node:R5.rPin = a node. Condition: rise:4, fall:8, cross:0 (either), out:0..1.2, in:4.5..5.5 (values with "
        "or without a unit: 350mA, 2.5V). --pause pauses at the step that fired; without it the trigger only counts. "
        "Default mode single (disarms after firing); --holdoff or --max make it normal. --hyst: default 1 % of the "
        "level (at least 1 mV / 1 µA).",
        []( CliCtx& c ) -> QJsonValue
    {
        if( c.pos.size() < 2 ) throw CliError("usage: trigger add <watch> <condition> [options]");
        Trigger t;
        parseWatch( c.pos.at( 0 ), t.cond );
        parseCond( c.pos.at( 1 ), t.cond );
        parseOptions( c, t, true );
        QString err = trig()->check( t );
        if( !err.isEmpty() ) throw CliError( err );
        int id = trig()->add( t );
        return Cli::triggerJson( id );
    });

    add("trigger set", "trigger set <id> [--watch W] [--cond C] [--pause|--pause=0] [--mode single|normal] "
        "[--holdoff T] [--max N] [--hyst V] [--name TEXT]",
        "Change a trigger: the same options as trigger add (--watch and --cond for its signal and condition).",
        []( CliCtx& c ) -> QJsonValue
    {
        QList<int> l = ids( c );
        if( l.size() != 1 ) throw CliError("trigger set takes one id");
        Trigger t = *trig()->find( l.first() );
        if( c.has("watch") ) parseWatch( c.opt("watch"), t.cond );
        if( c.has("cond") || c.has("watch") )
        {
            QString cond = c.opt("cond");
            if( cond.isEmpty() ) throw CliError("a new watch needs its --cond too (the units may differ)");
            parseCond( cond, t.cond );
        }
        parseOptions( c, t, false );
        QString err = trig()->check( t );
        if( !err.isEmpty() ) throw CliError( err );
        trig()->set( t );
        return Cli::triggerJson( t.id );
    });

    add("trigger arm", "trigger arm <id>...", "Arm triggers.", []( CliCtx& c ) -> QJsonValue
    {
        QList<int> l = ids( c );
        trig()->setArmed( l, true );
        QJsonArray a; for( int id : l ) a.append( Cli::triggerJson( id ) );
        return a;
    });

    add("trigger disarm", "trigger disarm <id>...", "Disarm triggers (kept, not watched).", []( CliCtx& c ) -> QJsonValue
    {
        QList<int> l = ids( c );
        trig()->setArmed( l, false );
        QJsonArray a; for( int id : l ) a.append( Cli::triggerJson( id ) );
        return a;
    });

    add("trigger del", "trigger del <id>...", "Delete triggers (no question; Edit → Undo brings them back).",
        []( CliCtx& c ) -> QJsonValue
    {
        int n = trig()->remove( ids( c ) );
        return QJsonObject{ {"deleted", n} };
    });

    add("trigger hits", "trigger hits [<id>]", "The hits of this run with their times (all triggers without an id).",
        []( CliCtx& c ) -> QJsonValue
    {
        QList<int> l;
        if( c.pos.isEmpty() ){ for( const Trigger& t : trig()->list() ) l.append( t.id ); }
        else l = ids( c );
        QJsonArray a;
        for( int id : l )
        {
            Trigger* t = trig()->find( id );
            QJsonArray times;
            for( uint64_t h : t->hits ) times.append( QString::number( h ) );
            QJsonObject o = Cli::triggerJson( id );
            o["times_ps"] = times;
            a.append( o );
        }
        return a;
    });
}
