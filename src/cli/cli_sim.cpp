/***************************************************************************
 *   SimulIDE command line control interface                              *
 *   Simulation control commands                                           *
 ***( see copyright.txt file at root folder )*******************************/

#include "cliapi.h"
#include "circuitwidget.h"
#include "simulator.h"

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
    return o;
}

void startOrResume()
{
    Simulator* sim = Simulator::self();
    if( !sim->isRunning() )    CircuitWidget::self()->powerCircOn();
    else if( sim->isPaused() ) CircuitWidget::self()->pauseCirc();   // Toggles: resumes
}
}

void CliApi::registerSim()
{
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
        Cli::pollUntil( [reply, target]() -> bool
        {
            Simulator* sim = Simulator::self();
            if( !sim->isRunning() ){
                reply->fail("simulation stopped before reaching "+Cli::timeStr( target ) );
                return true;
            }
            if( sim->circTime() < target ) return false;

            if( !sim->isPaused() ) CircuitWidget::self()->pauseCirc();
            sim->setStopAt( 0 );
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
            Simulator::self()->setPsPerSec( (uint64_t)( pct*1e10 ) );
        }
        return simStatus();
    });
}

void CliApi::registerMcu() {}
