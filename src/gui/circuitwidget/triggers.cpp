/***************************************************************************
 *   Circuit triggers (specs/triggers.html)                                *
 ***( see copyright.txt file at root folder )*******************************/

#include <cmath>
#include <QDebug>
#include <QDomDocument>
#include <QHash>

#include "triggers.h"
#include "circuit.h"
#include "circuitwidget.h"
#include "component.h"
#include "keyframes.h"
#include "node.h"
#include "pin.h"
#include "e-node.h"
#include "simulator.h"

#define TRIG_LOG qDebug() << "[triggers]"

static const char* kUndoId = "__triggers__";   // The undo step's "component" (Circuit::restoreState)

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
        && level == o.level && low == o.low && high == o.high && hyst == o.hyst;
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
    bool isNum = false;
    int n = name.toInt( &isNum );             // A pin number: 1-based, as on the package
    if( isNum && n >= 1 && n <= (int)pins.size() ) return pins[n-1];
    return nullptr;
}

static void splitNode( const QString& node, QString& uid, QString& pin )
{
    int i = node.lastIndexOf(':');
    uid = node.left( i );
    pin = node.mid( i+1 );
}

QString Triggers::check( const Trigger& t )
{
    const TrigCond& c = t.cond;
    if( c.sig != "volt" && c.sig != "curr" ) return "unknown signal "+c.sig;
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

static QString pinLabel( Component* c, Pin* p )   // "pin 3 VDD", "pin 1", "pin G"
{
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
        if( twoPin ) return compName( comp )+( n == 1 ? " current (1→2)" : " current (2→1)" );
        return compName( comp )+" "+pinLabel( comp, p )+" (I)";
    }
    if( c.ref.isEmpty() ) return compName( comp )+" "+pinLabel( comp, p );

    QString rcUid = c.refComp.isEmpty() ? c.comp : c.refComp;
    Component* rc = compByUid( rcUid );
    Pin* r = rc ? findPin( rcUid, c.ref ) : nullptr;
    if( !rc || !r ) return compName( comp )+" "+pinLabel( comp, p )+" – "+rcUid+"."+c.ref;
    if( rc != comp ) return compName( comp )+" "+pinLabel( comp, p )+" – "+compName( rc )+" "+pinLabel( rc, r );

    auto shortName = [&]( Pin* x ){ QString l = x->getLabelText().trimmed();
                                    return l.isEmpty() ? QString::number( pinNumber( comp, x ) ) : l; };
    QString a = shortName( p ), b = shortName( r );
    QString known;                        // A familiar name for a transistor's pin pair
    QString ab = (a+b).toUpper();
    if( ab == "GS" || ab == "DS" || ab == "GD" || ab == "BE" || ab == "CE" || ab == "BC" ) known = "V"+a.toLower()+b.toLower();
    return compName( comp )+" pins "+a+"–"+b+" ("+( known.isEmpty() ? "V" : known )+")";
}

