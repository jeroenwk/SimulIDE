/***************************************************************************
 *   Skip forward by an interval (specs/sim-control.md §3.3–3.4)           *
 ***( see copyright.txt file at root folder )*******************************/

#include <QRegularExpression>
#include <cmath>

#include "simskip.h"
#include "currentflow.h"
#include "simulator.h"
#include "circuitwidget.h"
#include "simhistory.h"
#include "infowidget.h"
#include "triggers.h"

SimSkip* SimSkip::self()
{
    static SimSkip* s = new SimSkip();
    return s;
}

SimSkip::SimSkip()
{
    m_poll.setInterval( 10 );
    connect( &m_poll, &QTimer::timeout, this, [this](){ poll(); } );
}

QString SimSkip::start( uint64_t span, int count )
{
    if( m_active ) return "busy: a skip is running";
    if( count < 1 ) count = 1;
    Simulator* sim = Simulator::self();
    if( !sim ) return "no circuit";
    if( sim->simState() == SIM_DEBUG || sim->isPauseDebug() ) return "the debugger controls the simulation";

    bool wasStopped = !sim->isRunning();
    if( wasStopped )
    {
        CircuitWidget::self()->powerCircOn();
        if( !sim->isRunning() ) return "the simulation could not start";
    }
    sim->addToUpdateList( this );   // Each circuit has its own Simulator (no-op if listed)
    sim->holdWorker();              // The worker idle at a solved step (no-op when paused)

    m_from = sim->circTime();       // A new run starts at 1 ps (resetSim): measured from there, as sim run --for
    m_span = span;
    if( span == 0 )                 // Solver steps: to the next pending event
    {
        m_target = sim->nextEventTime();
        if( m_target == 0 )
        {
            if( !sim->isPaused() ) CircuitWidget::self()->pauseCirc();
            return "no event pending: the circuit is static";
        }
        m_stepsLeft = count;
    }
    else
    {
        m_target = m_from + span*(uint64_t)count;
        if( m_target <= sim->circTime() ) m_target = sim->circTime()+1;
        m_stepsLeft = 0;
    }
    sim->setStopAt( m_target );
    sim->setFastRun( true );
    m_trigCount = Triggers::self() ? Triggers::self()->stopCount() : 0;

    m_active = true;
    m_waitFrame = -1;
    m_wall.start();
    if( sim->isPaused() ) CircuitWidget::self()->pauseCirc();   // Toggles: resumes
    else sim->resumeWorker();
    m_poll.start();
    qDebug() << "[skip] from" << m_from << "ps to" << m_target << "ps" << ( span ? "" : "(solver steps)" ) << count;
    return "";
}

QString SimSkip::startBack( uint64_t span, int count )
{
    if( m_active ) return "busy: a skip is running";
    if( count < 1 ) count = 1;
    Simulator* sim = Simulator::self();
    if( !sim || !sim->isRunning() ) return "nothing to go back to: the simulation is stopped";
    if( sim->simState() == SIM_DEBUG || sim->isPauseDebug() ) return "the debugger controls the simulation";
    SimHistory* h = SimHistory::self();
    if( !h->enabled() ) return "history is off (settings history)";
    if( !h->usable() )
    {
        if( !h->missing().isEmpty() ) return "going back is off for this circuit: "+SimHistory::blockers( sim->elementList() );
        return "no history: "+h->lastError();
    }
    if( h->count() == 0 ) return "no keyframe yet";
    if( !sim->isPaused() ) CircuitWidget::self()->pauseCirc();

    m_back = true;
    m_backFrom = sim->circTime();
    CurrentFlow::self()->rewindCue( 300 );    // From the key press on; a long one is extended at the landing
    m_announced = false;
    m_wall.start();
    if( span == 0 )
    {
        m_backSteps = count;
        stepBack();
        return m_active ? "" : "no earlier step in the history";
    }

    uint64_t back = span*(uint64_t)count;
    uint64_t target = ( back < m_backFrom ) ? m_backFrom-back : 0;
    if( target < h->firstTime() ) target = h->firstTime();   // The history starts there
    QString err = backTo( target );
    if( !err.isEmpty() ) m_back = false;
    return err;
}

