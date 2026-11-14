/***************************************************************************
 *   SimulIDE command line control interface                              *
 *   Simulation control commands                                           *
 ***( see copyright.txt file at root folder )*******************************/

#include "cliapi.h"
#include "circuitwidget.h"
#include "simulator.h"
#include "updatable.h"
#include "noise.h"

#include <QJsonArray>

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

