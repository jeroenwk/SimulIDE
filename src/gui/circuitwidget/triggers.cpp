/***************************************************************************
 *   Circuit triggers (specs/triggers.html)                                *
 ***( see copyright.txt file at root folder )*******************************/

#include <cmath>
#include <QDebug>
#include <QDomDocument>
#include <QHash>
#include <QPointer>

#include "triggers.h"
#include "circuit.h"
#include "circuitwidget.h"
#include "component.h"
#include "keyframes.h"
#include "node.h"
#include "pin.h"
#include "e-node.h"
#include "simulator.h"
#include "simstate.h"
#include "simhistory.h"
#include "simskip.h"
#include "statusview.h"
#include "subcircuit.h"
#include "mcu.h"
#include "scriptcpu.h"
#include "oscope.h"
#include "oscopechannel.h"

#define TRIG_LOG qDebug() << "[triggers]"

static const char* kUndoId = "__triggers__";
static QString knownName( class Component* c, class Pin* p );   // The undo step's "component" (Circuit::restoreState)

//------------------------------------------------------------------------------------------------
// Conditions

double TrigCond::hysteresis() const
{
    if( hyst >= 0 ) return hyst;
    double floor = ( sig == "curr" ) ? 1e-6 : 1e-3;   // At least 1 µA / 1 mV
    double span  = ( kind == "window" ) ? std::fabs( high-low ) : std::fabs( level );
    return std::max( 0.01*span, floor );
}

bool TrigCond::operator==( const TrigCond& o ) const
{
    return sig == o.sig && comp == o.comp && pin == o.pin && refComp == o.refComp && ref == o.ref
        && node == o.node && kind == o.kind && edge == o.edge && outside == o.outside
        && level == o.level && low == o.low && high == o.high && hyst == o.hyst
        && block == o.block && from == o.from && to == o.to && change == o.change && sev == o.sev;
}

inline double Triggers::Check::value()
{
    if( direct ) return n1 ? n0->getVolt()-n1->getVolt() : n0->getVolt();
    if( curr ) return p0->getCurrent();
    return p1 ? p0->getVoltage() - p1->getVoltage() : p0->getVoltage();
}

bool Triggers::Check::eval( double v )   // §6: armed beyond the hysteresis, fires at the level
{
    if( window )                          // The hysteresis is inward on both edges
    {
        bool in  = v >= lo+h && v <= hi-h;
        bool out = v < lo || v > hi;
        if( outside ){
            if( armW && out ){ armW = false; return true; }
            if( in ) armW = true;
        }else{
            if( armW && in ){ armW = false; return true; }
            if( out ) armW = true;
        }
        return false;
    }
    bool f = false;
    if( edge >= 0 ){
        if( armR && v >= level ){ armR = false; f = true; }
        else if( v < level-h ) armR = true;
    }
    if( edge <= 0 ){
        if( armF && v <= level ){ armF = false; f = true; }
        else if( v > level+h ) armF = true;
    }
    return f;
}

void Triggers::Check::derive( double v )   // After a keyframe load: which side the value is on
{
    armR = v < level;
    armF = v > level;
    bool inBand = v >= lo && v <= hi;
    armW = outside ? inBand : !inBand;
}

//------------------------------------------------------------------------------------------------

Triggers::Triggers( QObject* parent ) : QObject( parent ) {}
Triggers::~Triggers() {}

Triggers* Triggers::self()
{
    return Circuit::self() ? Circuit::self()->triggers() : nullptr;
}

Trigger* Triggers::find( int id )
{
    for( Trigger& t : m_list ) if( t.id == id ) return &t;
    return nullptr;
}

static Component* compByUid( const QString& uid )
{
    Circuit* circ = Circuit::self();
    if( !circ || uid.isEmpty() ) return nullptr;
    for( Component* c : *circ->compList() ) if( c->getUid() == uid ) return c;
    for( Node* n : *circ->nodeList() ) if( n->getUid() == uid ) return n;
    return nullptr;
}

QString Triggers::pinName( Pin* p, Component* c )
{
    QString id = p->pinId();
    if( c && id.startsWith( c->getUid()+"-" ) ) return id.mid( c->getUid().size()+1 );
    return id;
}

Pin* Triggers::findPin( const QString& uid, const QString& name )
{
    Component* c = compByUid( uid );
    if( !c ) return nullptr;
    std::vector<Pin*> pins = c->getPins();
    for( Pin* p : pins ) if( p && ( pinName( p, c ) == name || p->pinId() == name ) ) return p;
    for( Pin* p : pins ) if( p && p->getLabelText() == name ) return p;
    for( Pin* p : pins ) if( p && pinName( p, c ).compare( name, Qt::CaseInsensitive ) == 0 ) return p;
    for( Pin* p : pins ) if( p && p->getLabelText().compare( name, Qt::CaseInsensitive ) == 0 ) return p;
    for( Pin* p : pins ) if( p && knownName( c, p ).compare( name, Qt::CaseInsensitive ) == 0 ) return p;   // Q1.G
    bool isNum = false;
    int n = name.toInt( &isNum );             // A pin number: 1-based, as on the package
    if( isNum && n >= 1 && n <= (int)pins.size() ) return pins[n-1];
    return nullptr;
}

static QString compName( Component* c );
static QString pinLabel( Component* c, Pin* p );
static QString knownName( Component* c, Pin* p );

static void splitNode( const QString& node, QString& uid, QString& pin )
{
    int i = node.lastIndexOf(':');
    uid = node.left( i );
    pin = node.mid( i+1 );
}