QString Triggers::condText( const Trigger& t )
{
    const TrigCond& c = t.cond;
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
            if( ce.hasAttribute("window") ){
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
// The run (§6, §10)

bool Triggers::compileOne( const Trigger& t, Check& k )
{
    const TrigCond& c = t.cond;
    if( !check( t ).isEmpty() ) return false;
    if( !c.node.isEmpty() ){
        QString uid, pin;
        splitNode( c.node, uid, pin );
        k.p0 = findPin( uid, pin );
    }else{
        k.p0 = findPin( c.comp, c.pin );
        if( !c.ref.isEmpty() ) k.p1 = findPin( c.refComp.isEmpty() ? c.comp : c.refComp, c.ref );
    }
    k.id      = t.id;
    k.curr    = c.sig == "curr";
    k.window  = c.kind == "window";
    k.edge    = c.edge;
    k.outside = c.outside;
    k.level   = c.level;
    k.lo      = c.low;
    k.hi      = c.high;
    k.h       = c.hysteresis();
    if( k.window ) k.h = std::min( k.h, 0.25*( c.high-c.low ) );   // A narrow band keeps an inside
    k.pause   = t.pause;
    k.single  = t.single;
    k.holdoff = t.single ? 0 : t.holdoff;
    k.max     = t.single ? 1 : t.max;
    k.hits    = t.hits.size();
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
    for( const Trigger& t : m_list )
    {
        if( !t.armed ) continue;
        Check k;
        if( !compileOne( t, k ) ){
            qWarning().noquote() << "[triggers] Trigger "+QString::number( t.id )+" ("+describe( t )+") skipped: "+check( t );
            continue;
        }
        auto o = old.find( t.id );         // The same check as before (an edit of another row): keep its state
        if( o != old.end() && o->p0 == k.p0 && o->p1 == k.p1 && o->curr == k.curr && o->window == k.window
            && o->edge == k.edge && o->outside == k.outside && o->level == k.level && o->lo == k.lo
            && o->hi == k.hi && o->h == k.h )
        {
            k.armR = o->armR; k.armF = o->armF; k.armW = o->armW;
            k.holdEnd = o->holdEnd;
        }
        m_checks.append( k );
    }
    if( derive ) m_derive = true;
    if( m_sim ) m_sim->setWatching( !m_checks.isEmpty() );
    TRIG_LOG << "compiled" << m_checks.size() << "check(s)" << ( derive ? "(sides from the values)" : "" );
}

void Triggers::simStarted()
{
    m_sim = Simulator::self();
    m_sim->addToUpdateList( this );
    for( Trigger& t : m_list ){ t.hits.clear(); t.selfDisarmedAt = 0; }   // Hits are this run's
    m_checks.clear();
    m_fired.clear();
    m_countNew.clear();
    m_derive = false;
    m_dirty = false;
    m_wasPaused = m_sim->isPaused();
    compile( false );
    emit changed();
}

void Triggers::simStopped()
{
    updateStep();       // Hits of the last frame
    summary();
    m_checks.clear();
    m_fired.clear();
    m_dirty = false;
}

void Triggers::stepDone()   // Worker: after each solved step
{
    Simulator* sim = m_sim;
    uint64_t t = sim->circTime();
    if( m_derive )      // The first step after a keyframe load: the sides from the values, no hits
    {
        m_derive = false;
        for( Check& c : m_checks ) c.derive( c.value() );
        return;
    }
    bool quiet = t <= sim->trigQuiet();   // A replay of going back: track, don't fire
    bool stop = false;
    for( Check& c : m_checks )
    {
        if( c.done ) continue;
        double v = c.value();
        if( !c.eval( v ) || quiet ) continue;
        if( c.holdEnd && t < c.holdEnd ) continue;
        c.hits++;
        c.holdEnd = c.holdoff ? t+c.holdoff : 0;
        if( c.max && c.hits >= c.max ) c.done = true;
        m_fired.append({ c.id, t, v });
        if( c.pause ) stop = true;
    }
    if( stop ) sim->trigStop();
}

void Triggers::updateStep()   // GUI thread, worker idle
{
    Simulator* sim = Simulator::self();
    if( !m_sim || sim != m_sim ) return;

    bool any = false;
    int stopId = -1;
    uint64_t stopTime = 0;
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
        if( t->pause || t->keyframe )
            qInfo().noquote() << "Trigger: "+describe( *t )+" at "+timeFmt( h.time )+( t->pause ? " (paused)" : "" );
        else m_countNew.append( t->id );
    }
    m_fired.clear();

    if( sim->trigStopped() )
    {
        sim->clearTrigStop();
        if( stopId >= 0 ){ m_stopId = stopId; m_stopTime = stopTime; }
        m_stopCount++;
        if( sim->isRunning() && !sim->isPaused() ) CircuitWidget::self()->pauseCirc();
        emit fired( m_stopId );
    }
    if( m_dirty && sim->isRunning() ){ m_dirty = false; compile( false ); }

    bool paused = sim->isPaused();
    if( paused && !m_wasPaused ) summary();
    m_wasPaused = paused;
    if( any ) emit changed();
}

void Triggers::restored()   // GUI thread: a keyframe was loaded
{
    Simulator* sim = Simulator::self();
    if( !m_sim || sim != m_sim ) return;
    m_fired.clear();
    sim->clearTrigStop();
    uint64_t keep = std::max( sim->circTime(), sim->trigQuiet() );   // A replay to trigQuiet follows
    for( Trigger& t : m_list )
    {
        while( !t.hits.isEmpty() && t.hits.last() > keep ) t.hits.removeLast();
        if( t.selfDisarmedAt > keep ){ t.armed = true; t.selfDisarmedAt = 0; }
    }
    m_checks.clear();                     // The checks start again from the definitions
    compile( true );
    m_dirty = false;
    emit changed();
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