QString SimSkip::backTo( uint64_t target )
{
    Simulator* sim = Simulator::self();
    sim->setTrigQuiet( target );              // The replay up to the target does not fire triggers
    uint64_t k = SimHistory::self()->restore( sim, target );
    if( !k ){ m_back = false; return "no keyframe at or before "+Simulator::siStr( target*1e-12, "s" ); }
    SimHistory::self()->truncateAfter( k );   // Going on from here: the old future is recomputed
    InfoWidget::self()->setCircTime( sim->circTime() );   // The toolbar time at once
    if( k >= target )                         // On a keyframe: only a frame for the displays
    {
        m_active = true;
        m_from = m_target = target;
        m_span = 1;
        m_waitFrame = (int64_t)m_frames;
        sim->addToUpdateList( this );
        m_poll.start();
        return "";
    }
    m_active = false;
    QString err = start( target-k );          // Replay from the keyframe (fast run), pause there
    m_from = m_backFrom;                      // Reported from where the back started
    return err;
}

// One solver step back: replay to just before the present time, read when the last step before
// it ran, replay again to exactly there. Two replays of at most one frame each
void SimSkip::stepBack()
{
    Simulator* sim = Simulator::self();
    uint64_t now = sim->circTime();
    if( now <= SimHistory::self()->firstTime()+1 ){ backTo( now ); return; }   // The history starts here
    uint64_t probe = now-1;
    sim->setTrigQuiet( probe );               // Replays don't fire triggers
    uint64_t k = SimHistory::self()->restore( sim, probe );
    if( !k ){ finish( false, "no keyframe before "+Simulator::siStr( now*1e-12, "s" ) ); return; }
    auto land = [this]()
    {
        uint64_t last = Simulator::self()->lastEventTime();   // The last step at or before now-1
        if( --m_backSteps > 0 ) m_then = [this](){ stepBack(); };
        QString err = backTo( last );
        if( !err.isEmpty() ){ m_then = nullptr; finish( false, err ); }
    };
    if( k == probe ){ land(); return; }
    m_then = land;
    QString err = start( probe-k );
    if( !err.isEmpty() ){ m_then = nullptr; finish( false, err ); }
}

void SimSkip::cancel()
{
    if( !m_active ) return;
    Simulator* sim = Simulator::self();
    if( sim ){
        sim->setStopAt( 0 );
        sim->setFastRun( false );
        if( sim->isRunning() && !sim->isPaused() ) CircuitWidget::self()->pauseCirc();
    }
    finish( false, "skip cancelled at "+Simulator::siStr( sim ? sim->circTime()*1e-12 : 0, "s" ) );
}

void SimSkip::poll()
{
    Simulator* sim = Simulator::self();
    if( !sim || !sim->isRunning() ){ finish( false, "the simulation stopped" ); return; }

    if( m_waitFrame < 0 )
    {
        if( sim->isPaused() )            // Paused by someone else (F6, the CLI, a trigger): the skip ends there
        {
            sim->setStopAt( 0 );
            sim->setFastRun( false );
            Triggers* tr = Triggers::self();
            if( tr && tr->stopCount() != m_trigCount && !tr->lastStopText().isEmpty() )
                finish( true, "stopped by trigger "+tr->lastStopText() );    // Like a breakpoint (§8): not an error
            else
                finish( false, "skip cancelled at "+Simulator::siStr( sim->circTime()*1e-12, "s" ) );
            return;
        }
        if( sim->circTime() < m_target ){ progress(); return; }

        if( m_span == 0 && m_stepsLeft > 1 )   // More solver steps: on to the next event
        {
            sim->holdWorker();
            uint64_t next = sim->nextEventTime();
            if( next > sim->circTime() )
            {
                m_stepsLeft--;
                m_target = next;
                sim->setStopAt( m_target );
                sim->resumeWorker();
                return;
            }
        }
        CircuitWidget::self()->pauseCirc();
        sim->setStopAt( 0 );
        sim->setFastRun( false );
        m_waitFrame = (int64_t)m_frames;     // Let the displays draw the new state
        return;
    }
    if( (int64_t)m_frames <= m_waitFrame ) return;

    uint64_t t = sim->circTime();
    if( m_back )
    {
        finish( true, "−"+intervalText( m_backFrom-t )+" → "+Simulator::siStr( t*1e-12, "s" ) );
        return;
    }
    QString span = m_span ? intervalText( t-m_from ) : intervalText( 0 );
    if( m_span == 0 && t > m_from ) span += " ("+intervalText( t-m_from )+")";
    finish( true, "+"+span+" → "+Simulator::siStr( t*1e-12, "s" ) );
}

void SimSkip::progress()
{
    // The toolbar time shows how far a skip has come; a long one gets a line in Messages
    if( m_wall.elapsed() < 300 || m_announced ) return;
    m_announced = true;
    if( m_back ) qInfo().noquote() << "Going back to "+Simulator::siStr( m_target*1e-12, "s" )+" …";
    else qInfo().noquote() << "Skipping "+intervalText( m_target-m_from )+" from "+Simulator::siStr( m_from*1e-12, "s" )+" …";
}