QString Triggers::check( const Trigger& t )
{
    const TrigCond& c = t.cond;
    if( c.sig != "volt" && c.sig != "curr" && c.sig != "state" && c.sig != "scope" ) return "unknown signal "+c.sig;
    if( c.sig == "scope" )
    {
        Oscope* o = dynamic_cast<Oscope*>( compByUid( c.comp ) );
        if( !o ) return "no scope "+c.comp;
        if( o->trigger() < 0 || o->trigger() > 3 ) return "the scope's trigger is off: choose its channel";
        return "";
    }
    if( c.sig == "state" )
    {
        Component* comp = compByUid( c.comp );
        if( !comp ) return "no part "+c.comp;
        if( !stateSource( comp, c.block ) )
        {
            QStringList b = stateBlocks( comp );
            if( b.isEmpty() ) return compName( comp )+" reports no state";
            for( QString& x : b ) if( x.isEmpty() ) x = "state";
            return c.block.isEmpty() ? compName( comp )+" has no state of its own: blocks "+b.join(", ")
                                     : "no block "+c.block+" on "+compName( comp )+": "+b.join(", ");
        }
        if( !c.change && c.sev < 0 && c.from.isEmpty() && c.to.isEmpty() ) return "no state condition";
        return "";
    }
    if( !c.node.isEmpty() )
    {
        QString uid, pin;
        splitNode( c.node, uid, pin );
        if( !compByUid( uid ) ) return "no part "+uid+" for node "+c.node;
        if( !findPin( uid, pin ) ) return "no pin "+pin+" on "+uid;
        if( c.sig == "curr" ) return "a node has no current: watch a pin";
    }else{
        if( !compByUid( c.comp ) ) return "no part "+c.comp;
        if( !findPin( c.comp, c.pin ) ) return "no pin "+c.pin+" on "+c.comp;
        if( !c.ref.isEmpty() )
        {
            QString rc = c.refComp.isEmpty() ? c.comp : c.refComp;
            if( !compByUid( rc ) ) return "no part "+rc;
            if( !findPin( rc, c.ref ) ) return "no pin "+c.ref+" on "+rc;
            if( c.sig == "curr" ) return "a current is into one pin";
        }
    }
    if( c.kind == "window" && !( c.low < c.high ) ) return "the window's low must be below its high";
    if( c.kind != "window" && c.kind != "edge" ) return "unknown condition "+c.kind;
    return "";
}

//------------------------------------------------------------------------------------------------
// Texts (the rows of the Triggers tab, Messages, the CLI)

static QString compName( Component* c )
{
    QString l = c->idLabel();
    return l.isEmpty() ? c->getUid() : l;
}

static int pinNumber( Component* c, Pin* p )
{
    std::vector<Pin*> pins = c->getPins();
    for( int i=0; i<(int)pins.size(); ++i ) if( pins[i] == p ) return i+1;
    return 0;
}

// The usual name of an unlabelled pin of a built-in part (transistors: by name only, §13)
static QString knownName( Component* c, Pin* p )
{
    static const QHash<QString, QHash<QString, QString>> names = {
        { "Mosfet",  { {"Dren","D"}, {"Sour","S"}, {"Gate","G"} } },
        { "JFET",    { {"Dren","D"}, {"Sour","S"}, {"Gate","G"} } },
        { "BJT",     { {"collector","C"}, {"emiter","E"}, {"base","B"} } },
        { "SCR",     { {"lPin","A"}, {"rPin","K"}, {"gPin","G"} } },
        { "Triac",   { {"lPin","MT1"}, {"rPin","MT2"}, {"gPin","G"} } },
        { "Diode",   { {"lPin","A"}, {"rPin","K"} } },
        { "Zener",   { {"lPin","A"}, {"rPin","K"} } },
        { "Led",     { {"lPin","A"}, {"rPin","K"} } },
    };
    auto t = names.find( c->itemType() );
    if( t == names.end() ) return "";
    return t->value( Triggers::pinName( p, c ) );
}

static QString pinShort( Component* c, Pin* p )   // "G", "VDD", "1"
{
    QString k = knownName( c, p );
    if( !k.isEmpty() ) return k;
    QString l = p->getLabelText().trimmed();
    return l.isEmpty() ? QString::number( pinNumber( c, p ) ) : l;
}

static QString pinLabel( Component* c, Pin* p )   // "pin 3 VDD", "pin 1", "pin G"
{
    QString k = knownName( c, p );
    if( !k.isEmpty() ) return "pin "+k;
    QString name = p->getLabelText().trimmed();
    int n = pinNumber( c, p );
    if( name.isEmpty() ) return "pin "+QString::number( n );
    return "pin "+QString::number( n )+" "+name;
}

static QString fmt( double v, const QString& unit )
{
    if( v == 0 ) return "0 "+unit;
    static const char* pre[] = { "p","n","µ","m","","k","M","G","T" };
    int e = (int)std::floor( std::log10( std::fabs( v ) )/3 );
    e = std::max( -4, std::min( 4, e ) );
    return QString::number( v/std::pow( 1000, e ), 'g', 6 )+" "+pre[e+4]+unit;
}

static QString timeFmt( uint64_t ps ) { return KeyFrames::timeText( ps ); }

