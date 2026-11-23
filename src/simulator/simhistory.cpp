/***************************************************************************
 *   Keyframes of a running simulation (specs/sim-control.md §4.3, §4.5)   *
 ***( see copyright.txt file at root folder )*******************************/

#include <QElapsedTimer>
#include <QHash>
#include <QMap>
#include <QDebug>
#include <cstdlib>
#include <cxxabi.h>
#include <typeinfo>

#include "simhistory.h"
#include "simstate.h"
#include "simulator.h"
#include "e-element.h"
#include "mainwindow.h"
#include "circuitwidget.h"
#include "statelog.h"
#include "scriptcpu.h"
#include "eventlog.h"
#include <QSettings>

SimHistory* SimHistory::self()
{
    static SimHistory* s = nullptr;
    if( !s ){
        s = new SimHistory();
        if( MainWindow::self() )        // A user setting, on by default (Simulation/history)
            s->m_enabled = MainWindow::self()->settings()->value("Simulation/history", true ).toBool();
    }
    return s;
}

void SimHistory::setEnabled( bool on )
{
    m_enabled = on;
    if( !on ) stop();
    if( MainWindow::self() ){
        MainWindow::self()->settings()->setValue("Simulation/history", on );
        MainWindow::self()->settings()->sync();
    }
    if( CircuitWidget::self() ) CircuitWidget::self()->updtBackAvail( false );
}

void SimHistory::setBudget( uint64_t bytes )
{
    if( bytes < 1024*1024 ) bytes = 1024*1024;
    if( bytes == m_budget ) return;
    m_budget = bytes;
    stop();
    free( m_arena );           // Allocated again at the next capture
    m_arena = nullptr;
    m_arenaSize = 0;
}

QString SimHistory::className( eElement* el )
{
    const char* mangled = typeid( *el ).name();
    int status = 0;
    char* d = abi::__cxa_demangle( mangled, nullptr, nullptr, &status );
    QString n = ( status == 0 && d ) ? QString( d ) : QString( mangled );
    free( d );
    return n;
}

void SimHistory::start( Simulator* sim )
{
    stop();
    m_ended    = false;
    m_captures = 0;
    m_skipped  = 0;
    m_totalNs  = 0;
    m_maxNs    = 0;
    m_error.clear();

    QHash<QString,int> missing;
    for( eElement* el : sim->elementList() )
        if( !el->hasSimState() ) missing[ className( el ) ]++;
    m_missing.clear();
    for( auto it = missing.begin(); it != missing.end(); ++it )
        m_missing << it.key()+" ×"+QString::number( it.value() );
    m_missing.sort();
    m_usable = m_missing.isEmpty();
}

void SimHistory::end( QString why )
{
    if( m_ended || !m_enabled ) return;
    stop();
    m_ended = true;
    m_error = why;
    qInfo().noquote() << "History: "+why+", going back is off until the next start";
}

void SimHistory::stop()
{
    m_keys.clear();
    m_head = 0;
}

uint64_t SimHistory::bytesUsed()
{
    uint64_t b = 0;
    for( const Key& k : m_keys ) b += k.size;
    return b;
}

// Keyframes whose bytes lie in [from, to) are about to be overwritten
void SimHistory::dropOverlapping( uint64_t from, uint64_t to )
{
    while( !m_keys.empty() )
    {
        const Key& k = m_keys.front();
        if( k.off+k.size <= from || k.off >= to ) break;   // The oldest is clear: the rest too
        m_keys.pop_front();
    }
}

void SimHistory::capture( Simulator* sim, bool replace )
{
    if( !m_enabled ) return;
    QElapsedTimer t;
    t.start();

    if( !m_arena )
    {
        m_arena = (uint8_t*)malloc( m_budget );   // Untouched pages cost no memory until written
        if( !m_arena ){ m_enabled = false; m_error = "no memory for the history"; return; }
        m_arenaSize = m_budget;
    }
    if( !m_keys.empty() && m_keys.back().time == sim->circTime() )
    {
        if( !replace ) return;                   // Paused: the same state
        m_head = m_keys.back().off;              // Changed at the same time: take it again
        m_keys.pop_back();
    }

    for( int pass=0; pass<2; ++pass )
    {
        if( m_head >= m_arenaSize ) m_head = 0;
        StateIO s( m_arena+m_head, m_arenaSize-m_head );
        if( !saveAll( sim, s ) )
        {
            m_skipped++;
            m_error = sim->coreError();
            return;
        }
        if( s.overflow() )                       // Past the end: start again at the beginning
        {
            if( m_head == 0 ){ m_enabled = false; m_error = "a keyframe is larger than the history budget"; return; }
            dropOverlapping( m_head, m_arenaSize );
            m_head = 0;
            continue;
        }
        uint64_t size = s.size();
        dropOverlapping( m_head, m_head+size );
        m_keys.push_back({ sim->circTime(), m_head, (uint32_t)size });
        m_head += size;
        break;
    }
    double ns = t.nsecsElapsed();
    m_captures++;
    m_totalNs += ns;
    if( ns > m_maxNs ) m_maxNs = ns;
}

