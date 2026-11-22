/***************************************************************************
 *   Part states: the current one and every change with its simulated     *
 *   time, written in the simulation thread, read in the GUI thread       *
 ***( see copyright.txt file at root folder )*******************************/

#include <atomic>
#include <QList>

#include "statelog.h"
#include "simstate.h"

uint64_t StateLog::nextSeq()
{
    static std::atomic<uint64_t> s_seq( 0 );
    return ++s_seq;
}

StateWatch* StateLog::s_watch = nullptr;

static QMutex s_allLock;
static QList<StateLog*> s_all;    // Every log, subcircuit parts included, in creation order (keyframes)

StateLog::StateLog()  { QMutexLocker l( &s_allLock ); s_all.append( this ); }
StateLog::~StateLog() { QMutexLocker l( &s_allLock ); s_all.removeOne( this ); }

void StateLog::ioAll( StateIO& s )
{
    QMutexLocker l( &s_allLock );
    uint32_t n = s_all.size();
    s.io( n );
    if( !s.saving() && n != (uint32_t)s_all.size() ){ s.fail(); return; }
    for( StateLog* log : s_all )
    {
        QMutexLocker lock( &log->m_lock );
        s.ioString( log->m_text );
        s.io( log->m_level );
    }
}

void StateLog::rewindList( QList<Event>& h, QString& text, int& level, uint64_t time )
{
    while( !h.isEmpty() && h.last().time > time ) h.removeLast();
    bool same = h.isEmpty() ? ( level < 0 ) : ( h.last().text == text && h.last().level == level );
    if( !same && level >= 0 ) h.append( { time, text, level, nextSeq() } );
    else if( !same ){ text = h.last().text; level = h.last().level; }   // No state loaded: the history's
}

void StateLog::rewind( uint64_t time )
{
    QMutexLocker lock( &m_lock );
    rewindList( m_history, m_text, m_level, time );
    m_peak = { 0, "", -1, 0 };
    m_held = { 0, "", -1, 0 };
    m_holdFrames = 0;
}

void StateLog::rewindAll( uint64_t time )
{
    QMutexLocker l( &s_allLock );
    for( StateLog* log : s_all ) log->rewind( time );
}

bool StateLog::report( QString text, int level, uint64_t time )
{
    QMutexLocker lock( &m_lock );
    if( text == m_text && level == m_level ) return false;   // Only changes count
    m_text  = text;
    m_level = level;
    if( m_history.size() >= 1000 ) m_history.removeFirst();
    Event e = { time, text, level, nextSeq() };
    m_history.append( e );
    if( level > m_peak.level ) m_peak = e;
    lock.unlock();
    if( s_watch ) s_watch->stateReport( this, text, level, time );   // Triggers
    return true;
}

StateLog::Event StateLog::current()
{
    QMutexLocker lock( &m_lock );
    uint64_t t = m_history.isEmpty() ? 0 : m_history.last().time;
    return { t, m_text, m_level, 0 };
}

QList<StateLog::Event> StateLog::history()
{
    QMutexLocker lock( &m_lock );
    return m_history;
}

void StateLog::clear()
{
    QMutexLocker lock( &m_lock );
    m_text.clear();
    m_level = -1;
    m_history.clear();
    m_peak = { 0, "", -1, 0 };
    m_held = { 0, "", -1, 0 };
    m_holdFrames = 0;
}

StateLog::Event StateLog::shown()
{
    QMutexLocker lock( &m_lock );
    Event now = { 0, m_text, m_level, 0 };
    if( m_peak.level > now.level ){ m_held = m_peak; m_holdFrames = 3; }  // Came and went: hold it
    m_peak = { 0, "", -1, 0 };
    if( m_holdFrames > 0 && m_held.level > now.level ){ m_holdFrames--; return m_held; }
    m_holdFrames = 0;
    return now;
}