QString Triggers::watchText( const Trigger& t )
{
    const TrigCond& c = t.cond;
    if( c.sig == "scope" )
    {
        Oscope* o = dynamic_cast<Oscope*>( compByUid( c.comp ) );
        if( !o ) return c.comp+" (scope)";
        int ch = o->trigger();
        return compName( o )+( ch >= 0 && ch < 4 ? " Ch"+QString::number( ch+1 ) : " (trigger off)" );
    }
    if( c.sig == "state" )
    {
        Component* comp = compByUid( c.comp );
        QString n = comp ? compName( comp ) : c.comp;
        return c.block.isEmpty() ? n+" state" : n+" "+c.block+" (state)";
    }
    if( !c.node.isEmpty() )
    {
        QString uid, pin;
        splitNode( c.node, uid, pin );
        Component* comp = compByUid( uid );
        Pin* p = findPin( uid, pin );
        if( !comp || !p ) return "node "+c.node;
        return "node at "+compName( comp )+" "+pinLabel( comp, p );
    }
    Component* comp = compByUid( c.comp );
    Pin* p = comp ? findPin( c.comp, c.pin ) : nullptr;
    if( !comp || !p ) return c.comp+"."+c.pin;
    bool twoPin = comp->getPins().size() == 2;
    int n = pinNumber( comp, p );
    if( c.sig == "curr" )
    {
        if( twoPin ){
            std::vector<Pin*> ps = comp->getPins();
            QString a = pinShort( comp, ps[0] ), b = pinShort( comp, ps[1] );
            return compName( comp )+( n == 1 ? " current ("+a+"→"+b+")" : " current ("+b+"→"+a+")" );
        }
        return compName( comp )+" "+pinLabel( comp, p )+" (I)";
    }
    if( c.ref.isEmpty() ) return compName( comp )+" "+pinLabel( comp, p );

    QString rcUid = c.refComp.isEmpty() ? c.comp : c.refComp;
    Component* rc = compByUid( rcUid );
    Pin* r = rc ? findPin( rcUid, c.ref ) : nullptr;
    if( !rc || !r ) return compName( comp )+" "+pinLabel( comp, p )+" – "+rcUid+"."+c.ref;
    if( rc != comp ) return compName( comp )+" "+pinLabel( comp, p )+" – "+compName( rc )+" "+pinLabel( rc, r );

    auto shortName = [&]( Pin* x ){ return pinShort( comp, x ); };
    QString a = shortName( p ), b = shortName( r );
    QString known;                        // A familiar name for a transistor's pin pair
    QString ab = (a+b).toUpper();
    if( ab == "GS" || ab == "DS" || ab == "GD" || ab == "BE" || ab == "CE" || ab == "BC" ) known = "V"+a.toLower()+b.toLower();
    return compName( comp )+" pins "+a+"–"+b+" ("+( known.isEmpty() ? "V" : known )+")";
}

QString Triggers::condText( const Trigger& t )
{
    const TrigCond& c = t.cond;
    if( c.sig == "scope" )
    {
        Oscope* o = dynamic_cast<Oscope*>( compByUid( c.comp ) );
        if( !o ) return "↑";
        return o->trigAuto() ? "↑ auto" : "↑ "+fmt( o->trigLevel(), "V" );
    }
    if( c.sig == "state" )
    {
        if( c.change ) return "any change";
        if( c.sev >= 0 ) return "≥ "+levelName( c.sev );
        return ( c.from.isEmpty() ? "any" : c.from )+" → "+( c.to.isEmpty() ? "any" : c.to );
    }
    QString u = ( c.sig == "curr" ) ? "A" : "V";
    if( c.kind == "window" )
        return QString( c.outside ? "outside " : "inside " )+fmt( c.low, "" ).trimmed()+" … "+fmt( c.high, u );
    QString arrow = c.edge > 0 ? "↑ " : c.edge < 0 ? "↓ " : "↕ ";
    return arrow+fmt( c.level, u );
}

QString Triggers::actionText( const Trigger& t )
{
    QString kf;
    if( t.keyframe ) kf = t.before ? "key frame −"+timeFmt( t.before ) : "key frame";
    if( t.pause && t.keyframe ) return kf+", pause";
    if( t.pause ) return "pause";
    if( t.keyframe ) return kf;
    return "count only";
}

QString Triggers::modeText( const Trigger& t )
{
    if( t.single ) return "single";
    QString m = "normal";
    if( t.holdoff ) m += " · "+timeFmt( t.holdoff );
    if( t.max ) m += " · max "+QString::number( t.max );
    return m;
}

QString Triggers::lastStopText()
{
    Trigger* t = find( m_stopId );
    if( !t ) return "";
    return describe( *t )+" at "+timeFmt( m_stopTime );
}

//------------------------------------------------------------------------------------------------
// Edits

void Triggers::edited( const QString& undoXml )
{
    if( Circuit* circ = Circuit::self() ) circ->saveCompChange( kUndoId, "triggers", undoXml );
    m_dirty = true;
    emit changed();
}

int Triggers::add( Trigger t )
{
    QString undo = toXml();
    t.id = m_nextId++;
    t.hits.clear();
    t.selfDisarmedAt = 0;
    m_list.append( t );
    edited( undo );
    return t.id;
}

bool Triggers::set( const Trigger& t )
{
    Trigger* old = find( t.id );
    if( !old ) return false;
    QString undo = toXml();
    QVector<uint64_t> hits = old->hits;
    bool sameCond = old->cond == t.cond;
    *old = t;
    old->hits = sameCond ? hits : QVector<uint64_t>();   // Another signal: its hits are not these
    if( t.armed ) old->selfDisarmedAt = 0;
    edited( undo );
    return true;
}

int Triggers::remove( QList<int> ids )
{
    QString undo = toXml();
    int n = 0;
    for( int i=m_list.size()-1; i>=0; --i )
        if( ids.contains( m_list.at( i ).id ) ){ m_list.removeAt( i ); n++; }
    if( n ) edited( undo );
    return n;
}