uint64_t SimHistory::restore( Simulator* sim, uint64_t t, QString keepClass )
{
    for( auto it = m_keys.rbegin(); it != m_keys.rend(); ++it )
    {
        if( it->time > t ) continue;

        std::vector<uint8_t> kept;                 // Negative control: these keep their state
        QList<eElement*> keep;
        if( !keepClass.isEmpty() )
        {
            for( eElement* el : sim->elementList() ) if( className( el ) == keepClass ) keep << el;
            kept.resize( 1<<20 );
            StateIO k( kept.data(), kept.size() );
            for( eElement* el : keep ) el->simState( k );
        }
        auto keepThem = [&](){
            if( keep.isEmpty() ) return;
            StateIO k( kept.data(), kept.size(), false );
            for( eElement* el : keep ) el->simState( k );
        };
        if( !loadKey( sim, m_arena+it->off, it->size, it->time, keepThem ) ) return 0;
        return it->time;
    }
    return 0;
}

// The simulation state, then the part states (badges) of the built-in parts: StateLog belongs to
// the GUI component, not to an element. Saves or loads, as s does
bool SimHistory::saveAll( Simulator* sim, StateIO& s )
{
    if( !sim->simCore( s ) ) return false;
    if( !s.mark( 0x54415453 ) ) return false;                  // STAT
    StateLog::ioAll( s );
    SimWatch* w = sim->simWatch();                             // TRIG: the triggers' state (§10 of
    if( s.saving() ){                                          // specs/triggers.html), when they have one
        if( w && w->hasState() ){ s.mark( 0x47495254 ); w->ioState( s ); }
    }else if( s.remaining() >= 4 ){
        if( !s.mark( 0x47495254 ) ) return false;
        if( w ) w->ioState( s );
    }
    return true;
}

// Keyframe bytes into the running circuit (worker idle, paused), and what follows a restore:
// the matrix from the restored stamps, parts told, part states and the event log rewound
bool SimHistory::loadKey( Simulator* sim, const uint8_t* buf, size_t size, uint64_t time,
                          std::function<void()> afterLoad )
{
    StateIO s( buf, size, false );
    if( !saveAll( sim, s ) || s.overflow() )
    {
        m_error = "keyframe at "+QString::number( time )+" ps does not load (state layout differs)";
        qWarning().noquote() << "[history]" << m_error;
        return false;
    }
    if( afterLoad ) afterLoad();
    sim->setLastEventTime( time );
    sim->syncAfterRestore();
    for( eElement* el : sim->elementList() ) el->stateRestored();
    StateLog::rewindAll( time );               // Part states: as they were then (badges, Messages)
    for( eElement* el : sim->elementList() )
        if( ScriptCpu* cpu = dynamic_cast<ScriptCpu*>( el ) ) cpu->statusRewind( time );
    if( EventLog::self() ) EventLog::self()->rewound();
    if( sim->simWatch() ) sim->simWatch()->restored();   // Triggers: hits after this time go
    return true;
}

QByteArray SimHistory::saveBytes( Simulator* sim, QString* error )
{
    QByteArray b;
    for( int cap = 256*1024; cap <= 256*1024*1024; cap *= 4 )
    {
        b.resize( cap );
        StateIO s( (uint8_t*)b.data(), (size_t)cap );
        if( !saveAll( sim, s ) ){ if( error ) *error = sim->coreError(); return QByteArray(); }
        if( s.overflow() ) continue;
        b.resize( (int)s.size() );
        return b;
    }
    if( error ) *error = "the state is larger than 256 MB";
    return QByteArray();
}

QString SimHistory::loadBytes( Simulator* sim, const QByteArray& bytes, uint64_t time )
{
    m_error.clear();
    if( !loadKey( sim, (const uint8_t*)bytes.constData(), (size_t)bytes.size(), time ) )
        return "the key frame does not load into this circuit (its state layout differs)";
    stop();                                    // The ring's keyframes may be another past
    m_ended = false;
    capture( sim, true );                      // Going back works from here on
    return "";
}

// Which elements, in which order, and how many nodes: a keyframe loads only into the same
QString SimHistory::layoutSignature( Simulator* sim )
{
    uint64_t h = 1469598103934665603ull;
    auto add = [&h]( const QByteArray& b ){ for( char c : b ){ h ^= (uint8_t)c; h *= 1099511628211ull; } h ^= 0xff; h *= 1099511628211ull; };
    add( QByteArray::number( sim->eNodeCount() ) );
    for( eElement* el : sim->elementList() ) add( ( className( el )+" "+el->getId() ).toUtf8() );
    return QString::number( (qulonglong)h, 16 )+"/"+QString::number( sim->elementList().size() )
          +"/"+QString::number( sim->eNodeCount() );
}

void SimHistory::truncateAfter( uint64_t t )
{
    while( !m_keys.empty() && m_keys.back().time > t )
    {
        m_head = m_keys.back().off;
        m_keys.pop_back();
    }
}

QString SimHistory::blockers( const QList<eElement*>& elements )
{
    QMap<QString, int> count;
    QMap<QString, QString> why;
    for( eElement* el : elements )
    {
        if( el->hasSimState() ) continue;
        QString n = className( el );
        count[n]++;
        QString w = el->noSimStateWhy();
        if( !why.contains( n ) || why[n].isEmpty() ) why[n] = w;
    }
    QStringList out;
    for( auto it = count.begin(); it != count.end(); ++it )
    {
        QString w = why.value( it.key() );
        out << it.key()+" ×"+QString::number( it.value() )+" ("+( w.isEmpty() ? "no state code yet" : w )+")";
    }
    return out.join("; ");
}

