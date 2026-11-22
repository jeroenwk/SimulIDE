/***************************************************************************
 *   Transient noise: physical noise of the parts in a time simulation     *
 *   (spec: docs/NOISE_SPEC.md)                                            *
 ***( see copyright.txt file at root folder )*******************************/

#include <cmath>
#include <QHash>
#include <QDebug>

#include "noise.h"
#include "simulator.h"
#include "e-pin.h"
#include "e-node.h"
#include "circuit.h"
#include "component.h"
#include "subcircuit.h"
#include "e-reactive.h"

namespace
{
uint64_t splitmix64( uint64_t x )
{
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}
uint64_t fnv1a64( const QString& s )
{
    uint64_t h = 0xcbf29ce484222325ULL;
    for( QChar c : s ){ h ^= c.unicode(); h *= 0x100000001b3ULL; }
    return h;
}

struct Owner { QString label, type, uid; };

// Every component's uid → its label path and type, subcircuit parts included ("U1-R2")
void mapOwners( QList<Component*>* list, const QString& prefix, QHash<QString, Owner>& map )
{
    for( Component* comp : *list )
    {
        QString label = prefix+comp->idLabel();
        map.insert( comp->getUid(), { label, comp->itemType(), comp->getUid() } );
        if( SubCircuit* sub = dynamic_cast<SubCircuit*>( comp ) )
            mapOwners( sub->compList(), label+"-", map );
    }
}

// The component that owns an element: its uid is the element id or a prefix of it ("-resA")
const Owner* findOwner( const QString& elId, const QHash<QString, Owner>& map )
{
    QString id = elId;
    while( !id.isEmpty() )
    {
        auto it = map.find( id );
        if( it != map.end() ) return &it.value();
        int cut = id.lastIndexOf('-');
        if( cut <= 0 ) break;
        id = id.left( cut );
    }
    return nullptr;
}
}

QString NoiseSrc::kindName( Kind k )
{
    switch( k ){
        case Thermal: return "thermal";
        case Shot:    return "shot";
        case Channel: return "channel";
    }
    return "?";
}

bool NoiseSettings::isDefault() const
{
    NoiseSettings d;
    return on == d.on && bw == d.bw && scale == d.scale && seed == d.seed
        && start == d.start && stop == d.stop && off.isEmpty();
}

uint64_t NoiseSettings::holdPs() const
{
    double ps = 1e12/(2*bw);
    if( ps < 1 ) ps = 1;
    return (uint64_t)std::llround( ps );
}

eNoise::eNoise( const NoiseSettings& s, uint64_t seed )
      : eElement( "Noise-engine" )
{
    m_set  = s;
    m_seed = seed;
    m_hold = s.holdPs();
    m_gain = s.scale*std::sqrt( 1e12/(2.0*m_hold) ); // √fmax of the hold actually used
    m_state = Armed;
    m_firstActive = 0;
    m_lastActive  = 0;
}

eNoise::~eNoise()
{
    for( Src* s : m_src )
    {
        s->pa->stampCurrent( 0 );
        s->pb->stampCurrent( 0 );
        s->pa->setEnode( nullptr );
        s->pb->setEnode( nullptr );
        delete s->pa;
        delete s->pb;
        delete s;
    }
    m_src.clear();
}