int Triggers::setArmed( QList<int> ids, bool armed )
{
    QString undo = toXml();
    int n = 0;
    for( Trigger& t : m_list )
    {
        if( !ids.contains( t.id ) || t.armed == armed ) continue;
        t.armed = armed;
        t.selfDisarmedAt = 0;
        n++;
    }
    if( n ) edited( undo );
    return n;
}

void Triggers::clearHits( QList<int> ids )
{
    for( Trigger& t : m_list ) if( ids.contains( t.id ) ) t.hits.clear();
    for( Check& c : m_checks ) if( ids.contains( c.id ) ){ c.hits = 0; c.holdEnd = 0; }
    emit changed();
}

//------------------------------------------------------------------------------------------------
// Saved in the .sim1 (§9)

static QString secs( uint64_t ps ) { return QString::number( ps*1e-12, 'g', 12 ); }
static uint64_t fromSecs( const QString& s ) { return (uint64_t)std::llround( s.toDouble()*1e12 ); }
static QString num( double v ) { return QString::number( v, 'g', 12 ); }
static QString esc( const QString& s ) { return s.toHtmlEscaped(); }

QString Triggers::condXml( const TrigCond& c )
{
    QString x = "<cond sig=\""+c.sig+"\"";
    if( c.sig == "scope" ) return x+" comp=\""+esc( c.comp )+"\"/>";   // Channel and level: the scope's own
    if( c.sig == "state" )
    {
        x += " comp=\""+esc( c.comp )+"\"";
        if( !c.block.isEmpty() ) x += " block=\""+esc( c.block )+"\"";
        if( c.change ) x += " change=\"1\"";
        else if( c.sev >= 0 ) x += " sev=\""+levelName( c.sev )+"\"";
        else x += " from=\""+esc( c.from.isEmpty() ? "any" : c.from )+"\" to=\""+esc( c.to.isEmpty() ? "any" : c.to )+"\"";
        return x+"/>";
    }
    if( !c.node.isEmpty() ) x += " node=\""+esc( c.node )+"\"";
    else{
        x += " comp=\""+esc( c.comp )+"\" pin=\""+esc( c.pin )+"\"";
        if( !c.ref.isEmpty() ){
            if( !c.refComp.isEmpty() && c.refComp != c.comp ) x += " refcomp=\""+esc( c.refComp )+"\"";
            x += " ref=\""+esc( c.ref )+"\"";
        }
    }
    if( c.kind == "window" )
        x += QString(" window=\"")+( c.outside ? "out" : "in" )+"\" low=\""+num( c.low )+"\" high=\""+num( c.high )+"\"";
    else
        x += QString(" edge=\"")+( c.edge > 0 ? "rise" : c.edge < 0 ? "fall" : "either" )+"\" level=\""+num( c.level )+"\"";
    if( c.hyst >= 0 ) x += " hyst=\""+num( c.hyst )+"\"";
    return x+"/>";
}

QString Triggers::toXml()
{
    if( m_list.isEmpty() ) return "";
    QString x = "\n<triggers>\n";
    for( const Trigger& t : m_list )
    {
        x += "<trigger id=\""+QString::number( t.id )+"\" armed=\""+( t.armed ? "1" : "0" )+"\"";
        if( t.pause ) x += " pause=\"1\"";
        if( t.keyframe ) x += " keyframe=\"1\"";
        if( t.before ) x += " before=\""+secs( t.before )+"\"";
        x += QString(" mode=\"")+( t.single ? "single" : "normal" )+"\"";
        if( t.holdoff ) x += " holdoff=\""+secs( t.holdoff )+"\"";
        if( t.max ) x += " max=\""+QString::number( t.max )+"\"";
        if( !t.name.isEmpty() ) x += " name=\""+esc( t.name )+"\"";
        x += ">"+condXml( t.cond )+"</trigger>\n";
    }
    return x+"</triggers>\n";
}

void Triggers::restoreXml( const QString& xml )
{
    QList<Trigger> old = m_list;          // Hits stay with the triggers that come back
    m_list.clear();
    m_nextId = 1;
    QDomDocument dom;
    if( !xml.trimmed().isEmpty() && dom.setContent( xml ) )
    {
        QDomElement root = dom.documentElement();
        for( QDomElement e = root.firstChildElement("trigger"); !e.isNull(); e = e.nextSiblingElement("trigger") )
        {
            Trigger t;
            t.id       = e.attribute("id").toInt();
            t.armed    = e.attribute("armed", "1") != "0";
            t.pause    = e.attribute("pause") == "1";
            t.keyframe = e.attribute("keyframe") == "1";
            t.before   = fromSecs( e.attribute("before", "0") );
            t.single   = e.attribute("mode", "single") != "normal";
            t.holdoff  = fromSecs( e.attribute("holdoff", "0") );
            t.max      = e.attribute("max", "0").toInt();
            t.name     = e.attribute("name");
            QDomElement ce = e.firstChildElement("cond");
            TrigCond& c = t.cond;
            c.sig     = ce.attribute("sig", "volt");
            c.comp    = ce.attribute("comp");
            c.pin     = ce.attribute("pin");
            c.refComp = ce.attribute("refcomp");
            c.ref     = ce.attribute("ref");
            if( c.ref == "gnd" ) c.ref.clear();
            c.node    = ce.attribute("node");
            if( c.sig == "scope" ) c.kind = "scope";
            else if( c.sig == "state" ){
                c.kind   = "state";
                c.block  = ce.attribute("block");
                c.change = ce.attribute("change") == "1";
                c.sev    = ce.hasAttribute("sev") ? levelOf( ce.attribute("sev") ) : -1;
                c.from   = ce.attribute("from");
                c.to     = ce.attribute("to");
                if( c.from.compare( "any", Qt::CaseInsensitive ) == 0 ) c.from.clear();
                if( c.to.compare( "any", Qt::CaseInsensitive ) == 0 ) c.to.clear();
            }else if( ce.hasAttribute("window") ){
                c.kind = "window";
                c.outside = ce.attribute("window") != "in";
                c.low  = ce.attribute("low").toDouble();
                c.high = ce.attribute("high").toDouble();
            }else{
                c.kind = "edge";
                QString ed = ce.attribute("edge", "rise");
                c.edge = ed == "fall" ? -1 : ( ed == "either" || ed == "both" ) ? 0 : 1;
                c.level = ce.attribute("level").toDouble();
            }
            c.hyst = ce.hasAttribute("hyst") ? ce.attribute("hyst").toDouble() : -1;
            if( t.id <= 0 || find( t.id ) ) t.id = m_nextId;
            for( const Trigger& o : old ) if( o.id == t.id && o.cond == t.cond ) t.hits = o.hits;
            m_nextId = std::max( m_nextId, t.id+1 );
            m_list.append( t );
        }
    }
    m_dirty = true;
    emit changed();
}

