/***************************************************************************
 *   SimulIDE command line control interface                              *
 *   Simulation control commands                                           *
 ***( see copyright.txt file at root folder )*******************************/

#include "cliapi.h"
#include "circuitwidget.h"
#include "simulator.h"
#include "updatable.h"
#include "noise.h"
#include "simskip.h"
#include "simhistory.h"
#include "simstate.h"

#include <QJsonArray>
#include <QMap>
#include <QElapsedTimer>
#include <memory>
#include <cstring>

namespace
{
QString stateName( simState_t s )
{
    switch( s ){
        case SIM_STOPPED:  return "stopped";
        case SIM_ERROR:    return "error";
        case SIM_STARTING: return "starting";
        case SIM_PAUSED:   return "paused";
        case SIM_WAITING:  return "running";
        case SIM_RUNNING:  return "running";
        case SIM_DEBUG:    return "debug";
    }
    return "unknown";
}

QJsonObject simStatus()
{
    Simulator* sim = Simulator::self();
    uint64_t t = sim->isRunning() ? sim->circTime() : 0;
    QJsonObject o{
        {"state",   stateName( sim->simState() ) },
        {"time_ps", QString::number( t ) },
        {"time",    Cli::timeStr( t ) },
        {"speed",   100.0*(double)sim->psPerSec()/1e12 } };   // Target speed, % of real time
    if( sim->isRunning() && !sim->isPaused() )
        o["realSpeed"] = qRound( sim->realSpeed() )/100.0;     // Achieved speed, % of real time
    o["noise"] = Cli::noiseJson();
    if( sim->runSeed() ) o["seed"] = (qint64)sim->runSeed();   // rand() seed of this run (settings runSeed)
    if( SimSkip::self()->active() ) o["skipping"] = true;   // A skip (→ key, toolbar, sim skip) not done yet
    if( sim->speedLock() || sim->lockUsedThisRun() )        // Speed lock (specs/sim-control.md §3.9)
    {
        QJsonObject l{
            {"on",       sim->speedLock() },
            {"scale",    sim->stepScale() },
            {"step",     Cli::timeStr( sim->lockedStep() ) },
            {"max_step", Cli::timeStr( sim->lockMaxStep() ) } };
        if( sim->lockLimited() ) l["limited"] = true;       // Can't keep up even so
        o["speedLock"] = l;
        if( sim->lockUsedThisRun() )
            o["note"] = "the speed lock coarsened the reactive step this run (up to ×"
                        +QString::number( sim->lockMaxScaleUsed() )+"): results are less accurate";
    }
    return o;
}

// Counts simulator GUI frames: values such as LED brightness or meter displays are
// computed in updateStep(), so replies wait for one frame after pausing.
class FrameCounter : public Updatable
{
    public:
        virtual void updateStep() override { m_frames++; }
        uint64_t frames() { return m_frames; }
    private:
        uint64_t m_frames = 0;
};

FrameCounter* frameCounter()
{
    static FrameCounter* fc = new FrameCounter();
    Simulator::self()->addToUpdateList( fc );   // Each new circuit creates a new Simulator (no-op if listed)
    return fc;
}

void startOrResume()
{
    Simulator* sim = Simulator::self();
    if( !sim->isRunning() )    CircuitWidget::self()->powerCircOn();
    else if( sim->isPaused() ) CircuitWidget::self()->pauseCirc();   // Toggles: resumes
}
}

QJsonValue Cli::noiseJson()
{
    Simulator* sim = Simulator::self();
    const NoiseSettings& n = sim->noise();
    if( !n.on ) return "off";
    eNoise* e = sim->noiseEngine();
    QString state = "stopped";
    if( e ){
        eNoise::State s = e->state();
        state = ( s == eNoise::Active ) ? "on" : ( s == eNoise::Ended ) ? "ended" : "armed";
    }
    QJsonObject o{
        {"on", true },
        {"active", state == "on" },
        {"state", state },
        {"bw", Simulator::siStr( n.bw, "Hz" ).remove(' ') },
        {"scale", n.scale },
        {"seed", QString::number( n.seed ? n.seed : sim->noiseSeedUsed() ) },
        {"seedGiven", n.seed != 0 },
        {"start", Cli::timeStr( n.start ) },
        {"stop", n.stop ? Cli::timeStr( n.stop ) : QString("end") },
        {"off", QJsonArray::fromStringList( n.off ) },
        {"summary", sim->noiseBadge() } };
    if( e ){
        o["sources"] = e->sources();
        if( !e->unmatched().isEmpty() ) o["unmatched"] = QJsonArray::fromStringList( e->unmatched() );
    }
    return o;
}

