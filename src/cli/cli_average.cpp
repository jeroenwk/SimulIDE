/***************************************************************************
 *   SimulIDE command line control interface                              *
 *   sim average: time-weighted mean, RMS, min and max over a run          *
 ***( see copyright.txt file at root folder )*******************************/

// Scope samples sit on a fixed time grid, so the mean of a switching waveform taken from them
// can be 10 % off (bug hunt IMP-016). Here every converged solver step counts: an Averager
// element is called back when one of the measured nodes changes, asks for stepSettled(), and
// integrates the settled values as a piecewise-constant signal over simulated time.
//
// Node callbacks are attached and removed in updateStep(), which runs on the GUI thread while
// the simulation worker is idle; stepSettled() runs on the worker. The results are read once
// the run has paused, again from updateStep().

#include <cmath>
#include <memory>

#include <QDebug>
#include <QJsonArray>

#include "cliapi.h"
#include "clirefs.h"
#include "circuitwidget.h"
#include "component.h"
#include "e-element.h"
#include "e-node.h"
#include "pin.h"
#include "simulator.h"
#include "updatable.h"

#define AVG_LOG qDebug() << "[cli:average]"

namespace
{
QJsonValue num( double v ) { return std::isfinite( v ) ? QJsonValue( v ) : QJsonValue(); }

struct Channel           // One measured quantity
{
    QString ref;         // R1.lPin, or R1 for a two-pin component
    QString kind;        // "pin" (voltage) or "comp" (voltage across pin 0 - pin 1, current into pin 0)
    Pin* p0 = nullptr;
    Pin* p1 = nullptr;

    double v = 0, i = 0;                                   // Value held since lastT
    double sumV = 0, sumV2 = 0, sumI = 0, sumI2 = 0;       // Integrals (unit * ps)
    double minV = INFINITY, maxV = -INFINITY, minI = INFINITY, maxI = -INFINITY;

    void read()
    {
        v = p1 ? p0->getVoltage() - p1->getVoltage() : p0->getVoltage();
        if( kind == "comp" ) i = p0->getCurrent();
        minV = std::min( minV, v ); maxV = std::max( maxV, v );
        if( kind == "comp" ){ minI = std::min( minI, i ); maxI = std::max( maxI, i ); }
    }
    void hold( double dt )   // The held values lasted dt ps
    {
        sumV += v*dt; sumV2 += v*v*dt;
        if( kind == "comp" ){ sumI += i*dt; sumI2 += i*i*dt; }
    }
};

class Averager : public eElement, public Updatable
{
    public:
        Averager( QList<Channel> chans, uint64_t span )
            : eElement("cli-averager"), m_ch( chans ), m_span( span ) {}

        ~Averager() { detach(); }

        enum Phase { WAIT_START, RUNNING, PAUSED, DONE };

        virtual void voltChanged() override { Simulator::self()->addToSettled( this ); }

        virtual void stepSettled() override       // Worker thread, once per converged step
        {
            if( m_phase != RUNNING ) return;
            uint64_t t = Simulator::self()->circTime();
            if( t > m_end ) t = m_end;
            advance( t );
            for( Channel& c : m_ch ) c.read();
            m_steps++;
        }

        virtual void updateStep() override        // GUI thread, worker idle
        {
            Simulator* sim = Simulator::self();
            if( m_phase == WAIT_START && sim->isRunning() )
            {
                attach();
                m_start = m_last = sim->circTime();
                m_end = m_start + m_span;
                for( Channel& c : m_ch ) c.read();
                sim->setStopAt( m_end );
                m_phase = RUNNING;
                AVG_LOG << "start at" << m_start << "ps, end at" << m_end << "ps," << m_ch.size() << "channel(s)";
            }
            else if( m_phase == RUNNING && sim->circTime() >= m_end )
            {
                if( !sim->isPaused() ) CircuitWidget::self()->pauseCirc();
                sim->setStopAt( 0 );
                advance( m_end );                  // The last values held up to the end
                detach();
                m_phase = PAUSED;
                AVG_LOG << "done:" << m_steps << "settled steps over" << (m_end-m_start) << "ps";
            }
        }

        Phase phase() { return m_phase; }
        void setDone() { m_phase = DONE; }