void eNoise::collect()
{
    QList<NoiseSrc> list;
    QHash<QString, Owner> owners;
    if( Circuit::self() ) mapOwners( Circuit::self()->compList(), "", owners );

    for( eElement* el : Simulator::self()->elementList() )
    {
        if( el == this ) continue;
        int first = list.size();
        el->noiseSources( list );
        for( int i=first; i<list.size(); ++i )
        {
            NoiseSrc& s = list[i];
            s.elementId = el->getId();
            const Owner* o = findOwner( s.elementId, owners );
            s.partId   = o ? o->label : s.elementId;
            s.partType = o ? o->type  : "";
            if( o && s.partId.isEmpty() ) s.partId = o->uid;
        }
    }
    // noiseOff: labels, uids or "type:NAME" (case-insensitive)
    QStringList used;
    QList<NoiseSrc> keep;
    for( NoiseSrc& s : list )
    {
        QString owner = s.partId;
        const Owner* o = findOwner( s.elementId, owners );
        bool skip = false;
        for( const QString& e : m_set.off )
        {
            bool m = false;
            if( e.startsWith("type:", Qt::CaseInsensitive ) )
                m = !s.partType.isEmpty() && e.mid( 5 ).compare( s.partType, Qt::CaseInsensitive ) == 0;
            else
                m = e.compare( owner, Qt::CaseInsensitive ) == 0 || ( o && e == o->uid );
            if( m ){ skip = true; if( !used.contains( e ) ) used.append( e ); }
        }
        if( !skip ) keep.append( s );
    }
    for( const QString& e : m_set.off )
        if( !used.contains( e ) && !m_startUsed.contains( e ) ) m_unmatched.append( e );

    QHash<QString,int> index;   // Per element and kind, so a stream doesn't depend on the list order
    for( NoiseSrc& s : keep )
    {
        if( !s.a || !s.b || !s.psd ) continue;
        QString key = s.elementId+"/"+NoiseSrc::kindName( s.kind );
        int n = index.value( key, 0 );
        index[key] = n+1;

        Src* src = new Src();     // Value-initialized: the normal distribution's unused cache is zeroed (not garbage in keyframes)
        src->s = s;
        src->pa = new ePin( "Noise-"+key+"-"+QString::number(n)+"a", 0 );
        src->pb = new ePin( "Noise-"+key+"-"+QString::number(n)+"b", 1 );
        src->rng.seed( splitmix64( m_seed ^ fnv1a64( key+"/"+QString::number( n ) ) ) );
        m_src.push_back( src );
    }
}

int eNoise::startState( const QString& elId )
{
    if( !m_set.off.isEmpty() )
    {
        if( !m_startMapped )      // Every part once: its noiseOff entry, if any
        {
            m_startMapped = true;
            QHash<QString, Owner> owners;
            if( Circuit::self() ) mapOwners( Circuit::self()->compList(), "", owners );
            for( const Owner& o : owners )
            {
                QString hit;
                for( const QString& e : m_set.off )
                {
                    bool m = e.startsWith("type:", Qt::CaseInsensitive )
                           ? e.mid( 5 ).compare( o.type, Qt::CaseInsensitive ) == 0
                           : e.compare( o.label, Qt::CaseInsensitive ) == 0 || e == o.uid;
                    if( m ){ hit = e; break; }
                }
                m_startOff.insert( o.uid, hit );
            }
        }
        QString id = elId;        // The owning part: the element id or a prefix of it (findOwner)
        while( !id.isEmpty() )
        {
            auto it = m_startOff.find( id );
            if( it != m_startOff.end() )
            {
                if( it.value().isEmpty() ) break;
                if( !m_startUsed.contains( it.value() ) ) m_startUsed.append( it.value() );
                return -1;
            }
            int cut = id.lastIndexOf('-');
            if( cut <= 0 ) break;
            id = id.left( cut );
        }
    }
    return splitmix64( m_seed ^ fnv1a64( elId+"/start" ) ) & 1;
}

uint64_t eNoise::seedFor( const QString& elId, const QString& tag )
{
    if( startState( elId ) < 0 ) return 0;
    return splitmix64( m_seed ^ fnv1a64( elId+"/"+tag ) ) | 1;   // Never 0 (0 = off)
}

void eNoise::stamp()
{
    collect();
    for( eElement* el : Simulator::self()->elementList() )
        if( eReactive* r = dynamic_cast<eReactive*>( el ) ) m_reactive.push_back( r );
    for( Src* s : m_src )
    {
        s->pa->setEnode( s->s.a->getEnode() );
        s->pb->setEnode( s->s.b->getEnode() );
        s->pa->createCurrent();
        s->pb->createCurrent();
    }
    uint64_t t = Simulator::self()->circTime();
    uint64_t at = m_set.start > t ? m_set.start-t : 0;
    Simulator::self()->addEvent( at, this );
}