void Triggers::fromDoc( const QString& doc )
{
    int start = doc.indexOf("<triggers>");
    int end   = doc.indexOf("</triggers>", start );
    if( start < 0 || end < 0 ){ clear(); return; }
    restoreXml( doc.mid( start, end+11-start ) );
    TRIG_LOG << m_list.size() << "trigger(s) loaded";
}

void Triggers::clear()
{
    m_list.clear();
    m_nextId = 1;
    m_dirty = true;
    emit changed();
}

//------------------------------------------------------------------------------------------------
// Part states (§2): a built-in part's StateLog, a scripted core, or a block (core) of a subcircuit

QString Triggers::levelName( int level )
{
    switch( level ){
        case Component::stOff:   return "OFF";
        case Component::stOn:    return "ON";
        case Component::stWait:  return "WAIT";
        case Component::stFault: return "FAULT";
    }
    return "";
}

int Triggers::levelOf( const QString& name )
{
    QString n = name.toUpper();
    if( n == "OFF" ) return Component::stOff;
    if( n == "ON" ) return Component::stOn;
    if( n == "WAIT" ) return Component::stWait;
    if( n == "FAULT" ) return Component::stFault;
    return -1;
}

static const void* coreSource( Component* c )   // A scripted core (the blocks of a subcircuit, as the badges)
{
    if( Mcu* mcu = dynamic_cast<Mcu*>( c ) )
        if( ScriptCpu* cpu = dynamic_cast<ScriptCpu*>( mcu->cpu() ) ) return cpu;
    return nullptr;
}

static const void* ownSource( Component* c )
{
    if( StateLog* log = c->stateLogAny() ) return log;
    if( Mcu* mcu = dynamic_cast<Mcu*>( c ) )
        if( ScriptCpu* cpu = dynamic_cast<ScriptCpu*>( mcu->cpu() ) ) return cpu;
    return nullptr;
}

const void* Triggers::stateSource( Component* comp, const QString& block )
{
    if( !comp ) return nullptr;
    if( block.isEmpty() || block == "state" ) return ownSource( comp );
    if( SubCircuit* sub = dynamic_cast<SubCircuit*>( comp ) )
        for( Component* inner : *sub->compList() )
            if( StatusView::blockName( inner ).compare( block, Qt::CaseInsensitive ) == 0 )
                if( const void* s = coreSource( inner ) ) return s;
    return nullptr;
}

QStringList Triggers::stateBlocks( Component* comp )
{
    QStringList l;
    if( ownSource( comp ) ) l.append("");
    if( SubCircuit* sub = dynamic_cast<SubCircuit*>( comp ) )
        for( Component* inner : *sub->compList() )
            if( coreSource( inner ) ) l.append( StatusView::blockName( inner ) );
    return l;
}

static void currentState( const void* src, bool isLog, QString& text, int& level )
{
    StateLog::Event e = isLog ? ((StateLog*)src)->current() : ((ScriptCpu*)src)->status();
    text = e.text;
    level = e.level;
}

bool Triggers::Check::fireState( const Ev& e )
{
    QString prevText = curText;
    int prevLevel = curLevel;
    curText  = e.text;
    curLevel = e.level;
    if( change ) return true;
    if( sev >= 0 ) return prevLevel < sev && e.level >= sev;
    bool f = from.isEmpty() || prevText.compare( from, Qt::CaseInsensitive ) == 0;
    bool t = to.isEmpty()   || e.text.compare( to, Qt::CaseInsensitive ) == 0;
    return f && t;
}

void Triggers::stateReport( const void* source, const QString& text, int level, uint64_t time )   // Worker
{
    for( Check& c : m_checks )
        if( c.state && c.src == source && !c.done ) c.pending.append({ text, level, time });
}

//------------------------------------------------------------------------------------------------
// The run (§6, §10)