bool Cli::noiseDuring( uint64_t t0, uint64_t t1 )
{
    return Simulator::self()->noiseActiveDuring( t0, t1 );
}

bool Cli::noiseSoFar()
{
    return Simulator::self()->noiseWasActive();
}

static QString nodeHash()   // FNV-1a 64 over the bits of every node voltage
{
    Simulator* sim = Simulator::self();
    uint64_t h = 1469598103934665603ull;
    int n = sim->isRunning() ? sim->eNodeCount() : 0;
    for( int i=1; i<=n; ++i )
    {
        double v = sim->eNodeVolt( i );
        uint64_t bits; memcpy( &bits, &v, 8 );
        for( int b=0; b<8; ++b ){ h ^= ( bits >> (8*b) ) & 0xff; h *= 1099511628211ull; }
    }
    return QString::number( (qulonglong)h, 16 );
}

// FNV-1a 64 of the full keyframe content now (nodes, stamps, every element's state, the event
// queue): catches state that changes no voltage (a fuse's heat, a meter's average)
static QString stateHash()
{
    static std::vector<uint8_t> buf( 16u<<20 );
    StateIO s( buf.data(), buf.size() );
    if( !Simulator::self()->simCore( s ) || s.overflow() ) return "unsaved";
    uint64_t h = 1469598103934665603ull;
    for( size_t i=0; i<s.size(); ++i ){ h ^= buf[i]; h *= 1099511628211ull; }
    return QString::number( (qulonglong)h, 16 );
}

// Per element: "class:hash" of its own simState, to name what differs
static QStringList elementHashes()
{
    static std::vector<uint8_t> buf( 1u<<20 );
    QStringList out;
    for( eElement* el : Simulator::self()->elementList() )
    {
        StateIO s( buf.data(), buf.size() );
        el->simState( s );
        uint64_t h = 1469598103934665603ull;
        for( size_t i=0; i<s.size(); ++i ){ h ^= buf[i]; h *= 1099511628211ull; }
        out << SimHistory::className( el )+" "+el->getId()+":"+QString::number( (qulonglong)h, 16 );
    }
    return out;
}

// sim history check: run on recording hashes at checkpoints, go back to the keyframe at or
// before the start, run again to the same checkpoints: every hash must be the same
struct HistCheck
{
    CliPendingPtr reply;
    QList<uint64_t> points;
    QStringList first, second;
    QList<QStringList> elems1, elems2;
    uint64_t from = 0, keyTime = 0, back = 0;
    QString keep;
    double restoreUs = 0;
    int pass = 0;
    int next = 0;
};
static void histCheckStep( std::shared_ptr<HistCheck> hc )
{
    Simulator* sim = Simulator::self();
    if( hc->next >= hc->points.size() )
    {
        if( hc->pass == 0 )                     // Back to the keyframe, then the same checkpoints again
        {
            QElapsedTimer t; t.start();
            uint64_t back = hc->back < hc->from ? hc->from-hc->back : 0;
            uint64_t k = SimHistory::self()->restore( sim, back, hc->keep );
            hc->restoreUs = t.nsecsElapsed()/1e3;
            if( !k ){ hc->reply->fail("no keyframe at or before "+Cli::timeStr( hc->from )+": "+SimHistory::self()->lastError() ); return; }
            SimHistory::self()->truncateAfter( k );
            hc->keyTime = k;
            hc->pass = 1;
            hc->next = 0;
        }else{
            QJsonArray pts;
            int same = 0, sameNodes = 0;
            QStringList diffs;
            for( int i=0; i<hc->points.size(); ++i )
            {
                QStringList a = hc->first.at( i ).split("/"), b = hc->second.at( i ).split("/");
                bool eq = a == b;
                if( eq ) same++;
                if( a.at( 0 ) == b.at( 0 ) ) sameNodes++;
                if( !eq && diffs.isEmpty() )          // Name the elements whose state differs (first checkpoint that differs)
                {
                    const QStringList& e1 = hc->elems1.at( i );
                    const QStringList& e2 = hc->elems2.at( i );
                    for( int j=0; j<e1.size() && j<e2.size(); ++j )
                        if( e1.at( j ) != e2.at( j ) ) diffs << e1.at( j ).section(':', 0, 0 );
                    if( diffs.isEmpty() ) diffs << "the simulator core, nodes or event queue";
                }
                pts.append( QJsonObject{ {"time", Cli::timeStr( hc->points.at( i ) ) },
                                         {"first", hc->first.at( i ) }, {"replay", hc->second.at( i ) }, {"same", eq } } );
            }
            hc->reply->ok( QJsonObject{
                {"ok",        same == hc->points.size() },
                {"from",      Cli::timeStr( hc->from ) },
                {"keyframe",  Cli::timeStr( hc->keyTime ) },
                {"restore_us", qRound( hc->restoreUs*10 )/10.0 },
                {"same",      same },          // Nodes and state
                {"same_nodes", sameNodes },    // Node voltages only
                {"differ",    QJsonArray::fromStringList( diffs.mid( 0, 40 ) ) },
                {"points",    pts } } );
            return;
        }
    }
    uint64_t target = hc->points.at( hc->next );
    uint64_t now = sim->circTime();
    if( target <= now ){ hc->reply->fail("checkpoint "+Cli::timeStr( target )+" is not ahead of "+Cli::timeStr( now ) ); return; }
    QString err = SimSkip::self()->start( target-now );
    if( !err.isEmpty() ){ hc->reply->fail( err ); return; }
    SimSkip::self()->whenDone( [hc]( bool ok, QString msg )
    {
        if( !ok ){ hc->reply->fail( msg ); return; }
        ( hc->pass ? hc->second : hc->first ).append( nodeHash()+"/"+stateHash() );
        ( hc->pass ? hc->elems2 : hc->elems1 ).append( elementHashes() );
        hc->next++;
        histCheckStep( hc );
    });
}