void SimSkip::finish( bool ok, QString msg )
{
    m_poll.stop();
    m_active = false;
    m_waitFrame = -1;
    if( Simulator::self() && !( ok && m_then ) ) Simulator::self()->setTrigQuiet( 0 );   // Replays done
    if( ok && m_then )                 // A phase of a longer move (a step back): on to the next one
    {
        auto next = m_then;
        m_then = nullptr;
        next();
        return;
    }
    m_then = nullptr;
    if( ok && m_back ) CurrentFlow::self()->rewindCue();   // The dots run backwards a moment
    m_back = false;
    // Landed and paused on a solved step: a keyframe there, so a skip shorter than a frame still
    // leaves one and the places skipped to are quick to come back to (specs/sim-control.md §4.8)
    Simulator* sim = Simulator::self();
    if( ok && sim && sim->isRunning() && SimHistory::self()->usable() ) SimHistory::self()->capture( sim );
    // A quick skip is seen in the time; a long, cancelled or failed one gets a line in Messages
    if( m_announced || !ok ) qInfo().noquote() << "Skip: "+msg;
    m_announced = false;
    qDebug() << "[skip]" << ( ok ? "done:" : "ended:" ) << msg;
    QList<Done> done = m_done;
    m_done.clear();
    for( Done& fn : done ) fn( ok, msg );
}

// Interval ladder -------------------------------------------------------------

const QList<uint64_t>& SimSkip::ladder()
{
    static QList<uint64_t> l;
    if( l.isEmpty() )
    {
        l.append( 0 );                                   // 1 step
        uint64_t dec = 1;
        for( int e=0; e<=12; ++e, dec *= 10 )            // 1 ps … 5 s
            for( uint64_t m : { 1, 2, 5 } ) l.append( m*dec );
        for( uint64_t s : { 10, 20, 30, 60 } ) l.append( s*1000000000000ull );
    }
    return l;
}

uint64_t SimSkip::nextInterval( uint64_t ps, int dir, int factor )
{
    const QList<uint64_t>& l = ladder();
    if( ps == 0 ) return ( dir > 0 ) ? 1 : 0;            // From 1 step: up to 1 ps
    if( factor <= 1 )                                    // One notch
    {
        if( dir > 0 ){ for( uint64_t v : l ) if( v > ps ) return v; return l.last(); }
        for( int i=l.size()-1; i>=0; --i ) if( l.at(i) < ps ) return l.at(i);
        return 0;
    }
    double want = ( dir > 0 ) ? double( ps )*factor : double( ps )/factor;
    if( want < 1 ) return ( ps <= 1 ) ? 0 : 1;           // Below 1 ps: 1 ps first, then 1 step
    uint64_t best = 1;
    double bestD = 1e300;
    for( uint64_t v : l )
    {
        if( v == 0 ) continue;
        double d = std::fabs( std::log( double( v )/want ) );
        if( d < bestD ){ bestD = d; best = v; }
    }
    return best;
}

QString SimSkip::intervalText( uint64_t ps )
{
    if( ps == 0 ) return "1 step";
    auto num = []( double v ){ return QString::number( v, 'g', 4 ); };
    if( ps >= 60000000000000ull && ps % 60000000000000ull == 0 ) return num( ps/6e13 )+" min";
    double s = double( ps )/1e12;
    if( s >= 1    ) return num( s )+" s";
    if( s >= 1e-3 ) return num( s*1e3 )+" ms";
    if( s >= 1e-6 ) return num( s*1e6 )+" µs";
    if( s >= 1e-9 ) return num( s*1e9 )+" ns";
    return QString::number( ps )+" ps";
}

bool SimSkip::parseInterval( QString text, uint64_t* ps )
{
    QString t = text.trimmed().toLower();
    if( t == "step" || t == "1 step" || t == "1step" || t == "steps" ){ *ps = 0; return true; }
    static const QRegularExpression re("^([0-9]*\\.?[0-9]+(?:e[-+]?[0-9]+)?)\\s*(ps|ns|us|µs|ms|s|min)?$");
    QRegularExpressionMatch m = re.match( t );
    if( !m.hasMatch() ) return false;
    double v = m.captured(1).toDouble();
    QString u = m.captured(2);
    double mult = 1e12;                                  // Default: seconds
    if     ( u == "ps" ) mult = 1;
    else if( u == "ns" ) mult = 1e3;
    else if( u == "us" || u == "µs" ) mult = 1e6;
    else if( u == "ms" ) mult = 1e9;
    else if( u == "min" ) mult = 6e13;
    double r = v*mult;
    if( r < 0.5 || r > double( maxInterval )+0.5 ) return false;
    *ps = (uint64_t)llround( r );
    return true;
}