bool Triggers::compileOne( const Trigger& t, Check& k )
{
    const TrigCond& c = t.cond;
    if( !check( t ).isEmpty() ) return false;
    k.id       = t.id;
    k.pause    = t.pause;
    k.keyframe = t.keyframe;
    k.single   = t.single;
    k.holdoff  = t.single ? 0 : t.holdoff;
    k.max      = t.single ? 1 : t.max;
    k.hits     = t.hits.size();
    if( c.sig == "scope" )             // §4.5: a rising edge of the scope's trigger channel at its level
    {
        Oscope* o = dynamic_cast<Oscope*>( compByUid( c.comp ) );
        std::vector<Pin*> pins = o->getPins();
        int ch = o->trigger();
        if( (int)pins.size() < 5 ) return false;
        k.p0 = pins[ch];
        k.p1 = pins[4];                   // The scope's ground lead
        k.edge = 1;
        if( o->trigAuto() ) k.scope = (OscopeChannel*)o->channel( ch );
        else{
            k.level = o->trigLevel();
            k.h = o->filter() > 0 ? o->filter() : std::max( 0.01*std::fabs( k.level ), 1e-3 );
        }
        k.n0 = k.p0->getEnode();
        k.n1 = k.p1->getEnode();
        k.direct = k.n0 && k.n1;
        return true;
    }
    if( c.sig == "state" )
    {
        Component* comp = compByUid( c.comp );
        k.state  = true;
        k.src    = stateSource( comp, c.block );
        k.change = c.change;
        k.sev    = c.sev;
        k.from   = c.from;
        k.to     = c.to;
        bool isLog = comp->stateLogAny() == k.src;   // A StateLog, else a scripted core
        if( SubCircuit* sub = dynamic_cast<SubCircuit*>( comp ) )
            for( Component* inner : *sub->compList() ) if( inner->stateLogAny() == k.src ) isLog = true;
        currentState( k.src, isLog, k.curText, k.curLevel );
        return k.src != nullptr;
    }
    if( !c.node.isEmpty() ){
        QString uid, pin;
        splitNode( c.node, uid, pin );
        k.p0 = findPin( uid, pin );
    }else{
        k.p0 = findPin( c.comp, c.pin );
        if( !c.ref.isEmpty() ) k.p1 = findPin( c.refComp.isEmpty() ? c.comp : c.refComp, c.ref );
    }
    k.curr    = c.sig == "curr";
    k.window  = c.kind == "window";
    k.edge    = c.edge;
    k.outside = c.outside;
    k.level   = c.level;
    k.lo      = c.low;
    k.hi      = c.high;
    k.h       = c.hysteresis();
    if( k.window ) k.h = std::min( k.h, 0.25*( c.high-c.low ) );   // A narrow band keeps an inside
    if( k.p0 ){
        k.n0 = k.p0->getEnode();
        k.n1 = k.p1 ? k.p1->getEnode() : nullptr;
        k.direct = !k.curr && k.n0 && ( !k.p1 || k.n1 );
    }
    return k.p0 != nullptr;
}

void Triggers::compile( bool derive )
{
    QHash<int, Check> old;
    for( const Check& c : m_checks ) old.insert( c.id, c );
    m_checks.clear();
    bool states = false;
    for( const Trigger& t : m_list )
    {
        if( !t.armed ) continue;
        Check k;
        if( !compileOne( t, k ) ){
            qWarning().noquote() << "[triggers] Trigger "+QString::number( t.id )+" ("+describe( t )+") skipped: "+check( t );
            continue;
        }
        auto o = old.find( t.id );         // The same check as before (an edit of another row): keep its state
        if( !derive && o != old.end() && o->p0 == k.p0 && o->p1 == k.p1 && o->curr == k.curr && o->window == k.window
            && o->edge == k.edge && o->outside == k.outside && o->level == k.level && o->lo == k.lo
            && o->hi == k.hi && o->h == k.h && o->src == k.src )
        {
            k.armR = o->armR; k.armF = o->armF; k.armW = o->armW;
            k.holdEnd = o->holdEnd;
            k.pending = o->pending;
        }
        if( derive )                       // A keyframe was loaded: its saved state, else from the values
        {
            auto l = m_loaded.find( t.id );
            if( l != m_loaded.end() ){
                k.armR = l->armR; k.armF = l->armF; k.armW = l->armW; k.holdEnd = l->holdEnd;
                if( k.state ){ k.curText = l->text; k.curLevel = l->level; }
            }
            else if( !k.state ) k.needDerive = true;
        }
        if( k.state ) states = true;
        m_checks.append( k );
    }
    m_loaded.clear();
    StateLog::setWatch( states ? this : nullptr );
    if( m_sim ) m_sim->setWatching( !m_checks.isEmpty() );
    TRIG_LOG << "compiled" << m_checks.size() << "check(s)" << ( derive ? "(state from the keyframe or the values)" : "" );
}

void Triggers::simStarted()
{
    m_sim = Simulator::self();
    m_sim->addToUpdateList( this );
    for( Trigger& t : m_list ){ t.hits.clear(); t.selfDisarmedAt = 0; }   // Hits are this run's
    m_checks.clear();
    m_fired.clear();
    m_countNew.clear();
    m_loaded.clear();
    m_firstDone = false;
    m_dirty = false;
    m_keepUntil = 0;
    m_preBusy = false;
    m_wasPaused = m_sim->isPaused();
    compile( false );
    emit changed();
}

void Triggers::simStopped()
{
    updateStep();       // Hits of the last frame
    summary();
    StateLog::setWatch( nullptr );
    m_checks.clear();
    m_fired.clear();
    m_dirty = false;
}

bool Triggers::hit( Check& c, uint64_t t, double v, bool quiet )
{
    if( quiet ) return false;
    if( c.holdEnd && t < c.holdEnd ) return false;
    c.hits++;
    c.holdEnd = c.holdoff ? t+c.holdoff : 0;
    if( c.max && c.hits >= c.max ) c.done = true;
    m_fired.append({ c.id, t, v });
    return c.pause || c.keyframe;            // A key frame is taken at this step too
}