static QJsonValue histCheck( CliCtx& c )
{
    Simulator* sim = Simulator::self();
    if( !SimHistory::self()->usable() ) throw CliError("history is off or not supported here (settings history, sim history)");
    if( !sim->isRunning() ) throw CliError("start the simulation first");
    uint64_t span = Cli::parseTime( c.opt("span", "5ms") );
    bool ok = false;
    int n = c.opt("points", "5").toInt( &ok );
    if( !ok || n < 1 || n > 1000 ) throw CliError("--points must be 1 … 1000");
    if( !sim->isPaused() ) CircuitWidget::self()->pauseCirc();
    if( SimHistory::self()->count() == 0 ) throw CliError("no keyframe yet: let it run a few frames");
    auto hc = std::make_shared<HistCheck>();
    hc->reply = c.defer();
    hc->from = sim->circTime();
    hc->back = c.has("ago") ? Cli::parseTime( c.opt("ago", "0") ) : 0;
    hc->keep = c.opt("keep", "");
    for( int i=1; i<=n; ++i ) hc->points.append( hc->from + span*(uint64_t)i/(uint64_t)n );
    histCheckStep( hc );
    return QJsonValue();
}

void CliApi::registerSim()
{
    add("noise sources", "noise sources", "The transient noise sources of the running circuit: part, kind, nodes, "
        "spectral density S (A²/Hz) and rms current of each.",
        []( CliCtx& ) -> QJsonValue
    {
        Simulator* sim = Simulator::self();
        if( !sim->noise().on ) throw CliError("noise is off (settings set noise=on, then sim start)");
        eNoise* e = sim->noiseEngine();
        if( !e ) throw CliError("noise is on but the simulation is not running (sim start)");
        QJsonArray rows;
        for( const eNoise::SrcInfo& s : e->info() )
            rows.append( QJsonObject{ {"part", s.part}, {"type", s.type}, {"kind", s.kind},
                                      {"nodeA", s.nodeA}, {"nodeB", s.nodeB},
                                      {"psd", s.psd}, {"rms", s.rms} } );
        return QJsonObject{ {"noise", Cli::noiseJson()}, {"sources", rows} };
    });

    add("sim status", "sim status", "Simulation state, simulated time and speed.",
        []( CliCtx& ) -> QJsonValue { return simStatus(); });

    // Determinism check (specs/sim-control.md §4.4): two runs to the same time must give the
    // same bits in every node voltage
    add("sim hash", "sim hash", "Bit-exact hash (FNV-1a 64) of every node voltage at the current simulated time, "
        "with the time and the node count: two runs to the same time compare equal only when they computed the same.",
        []( CliCtx& ) -> QJsonValue
    {
        Simulator* sim = Simulator::self();
        uint64_t t = sim->isRunning() ? sim->circTime() : 0;
        return QJsonObject{ {"time_ps", QString::number( t ) }, {"nodes", sim->isRunning() ? sim->eNodeCount() : 0 },
                            {"hash", nodeHash() } };
    });

    add("sim history", "sim history [sizes | check [--span T] [--points N] [--ago T] [--keep CLASS]]", "Keyframes of the run (specs/sim-control.md §4): "
        "on, usable, count, span, bytes, size of the last one, capture time (mean and max, µs), frames skipped, and "
        "the element classes without state support (missing: history is off for this circuit). check: round trip "
        "of the keyframes: from now, skip to N checkpoints over the span (default 5 ms, 5) recording sim hash at "
        "each, go back to the keyframe at or before now (--ago T: T before now), skip to the same checkpoints "
        "again: ok when every hash is the same (bit for bit). --keep CLASS (negative control): that class keeps "
        "its present state instead of the keyframe's, so the check should fail when its state matters.",
        []( CliCtx& c ) -> QJsonValue
    {
        if( !c.pos.isEmpty() && c.pos.at( 0 ) == "check" ) return histCheck( c );
        if( !c.pos.isEmpty() && c.pos.at( 0 ) == "sizes" )      // Bytes of a keyframe per element class
        {
            static std::vector<uint8_t> buf( 1u<<20 );
            QMap<QString,QPair<int,int>> per;                    // class -> (count, bytes)
            for( eElement* el : Simulator::self()->elementList() )
            {
                StateIO s( buf.data(), buf.size() );
                el->simState( s );
                auto& v = per[ SimHistory::className( el ) ];
                v.first++; v.second += (int)s.size()+4;        // + its marker
            }
            QJsonObject o;
            int total = 0;
            for( auto it = per.begin(); it != per.end(); ++it ){
                o[ it.key() ] = QString("%1 × → %2 B").arg( it.value().first ).arg( it.value().second );
                total += it.value().second;
            }
            o["(elements)"] = total;
            o["(keyframe)"] = (double)SimHistory::self()->lastSize();
            return o;
        }
        if( !c.pos.isEmpty() ) throw CliError("sim history [check]");
        SimHistory* h = SimHistory::self();
        QJsonObject o{
            {"on",         h->enabled() },
            {"usable",     h->usable() },
            {"keyframes",  h->count() },
            {"from",       Cli::timeStr( h->firstTime() ) },
            {"to",         Cli::timeStr( h->lastTime() ) },
            {"bytes",      (double)h->bytesUsed() },
            {"budget",     (double)h->budget() },
            {"last_size",  (double)h->lastSize() },
            {"captures",   (double)h->captures() },
            {"capture_us_mean", qRound( h->meanUs()*100 )/100.0 },
            {"capture_us_max",  qRound( h->maxUs()*100 )/100.0 },
            {"skipped",    (double)h->skipped() } };
        if( !h->missing().isEmpty() ) o["missing"] = QJsonArray::fromStringList( h->missing() );
        if( !h->lastError().isEmpty() ) o["error"] = h->lastError();
        return o;
    });

    add("sim start", "sim start", "Power the circuit on (or resume if paused).",
        []( CliCtx& ) -> QJsonValue
    {
        startOrResume();
        return simStatus();
    });

    add("sim stop", "sim stop", "Power the circuit off.",
        []( CliCtx& ) -> QJsonValue
    {
        CircuitWidget::self()->powerCircOff();
        return simStatus();
    });

    add("sim pause", "sim pause", "Pause a running simulation.",
        []( CliCtx& ) -> QJsonValue
    {
        Simulator* sim = Simulator::self();
        if( !sim->isRunning() ) throw CliError("simulation is not running");
        if( !sim->isPaused() ) CircuitWidget::self()->pauseCirc();
        return simStatus();
    });

    add("sim resume", "sim resume", "Resume a paused simulation.",
        []( CliCtx& ) -> QJsonValue
    {
        Simulator* sim = Simulator::self();
        if( !sim->isPaused() ) throw CliError("simulation is not paused");
        CircuitWidget::self()->pauseCirc();
        return simStatus();
    });

    add("sim run", "sim run --for <time>", "Run (starting or resuming as needed) for a span of simulated time, then pause. "
        "Replies when done. Time examples: 500us, 10ms, 2s.",
        []( CliCtx& c ) -> QJsonValue
    {
        QString span = c.opt("for", c.argOr( 0, "") );
        if( span.isEmpty() ) throw CliError("missing --for <time>");
        uint64_t dur = Cli::parseTime( span );
        if( dur == 0 ) throw CliError("time must be > 0");

        Simulator* sim = Simulator::self();
        startOrResume();
        if( !sim->isRunning() ) throw CliError("simulation could not start");

        uint64_t target = sim->circTime() + dur;
        sim->setStopAt( target );

        CliPendingPtr reply = c.defer();
        frameCounter();
        auto pausedAtFrame = std::make_shared<int64_t>( -1 );
        int generation = Cli::runGeneration();
        Cli::pollUntil( [reply, target, pausedAtFrame, generation]() -> bool
        {
            Simulator* sim = Simulator::self();
            if( Cli::runGeneration() != generation ){
                if( sim ) sim->setStopAt( 0 );
                reply->fail("run cancelled");
                return true;
            }
            if( !sim->isRunning() ){
                reply->fail("simulation stopped before reaching "+Cli::timeStr( target ) );
                return true;
            }
            if( *pausedAtFrame < 0 )
            {
                if( sim->circTime() < target ) return false;
                if( !sim->isPaused() ) CircuitWidget::self()->pauseCirc();
                sim->setStopAt( 0 );
                *pausedAtFrame = (int64_t)frameCounter()->frames();
                return false;
            }
            if( (int64_t)frameCounter()->frames() <= *pausedAtFrame ) return false; // Let displays update

            reply->ok( simStatus() );
            return true;
        });
        return QJsonValue();
    });

    // Skip (specs/sim-control.md §3.4): as the → key and the toolbar's skip button
    auto skipCmd = []( CliCtx& c, uint64_t span, int count, bool back ) -> QJsonValue
    {
        Simulator* sim = Simulator::self();
        uint64_t before = sim->isRunning() ? sim->circTime() : 0;
        QString err = back ? SimSkip::self()->startBack( span, count ) : SimSkip::self()->start( span, count );
        if( !err.isEmpty() ) throw CliError( err );
        CliPendingPtr reply = c.defer();
        SimSkip::self()->whenDone( [reply, before]( bool ok, QString msg )
        {
            if( !ok ){ reply->fail( msg ); return; }
            QJsonObject o = simStatus();
            o["from"] = Cli::timeStr( before );
            uint64_t now = Simulator::self()->circTime();
            o["skipped"] = ( now >= before ) ? Cli::timeStr( now-before ) : "-"+Cli::timeStr( before-now );
            o["note"] = msg;
            reply->ok( o );
        });
        return QJsonValue();
    };

    add("sim skip", "sim skip [<time>|step] [--count N] [--back]", "Pause and skip forward by a time span, "
        "or by the skip interval (settings skipInterval) without one, as fast as the computer can, then pause "
        "and reply after one frame. 'step' = to the next solver event. --count N skips N times as far "
        "(N solver steps). Starts a stopped simulation from 0. --back: go back as far, with the keyframes of the run "
        "(settings history): to the keyframe at or before the target, then on to it, paused.",
        [skipCmd]( CliCtx& c ) -> QJsonValue
    {
        uint64_t span = Simulator::self()->skipInterval();
        if( !c.pos.isEmpty() && !SimSkip::parseInterval( c.pos.at(0), &span ) )
            throw CliError("invalid skip: "+c.pos.at(0)+" (step, or 1ps … 1min, e.g. 5us)");
        int count = 1;
        QString n = c.opt("count", "1");
        bool ok = false;
        count = n.toInt( &ok );
        if( !ok || count < 1 || count > 1000000 ) throw CliError("--count must be 1 … 1000000");
        return skipCmd( c, span, count, c.has("back") );
    });

    add("sim step", "sim step [--count N] [--back]", "One solver step: run the next pending event time, solve, pause "
        "(= sim skip step).",
        [skipCmd]( CliCtx& c ) -> QJsonValue
    {
        bool ok = false;
        int count = c.opt("count", "1").toInt( &ok );
        if( !ok || count < 1 || count > 1000000 ) throw CliError("--count must be 1 … 1000000");
        return skipCmd( c, 0, count, c.has("back") );
    });

    add("sim speed", "sim speed [percent]", "Get or set target simulation speed in % of real time (100 = real time).",
        []( CliCtx& c ) -> QJsonValue
    {
        if( !c.pos.isEmpty() )
        {
            double pct = Cli::parseNum( c.pos.at(0), "speed");
            if( pct <= 0 ) throw CliError("speed must be > 0");
            QString note = Cli::setSimSpeed( (uint64_t)( pct*1e10 ) );
            QJsonObject o = simStatus();
            if( !note.isEmpty() ) o["note"] = note;
            return o;
        }
        return simStatus();
    });
}

