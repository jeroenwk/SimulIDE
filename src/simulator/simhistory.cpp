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
        if( !sim->simCore( s ) )
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
        StateIO s( m_arena+it->off, it->size, false );
        if( !sim->simCore( s ) || s.overflow() )
        {
            m_error = "keyframe at "+QString::number( it->time )+" ps does not load (state layout differs)";
            qWarning().noquote() << "[history]" << m_error;
            return 0;
        }
        if( !keep.isEmpty() )
        {
            StateIO k( kept.data(), kept.size(), false );
            for( eElement* el : keep ) el->simState( k );
        }
        sim->setLastEventTime( it->time );
        for( eElement* el : sim->elementList() ) el->stateRestored();
        StateLog::rewindAll( it->time );           // Part states: as they were then (badges, Messages)
        for( eElement* el : sim->elementList() )
            if( ScriptCpu* cpu = dynamic_cast<ScriptCpu*>( el ) ) cpu->statusRewind( it->time );
        if( EventLog::self() ) EventLog::self()->rewound();
        return it->time;
    }
    return 0;
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