void Triggers::stepDone()   // Worker: after each solved step
{
    Simulator* sim = m_sim;
    uint64_t t = sim->circTime();
    bool quiet = t <= sim->trigQuiet();   // A replay of going back: track, don't fire
    bool stop = false;
    for( Check& c : m_checks )
    {
        if( c.state )
        {
            for( const Check::Ev& e : c.pending )
            {
                if( c.done ) break;
                bool first = c.curLevel < 0;          // Its first report is its start state
                bool f = c.fireState( e );
                if( f && m_firstDone && !first ) stop |= hit( c, e.time, e.level, quiet );   // Start states don't fire
            }
            c.pending.clear();
            continue;
        }
        if( c.done ) continue;
        if( c.scope && !c.scope->autoLevel( &c.level, &c.h ) ) continue;   // Auto: the scope's level now
        double v = c.value();
        if( c.needDerive ){ c.needDerive = false; c.derive( v ); continue; }
        if( c.eval( v ) ) stop |= hit( c, t, v, quiet );
    }
    m_firstDone = true;
    if( stop ) sim->trigStop();
}

void Triggers::updateStep()   // GUI thread, worker idle
{
    Simulator* sim = Simulator::self();
    if( !m_sim || sim != m_sim ) return;

    bool any = false;
    int stopId = -1;
    uint64_t stopTime = 0;
    QList<int> keyIds;                       // Key frame actions of this step
    for( const Hit& h : m_fired )
    {
        Trigger* t = find( h.id );
        if( !t ) continue;
        any = true;
        if( t->hits.size() < 1000000 ) t->hits.append( h.time );
        for( const Check& c : m_checks )
            if( c.id == h.id && c.done && c.hits == t->hits.size() && t->armed ){
                t->armed = false;                // Single, or its max reached: its box clears
                t->selfDisarmedAt = h.time;
            }
        if( t->pause ){ stopId = t->id; stopTime = h.time; }
        if( t->keyframe ) keyIds.append( t->id );
        if( t->pause || t->keyframe )
            qInfo().noquote() << "Trigger: "+describe( *t )+" at "+timeFmt( h.time )+( t->pause ? " (paused)" : "" );
        else m_countNew.append( t->id );
    }
    m_fired.clear();

    if( sim->trigStopped() )
    {
        sim->clearTrigStop();
        bool pausing = stopId >= 0;
        if( pausing ){
            m_stopId = stopId;
            m_stopTime = stopTime;
            m_stopCount++;
        }
        bool running = sim->isRunning() && !sim->isPaused();
        for( int id : keyIds )
            if( Trigger* t = find( id ) ) keyFrame( t, sim->circTime(), running && !pausing );
        if( pausing && sim->isRunning() && !sim->isPaused() ) CircuitWidget::self()->pauseCirc();
        if( pausing ) emit fired( m_stopId );
    }
    if( m_dirty && sim->isRunning() ){ m_dirty = false; compile( false ); }

    bool paused = sim->isPaused();
    if( paused && !m_wasPaused ) summary();
    m_wasPaused = paused;
    if( any ) emit changed();
}

// §7: a key frame at the step that fired; with "before Δ", Δ earlier: back with the history (the
// replay fires nothing), the key frame saved there, then forward again to the trigger's step (the
// same computation, so the same state) and on as before
void Triggers::keyFrame( Trigger* t, uint64_t hitTime, bool resumeAfter )
{
    QString note = t->name.isEmpty() ? describe( *t ) : t->name;
    Simulator* sim = Simulator::self();
    auto failed = [&]( QString why ){
        qWarning().noquote() << "[triggers] Trigger "+describe( *t )+": no key frame ("+why+"), paused instead";
        if( sim->isRunning() && !sim->isPaused() ) CircuitWidget::self()->pauseCirc();
    };
    uint64_t before = t->before;
    if( before && ( m_preBusy || SimSkip::self()->active() ) ){
        qWarning().noquote() << "[triggers] Trigger "+describe( *t )+": a key frame −Δ is being made, this one is saved at the trigger";
        before = 0;
    }
    if( before && !SimHistory::self()->usable() ){
        qWarning().noquote() << "[triggers] Trigger "+describe( *t )+": no history (settings history), the key frame is saved at the trigger";
        before = 0;
    }
    if( !before )
    {
        QString err;
        if( !KeyFrames::self()->save( note, &err, true ) ) failed( err );
        return;
    }
    uint64_t first = SimHistory::self()->firstTime();
    uint64_t back = ( hitTime > first ) ? std::min( before, hitTime-first ) : 0;
    if( back < before )
        qInfo().noquote() << "Trigger: "+describe( *t )+": the history holds only "+timeFmt( back )+" before it";
    if( back == 0 ){
        QString err;
        if( !KeyFrames::self()->save( note, &err, true ) ) failed( err );
        return;
    }
    m_preBusy = true;
    m_keepUntil = hitTime;
    uint64_t stopAt = sim->stopAt();          // A sim run --for, a range play: their end, put back after
    QString err = SimSkip::self()->startBack( back );
    if( !err.isEmpty() ){ m_preBusy = false; m_keepUntil = 0; failed( err ); return; }
    QPointer<Triggers> self( this );
    SimSkip::self()->whenDone( [self, note, hitTime, resumeAfter, stopAt]( bool ok, QString msg )
    {
        if( !self ) return;
        Simulator* sim = Simulator::self();
        if( !ok ){
            self->m_preBusy = false; self->m_keepUntil = 0;
            qWarning().noquote() << "[triggers] key frame before the trigger: "+msg;
            return;
        }
        QString err;
        if( !KeyFrames::self()->save( note, &err, true ) )
            qWarning().noquote() << "[triggers] key frame before the trigger: "+err;
        sim->setTrigQuiet( hitTime );        // On to the trigger again, without firing on the way
        err = SimSkip::self()->start( hitTime-sim->circTime() );
        if( !err.isEmpty() ){
            self->m_preBusy = false; self->m_keepUntil = 0;
            qWarning().noquote() << "[triggers] back to the trigger: "+err;
            return;
        }
        SimSkip::self()->whenDone( [self, resumeAfter, stopAt]( bool, QString )
        {
            if( !self ) return;
            self->m_preBusy = false;
            self->m_keepUntil = 0;
            Simulator* sim = Simulator::self();
            if( stopAt > sim->circTime() ) sim->setStopAt( stopAt );
            if( resumeAfter && sim->isRunning() && sim->isPaused() ) CircuitWidget::self()->pauseCirc();   // Resumes
        });
    });
}