        QJsonValue result()
        {
            double T = (double)(m_end - m_start);
            QJsonArray list;
            for( Channel& c : m_ch )
            {
                QJsonObject o{ {"ref", c.ref} };
                QJsonObject v{ {"mean", num( c.sumV/T )}, {"rms", num( std::sqrt( c.sumV2/T ) )},
                               {"min", num( c.minV )}, {"max", num( c.maxV )} };
                o["voltage"] = v;
                if( c.kind == "comp" )
                    o["current"] = QJsonObject{ {"mean", num( c.sumI/T )}, {"rms", num( std::sqrt( c.sumI2/T ) )},
                                                {"min", num( c.minI )}, {"max", num( c.maxI )} };
                list.append( o );
            }
            return QJsonObject{ {"from", Cli::timeStr( m_start )}, {"to", Cli::timeStr( m_end )},
                                {"span_ps", QString::number( m_end - m_start )},
                                {"steps", (double)m_steps}, {"channels", list} };
        }

    private:
        void advance( uint64_t t )
        {
            if( t <= m_last ) return;
            double dt = (double)(t - m_last);
            for( Channel& c : m_ch ) c.hold( dt );
            m_last = t;
        }
        void attach()
        {
            for( Channel& c : m_ch )
                for( Pin* p : { c.p0, c.p1 } )
                    if( p && p->getEnode() ){ p->getEnode()->voltChangedCallback( this ); m_nodes.append( p->getEnode() ); }
        }
        void detach()
        {
            for( eNode* n : m_nodes ) n->remFromChangedCallback( this );
            m_nodes.clear();
        }

        QList<Channel> m_ch;
        QList<eNode*>  m_nodes;
        uint64_t m_span, m_start = 0, m_end = 0, m_last = 0, m_steps = 0;
        Phase m_phase = WAIT_START;
};
}

void CliApi::registerAverage()
{
    add("sim average", "sim average --for <time> <pin|comp> [...]",
        "Run (starting or resuming as needed) for a span of simulated time, then pause, and give the time-weighted "
        "mean, RMS, min and max of every converged solver step: the voltage at a pin (R1.lPin), or for a two-pin "
        "component (R1, C1, L1) the voltage across it (pin 0 - pin 1) and the current into pin 0. Exact for "
        "switching waveforms, unlike means of scope samples.",
        []( CliCtx& c ) -> QJsonValue
    {
        QString span = c.opt("for", "");
        if( span.isEmpty() ) throw CliError("missing --for <time>");
        uint64_t dur = Cli::parseTime( span );
        if( dur == 0 ) throw CliError("time must be > 0");
        if( c.pos.isEmpty() ) throw CliError("missing pin or component (e.g. C1, R1.lPin)");

        QList<Channel> chans;
        for( QString ref : c.pos )
        {
            Channel ch; ch.ref = ref;
            if( ref.contains('.') ){
                ch.kind = "pin"; ch.p0 = CliRefs::pin( ref );
            }else{
                Component* comp = CliRefs::comp( ref );
                std::vector<Pin*> pins = comp->getPins();
                if( pins.size() != 2 ) throw CliError( ref+" has "+QString::number( pins.size() )
                                                       +" pins: name a pin (e.g. "+ref+".<pin>) instead");
                ch.kind = "comp"; ch.p0 = pins[0]; ch.p1 = pins[1];
            }
            chans.append( ch );
        }
        Simulator* sim = Simulator::self();
        if( !sim->isRunning() )    CircuitWidget::self()->powerCircOn();
        else if( sim->isPaused() ) CircuitWidget::self()->pauseCirc();   // Toggles: resumes
        if( !sim->isRunning() ) throw CliError("simulation could not start");

        for( Channel& ch : chans )               // Nodes exist only once the simulation has started
            for( Pin* p : { ch.p0, ch.p1 } )
                if( p && !p->getEnode() ){
                    AVG_LOG << CliRefs::pinRef( p ) << "has no node";
                    throw CliError( CliRefs::pinRef( p )+" is not connected");
                }

        auto av = std::make_shared<Averager>( chans, dur );
        sim->addToUpdateList( av.get() );
        AVG_LOG << "average over" << span << "of" << c.pos;

        CliPendingPtr reply = c.defer();
        int generation = Cli::runGeneration();
        Simulator* owner = sim;
        auto pausedFrames = std::make_shared<int>( 0 );
        Cli::pollUntil( [reply, av, generation, owner, pausedFrames]() -> bool
        {
            Simulator* sim = Simulator::self();
            auto finish = [&](){ if( sim == owner ){ sim->remFromUpdateList( av.get() ); sim->setStopAt( 0 ); } };
            if( sim != owner || Cli::runGeneration() != generation ){
                finish(); reply->fail("average cancelled"); return true;
            }
            if( !sim->isRunning() ){
                finish(); reply->fail("simulation stopped before the end of the span"); return true;
            }
            if( av->phase() != Averager::PAUSED ) return false;
            if( ++*pausedFrames < 2 ) return false;          // Let displays update, as sim run does
            finish();
            av->setDone();
            reply->ok( av->result() );
            return true;
        });
        return QJsonValue();
    });
}