void eNoise::zero()
{
    for( Src* s : m_src )
    {
        s->last = 0;
        s->pa->stampCurrent( 0 );
        s->pb->stampCurrent( 0 );
    }
}

void eNoise::runEvent()
{
    uint64_t now = Simulator::self()->circTime();
    if( m_set.stop && now >= m_set.stop )
    {
        zero();
        fineSteps( false );
        m_state = Ended;
        return;
    }
    if( m_state == Armed ){ m_state = Active; m_firstActive = now ? now : 1; fineSteps( true ); }
    m_lastActive = now;

    for( Src* s : m_src )
    {
        double psd = s->s.psd();
        if( !(psd > 0) || !std::isfinite( psd ) ) psd = 0;
        s->lastPsd = psd;
        double i = m_gain*std::sqrt( psd )*s->nd( s->rng );
        s->last = i;
        s->pa->stampCurrent(-i );   // Current vector: -i out of a's node, +i into b's node
        s->pb->stampCurrent( i );
    }
    uint64_t next = m_hold;
    if( m_set.stop && now+next > m_set.stop ) next = m_set.stop-now;
    Simulator::self()->addEvent( next, this );
}

// While noise is active every capacitor and inductor integrates with the noise step (when
// finer than its own): backward Euler damps any growth faster than about 2/step, so with the
// normal reactStep an unstable balance point (bistable, oscillator) would look stable
void eNoise::fineSteps( bool on )
{
    for( eReactive* r : m_reactive ) r->setFineStep( on ? m_hold : 0 );
}

uint64_t eNoise::activeUntil()
{
    if( !m_firstActive ) return 0;
    if( m_state == Ended ) return m_set.stop;
    uint64_t t = m_lastActive+m_hold;
    if( m_set.stop && t > m_set.stop ) t = m_set.stop;
    return t;
}

int eNoise::count( NoiseSrc::Kind k )
{
    int n = 0;
    for( Src* s : m_src ) if( s->s.kind == k ) n++;
    return n;
}

QList<eNoise::SrcInfo> eNoise::info()
{
    QList<SrcInfo> l;
    for( Src* s : m_src )
    {
        eNode* na = s->s.a->getEnode();
        eNode* nb = s->s.b->getEnode();
        double psd = s->s.psd();
        if( !(psd > 0) || !std::isfinite( psd ) ) psd = 0;
        l.append({ s->s.partId, s->s.partType, NoiseSrc::kindName( s->s.kind ),
                   na ? na->itemId() : "", nb ? nb->itemId() : "",
                   psd, m_gain*std::sqrt( psd ) });
    }
    return l;
}

// The window state, and each source's random generator and last current: a replay draws the
// same numbers. The generators are plain values (mt19937_64: its words and index; the normal
// distribution: its parameters and the spare value), copied as bytes
void eNoise::simState( StateIO& s )
{
    eElement::simState( s );
    s.io( m_seed );
    s.io( m_hold );
    s.io( m_gain );
    s.io( m_startMapped );
    int st = m_state.load();             s.io( st );
    uint64_t fa = m_firstActive.load(); s.io( fa );
    uint64_t la = m_lastActive.load();  s.io( la );
    if( !s.saving() ){ m_state = st; m_firstActive = fa; m_lastActive = la; }
    uint32_t n = m_src.size();
    s.io( n );
    if( n != m_src.size() ){ s.fail(); return; }
    for( Src* src : m_src )
    {
        s.raw( &src->rng, sizeof( src->rng ) );
        s.raw( &src->nd, sizeof( src->nd ) );
        s.io( src->last );
        s.io( src->lastPsd );
    }
}