void Triggers::restored()   // GUI thread: a keyframe was loaded
{
    Simulator* sim = Simulator::self();
    if( !m_sim || sim != m_sim ) return;
    m_fired.clear();
    sim->clearTrigStop();
    uint64_t keep = std::max( { sim->circTime(), sim->trigQuiet(), m_keepUntil } );   // A replay follows
    for( Trigger& t : m_list )
    {
        while( !t.hits.isEmpty() && t.hits.last() > keep ) t.hits.removeLast();
        if( t.selfDisarmedAt > keep ){ t.armed = true; t.selfDisarmedAt = 0; }
    }
    m_checks.clear();                     // The checks start again from the definitions
    compile( true );
    m_firstDone = true;
    m_dirty = false;
    emit changed();
}

// Keyframes (§10): each check's running state, keyed by trigger id, so a key frame loads whatever
// triggers were added, edited or deleted since: one deleted is skipped, one added starts from its
// signal's value
void Triggers::ioState( StateIO& s )
{
    uint32_t n = m_checks.size();
    s.io( n );
    if( s.saving() )
    {
        for( Check& c : m_checks )
        {
            int32_t id = c.id;
            uint8_t f = ( c.armR ? 1 : 0 ) | ( c.armF ? 2 : 0 ) | ( c.armW ? 4 : 0 ) | ( c.done ? 8 : 0 );
            uint64_t hold = c.holdEnd;
            int32_t level = c.curLevel;
            s.io( id ); s.io( f ); s.io( hold ); s.io( level );
            s.ioString( c.curText );
        }
        return;
    }
    m_loaded.clear();
    if( n > 100000 ){ s.fail(); return; }
    for( uint32_t i=0; i<n && !s.overflow(); ++i )
    {
        int32_t id = 0; uint8_t f = 0; uint64_t hold = 0; int32_t level = -1; QString text;
        s.io( id ); s.io( f ); s.io( hold ); s.io( level );
        s.ioString( text );
        m_loaded.insert( id, { bool( f & 1 ), bool( f & 2 ), bool( f & 4 ), bool( f & 8 ), hold, level, text } );
    }
}

void Triggers::summary()   // Count-only triggers: one line each at a pause or stop (§7)
{
    if( m_countNew.isEmpty() ) return;
    QList<int> ids;
    for( int id : m_countNew ) if( !ids.contains( id ) ) ids.append( id );
    m_countNew.clear();
    for( int id : ids )
    {
        Trigger* t = find( id );
        if( !t || t->hits.isEmpty() ) continue;
        int n = t->hits.size();
        qInfo().noquote() << "Trigger: "+describe( *t )+": "+QString::number( n )+( n == 1 ? " hit" : " hits" )
                             +", the last at "+timeFmt( t->hits.last() );
    }
}

//------------------------------------------------------------------------------------------------
// For the editors

Component* Triggers::comp( const QString& uid ) { return compByUid( uid ); }
QString Triggers::pinText( Component* c, Pin* p ) { return pinLabel( c, p ); }
QString Triggers::partName( Component* c ) { return compName( c ); }

QStringList Triggers::statesSeen( Component* c, const QString& block )
{
    QStringList l;
    const void* src = stateSource( c, block );
    if( !src ) return l;
    bool isLog = c->stateLogAny() == src;
    if( SubCircuit* sub = dynamic_cast<SubCircuit*>( c ) )
        for( Component* inner : *sub->compList() ) if( inner->stateLogAny() == src ) isLog = true;
    QList<StateLog::Event> h = isLog ? ((StateLog*)src)->history() : ((ScriptCpu*)src)->statusHistory();
    for( const StateLog::Event& e : h ) if( !e.text.isEmpty() && !l.contains( e.text ) ) l.append( e.text );
    return l;
}

QList<int> Triggers::watchingPart( const QString& uid )
{
    QList<int> ids;
    for( const Trigger& t : m_list )
    {
        const TrigCond& c = t.cond;
        QString nodeUid;
        if( !c.node.isEmpty() ) nodeUid = c.node.left( c.node.lastIndexOf(':') );
        if( c.comp == uid || c.refComp == uid || nodeUid == uid ) ids.append( t.id );
    }
    return ids;
}

int Triggers::scopeTrigger( const QString& uid )
{
    for( const Trigger& t : m_list ) if( t.cond.sig == "scope" && t.cond.comp == uid ) return t.id;
    return 0;
}
