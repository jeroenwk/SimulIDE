/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <qtconcurrentrun.h>
#include <QHash>
#include <math.h>

#include "simulator.h"
#include "editorwindow.h"
#include "circuit.h"
#include "updatable.h"
#include "outpaneltext.h"
#include "mainwindow.h"
#include "infowidget.h"
#include "circuitwidget.h"
#include "circmatrix.h"
#include "e-element.h"
#include "socket.h"
#include "noise.h"
#include "e-reactive.h"
#include "e-coil.h"
#include "e-junctioncap.h"
#include "simstate.h"
#include "simhistory.h"
#include <random>

Simulator* Simulator::m_pSelf = NULL;

Simulator::Simulator( QObject* parent )
         : QObject( parent )
{
    m_pSelf = this;

    m_matrix = new CircMatrix();

    m_fps = 20;
    m_timerId   = 0;
    m_timerTick_ms = 50;   // 50 ms default
    m_psPerSec  = 1e12;
    m_stepSize  = 1e6;
    m_stepsPS   = 1e6;
    m_reactStep = 1e6;
    m_maxNlstp  = 100000;
    m_nlBudget  = m_maxNlstp;
    m_slopeSteps = 0;
    m_supplyRamp = 0;

    m_errors[0] = "";
    //m_errors[1] = "Could not solve Matrix";
    m_errors[2] = "Add Event: NULL free event";
    m_errors[3] = "LAST_SIM_EVENT reached";

    m_warnings[1] = "NonLinear Not Converging";
    m_warnings[2] = "Probably Circuit Error";  // Warning if matrix diagonal element = 0.
    m_warnings[100] = "AVR crashed !!!";

    resetSim();
    CircuitWidget::self()->setMsg( " "+tr("Stopped")+" ", 1 );
    CircuitWidget::self()->syncSkip( m_skipInterval );   // A new circuit: the default interval

    m_RefTimer.start();
}
Simulator::~Simulator()
{
    m_CircuitFuture.waitForFinished();
    SimHistory::self()->stop();
    stopNoise();
    delete m_matrix;
    if( m_pSelf == this ) m_pSelf = NULL; // Elements destroyed later must not use a dangling pointer
}

inline void Simulator::solveMatrix()
{
    while( m_changedNode ){
        m_changedNode->stampMatrix();
        m_changedNode = m_changedNode->nextCH;
    }
    //if( !m_matrix->solveMatrix() ) // m_matrix sets the eNode voltages
    //    m_warning = 2;             // Warning if diagonal element = 0.
    // A step still iterating after 50 passes: solve the ill-conditioned groups with iterative
    // refinement (CircMatrix::refine), in case rounding is what keeps it from settling
    m_matrix->setRefine( m_NLstep >= 50 );
    m_matrix->solveMatrix(); // m_matrix sets the eNode voltages
}

void Simulator::timerEvent( QTimerEvent* e )  //update at m_timerTick_ms rate (50 ms, 20 Hz max)
{
    e->accept();

    if( m_state == SIM_WAITING ) return;

    uint64_t currentTime = m_RefTimer.nsecsElapsed();
    double fps = 1e9/(currentTime-m_timerTime);
    m_realFPS = (m_realFPS*9+fps)/10;
    m_timerTime = currentTime;

    if( m_error )
    {
        CircuitWidget::self()->powerCircOff();
        CircuitWidget::self()->setError( m_errors.value( m_error ) );
        return;
    }
    else if( m_warning > 0 )
    {
        int type = (m_warning < 100)? 1:2;
        CircuitWidget::self()->setMsg( m_warnings.value( m_warning), type );
        m_warning = -10;
    }
    else if( m_warning < 0 )
    { if( ++m_warning == 0 ) CircuitWidget::self()->setMsg( " "+tr("Running")+" ", 0 ); }

    holdWorker();                       // Stop remaining parallel thread, at the end of a step
    if( m_state == SIM_RUNNING && SimHistory::self()->usable() ) SimHistory::self()->capture( this );
    if( m_debug && m_state == SIM_PAUSED ) CircuitWidget::self()->debugPaused();
    if( m_noiseEl ) CircuitWidget::self()->updateNoise();   // Armed / on / ended
    if( m_speedLock ) CircuitWidget::self()->updateLock();   // The "step ×N" badge

    for( Updatable* el : m_updateList ) el->updateStep();
    EditorWindow::self()->outPane()->updateStep(); // OutPanel in Editor can be created before this simulator.

    // Calculate Simulation Load
    double timer_ns = m_timerTick_ms*1e6;
    uint64_t simLoop = 0;
    if( m_loopTime > m_refTime ) simLoop = m_loopTime-m_refTime;
    m_simLoad = (m_simLoad+100*simLoop/timer_ns)/2;

    // Get Simulation times
    m_simPsPF = m_circTime-m_tStep;
    m_tStep   = m_circTime;

    if( m_state == SIM_RUNNING ) speedLockFrame();   // Worker idle: the step factor can change
    m_lockRan = ( m_state == SIM_RUNNING );

    if( m_state == SIM_RUNNING ) // Run Circuit in a parallel thread
        m_CircuitFuture = QtConcurrent::run( this, &Simulator::runCircuit );

    if( Circuit::self()->animate() ) // Moved here to be in parallel with runCircuit thread
    {
        if( (m_timerTime-m_updtTime) >= 2e8 ){ // Animate at 5 FPS
            //Circuit::self()->updateConnectors();
            for( eNode* node : m_eNodeList) node->updateConnectors();
            m_updtTime = m_timerTime;
        }
    }
    // Calculate Real Simulation Speed
    m_refTime  = m_RefTimer.nsecsElapsed();
    if( m_speedBase && m_state == SIM_RUNNING )   // The worker starts with this frame: the window too
    {
        m_speedBase = false;
        m_lastRefT = m_refTime;
        m_lastStep = m_tStep;
    }
    uint64_t deltaRefTime = m_refTime-m_lastRefT;
    if( deltaRefTime >= 1e9 )               // We want steps per 1 Sec = 1e9 ns
    {
        double guiLoad = 100*(double)m_guiTime/(double)deltaRefTime;
        m_guiTime = 0;

        m_realSpeed = (m_tStep-m_lastStep)*10.0/deltaRefTime;
        InfoWidget::self()->setRate( m_realSpeed, m_simLoad, guiLoad, m_realFPS+0.5 );
        m_lastStep = m_tStep;
        m_lastRefT = m_refTime;
    }
    InfoWidget::self()->setCircTime( m_tStep );

    m_guiTime += m_RefTimer.nsecsElapsed()-m_timerTime; // Time in this function
}

void Simulator::runCircuit()
{
    solveCircuit(); // Solve any pending changes
    if( !m_startSettle.isEmpty() ) startSettle();
    runSettled();
    if( m_state < SIM_RUNNING ) return;

    eElement* event = m_firstEvent;
    uint64_t endRun = m_circTime + m_psPF; // Run upto next Timer event
    if( m_fastRun && m_stopAt > m_circTime ) endRun = m_stopAt; // A skip: to its end as fast as it can
    if( m_resumeRun && m_runEnd > m_circTime ) endRun = m_runEnd;   // Held by a CLI command: same end
    m_resumeRun = false;
    m_runEnd = endRun;
    if( m_stopAt && endRun > m_stopAt ) endRun = (m_stopAt > m_circTime) ? m_stopAt : m_circTime;
    uint64_t nextTime;

    while( event )                              // Simulator event loop
    {
        if( event->eventTime > endRun ) break;  // All events for this Timer Tick are done

        nextTime = m_circTime;
        while( m_circTime == nextTime )         // Run all event with same timeStamp
        {
            m_circTime = event->eventTime;
            m_lastEvent = m_circTime;
            m_firstEvent = event->nextEvent;    // free Event
            event->nextEvent = NULL;
            event->eventTime = 0;
            event->runEvent();                  // Run event callback
            event = m_firstEvent;
            if( event ) nextTime = event->eventTime;
            else break;
        }
        solveCircuit();
        runSettled();
        if( m_state < SIM_RUNNING ) break;
        event = m_firstEvent;               // m_firstEvent can be an event added at solveCircuit()
    }
    if( m_state > SIM_WAITING ) m_circTime = endRun;
    m_loopTime = m_RefTimer.nsecsElapsed();
}

void Simulator::runSettled()   // The step is solved: elements that asked see its final values
{
    if( m_stepWatch ) addToSettled( m_stepWatch );
    if( !stepComplete() ) return;   // Left in the middle of the step: values not final. The list
                                    // stays: the step is finished in the next run
    eElement* el = m_settled;
    m_settled = nullptr;
    m_settledGen++;
    while( el ){
        eElement* next = el->nextSettled;
        el->stepSettled();
        el = next;
    }
}

void Simulator::startSettle()  // First step: logic gates settle one by one, with a solve between
{
    QList<eElement*> list = m_startSettle;
    m_startSettle.clear();
    for( eElement* el : list )
    {
        el->startSettled();
        solveCircuit();
        if( m_state < SIM_RUNNING ) return;
    }
}

int Simulator::randomStart( eElement* el )
{
    return m_noiseEl ? m_noiseEl->startState( el->getId() ) : -1;
}

uint64_t Simulator::noiseSeed( eElement* el, const QString& tag )
{
    return m_noiseEl ? m_noiseEl->seedFor( el->getId(), tag ) : 0;
}

void Simulator::solveCircuit()
{
    while( m_changedNode || m_nonLinear || !m_converged ) // Also Proccess changes gererated in voltChanged()
    {
        if( m_changedNode ) solveMatrix();

        if( m_converged ) m_converged = m_nonLinear==NULL;
        while( !m_converged )              // Non Linear Components
        {
            m_converged = true;
            while( m_nonLinear ){
                if( m_nlBudget && m_NLstep+1 >= m_nlBudget && m_nlIds.size() < 16 )   // Last pass: who is
                {                                                                     // still iterating
                    QString id = m_nonLinear->getId();
                    if( !m_nlIds.contains( id ) ) m_nlIds.append( id );
                }
                m_nonLinear->added = false;
                m_nonLinear->voltChanged();
                m_nonLinear = m_nonLinear->nextChanged;
            }
            if( m_nlBudget && (m_NLstep++ >= m_nlBudget) )  // Max iterations reached: take the step
            {                                               // unconverged, report it (Messages), and
                m_warning = 1;                              // allow the next steps 1000 iterations
                {                                           // until one converges: no long stall on
                    QMutexLocker lock( &m_nlLock );         // each bad step, and no step left with
                    if( m_nlReports.size() < 100 )          // a single pass (stuck)
                        m_nlReports.append({ m_circTime, m_NLstep, m_nlIds });
                }
                m_nlIds.clear();
                m_NLstep = 0;
                m_nlBudget = qMin( m_maxNlstp, (uint32_t)1000 );
                return;
            }
            if( m_state < SIM_RUNNING && breakStep() ){ m_converged = false; break; } // Loop broken without converging
            if( m_changedNode ) solveMatrix();
        }
        if( !m_converged ) return; // Don't run linear until nonliear converged (Loop broken)

        m_NLstep = 0;
        m_nlBudget = m_maxNlstp;                   // Converged: full budget again
        while( m_voltChanged )
        {
            m_voltChanged->added = false;
            m_voltChanged->voltChanged();
            m_voltChanged = m_voltChanged->nextChanged;
        }
        if( m_state < SIM_RUNNING && breakStep() ) break;    // Loop broken without converging
    }
}

QList<Simulator::NlReport> Simulator::takeNlReports()
{
    QMutexLocker lock( &m_nlLock );
    QList<NlReport> r = m_nlReports;
    m_nlReports.clear();
    return r;
}

void Simulator::resetSim()
{
    m_state    = SIM_STOPPED;
    m_simLoad  = 0;
    m_guiTime  = 0;
    m_error    = 0;
    m_warning  = 0;
    m_lastStep = 0;
    m_tStep    = 0;
    m_lastRefT = 0;
    m_circTime = 1;
    m_stopAt   = 0;
    m_fastRun  = false;
    m_updtTime = 0;
    m_NLstep   = 0;
    m_converged = true;  // A stop during the non-linear loop leaves it false
    m_nlBudget  = m_maxNlstp;
    m_nlIds.clear();
    { QMutexLocker lock( &m_nlLock ); m_nlReports.clear(); }
    ///m_pauseCirc = false;
    m_simPsPF = 1;

    InfoWidget::self()->setCircTime( 0 );
    clearEventList();
    m_changedNode = NULL;
    m_voltChanged = NULL;
    m_nonLinear = NULL;
    m_settled = nullptr;      // Elements may be gone: drop the list, a new generation forgets the flags
    m_settledGen++;
    m_startSettle.clear();
    m_stepWatch = nullptr;
}

void Simulator::createNodes()
{
    //qDebug() <<"\ncreateNodes...\n";
    for( eNode* enode : m_eNodeList )
    {
        enode->clear();
        delete enode;
    }
    m_eNodeList.clear();

    int i = 0;
    QStringList pinList;
    QStringList pinNames = Circuit::self()->m_pinMap.keys();
    pinNames.sort();
    for( QString pinName : pinNames )
    {
        Pin* pin = Circuit::self()->m_pinMap.value( pinName );
        if( !pin ) continue;
        if( pinList.contains( pinName ) ) continue;
        if( !pin->conPin() ) continue;
        if( pin->isBus() ) continue;
        if( pinName.startsWith("Node") ) continue;

        eNode* node = new eNode( "eNodeSim-"+QString::number(i) );
        i++;
        //qDebug() <<"--------------createNode "<<i<<node->itemId();
        pin->registerPinsW( node );
        pin->registerEnode( node );
        for( ePin* nodePin : node->getEpins() )
        {
            QString pinId = nodePin->getId();//qDebug() <<pinId<<"\t\t\t"<<nodePin->getEnode()->itemId();
            if( pinId.startsWith("Node") ) continue;
            if( !pinList.contains(pinId) ) pinList.append( pinId );
        }
    }
    /// qDebug() <<"  Created      "<< i << "\teNodes"<<pinList.size()<<"Pins";
}

void Simulator::startSim( bool paused )
{
    stopNoise();          // A restart without a stop
    resetSim();
    m_runSeed = m_runSeedSet;
    while( m_runSeed == 0 ) m_runSeed = std::random_device{}();
    std::srand( m_runSeed );   // Before initialize(): flip-flops draw their start state there
    setPsPerSec( m_psPerSec );
    m_debug = paused;
    m_state = SIM_STARTING;

    qDebug() <<"\nStarting Circuit Simulation...\n";

    for( Socket* sock : m_socketList ) sock->updatePins( true );

    createNodes();
    startNoise();         // Last in the element list: it stamps after every part

    /// qDebug() <<"  Initializing "<< m_elementList.size() << "\teElements";
    for( eElement* el : m_elementList )    // Initialize all Elements
    {                                      // This can create new eNodes
        //qDebug() << "initializing  "<< el->getId();
        el->eventTime = 0;
        el->clearChanged();                // The same start state on every run (the first after opening differed)
        el->initialize();
        el->added = false;
    }

    /// qDebug() <<"  Initializing "<< m_eNodeList.size()<< "\teNodes";
    for( int i=0; i<m_eNodeList.size(); i++ )         // Initialize eNodes
    {
        eNode* enode = m_eNodeList.at(i);
        enode->setNodeNumber( i+1 );
        enode->initialize();
        //qDebug() << "initializing  "<< enode->itemId();
    }
    for( eElement* el : m_elementList ) el->stamp();
    for( int i=0; i<m_elementList.size(); ++i ) m_elementList.at( i )->simIndex = i;
    m_reactive.clear();   // Speed lock: the steps it can coarsen
    for( eElement* el : m_elementList )
        if( eReactive* r = dynamic_cast<eReactive*>( el ) ) m_reactive.append( r );
    m_stepScale = 1;
    m_lockMaxScale = 1;
    m_lockSlow = m_lockIdle = 0;
    m_lockLimit = false;
    m_lockRan = false;
    m_lockCap = lockMaxStep();
    if( m_noiseEl )       // Messages: what makes noise, and a reactStep that can't follow it
    {
        qInfo().noquote() << "Noise on:" << m_noiseEl->sources() << "sources ("
            +QString::number( m_noiseEl->count( NoiseSrc::Thermal ) )+" thermal, "
            +QString::number( m_noiseEl->count( NoiseSrc::Shot ) )+" shot, "
            +QString::number( m_noiseEl->count( NoiseSrc::Channel ) )+" channel), "+noiseSummary();
        int caps = 0, typical = 0;
        for( eElement* el : m_elementList )
            if( eJunctionCap* j = dynamic_cast<eJunctionCap*>( el ) )
                if( j->cjUsed() > 0 ){ caps++; if( j->cj0() <= 0 ) typical++; }
        if( caps ) qInfo().noquote() << "Noise: junction or winding capacitance on"<< caps << "places ("
                                     +QString::number( typical )+" with typical values, the part's Cj = 0); "
                                     +"capacitors and inductors use the noise step "+siStr( m_noise.holdPs()*1e-12, "s")+" in the window";
        for( QString e : m_noiseEl->unmatched() )
            qWarning().noquote() << "[noise] noiseOff: no part or type matches"<< e;
        uint64_t hold = m_noise.holdPs();
        bool coils = false;      // Transformers keep reactStep (capacitors and inductors follow the noise step)
        for( eElement* el : m_elementList )
            if( dynamic_cast<eCoil*>( el ) ){ coils = true; break; }
        if( coils && m_reactStep > hold )
            qWarning().noquote() << "[noise] noise bandwidth"<< siStr( m_noise.bw, "Hz")
                << "needs reactStep <="<< siStr( hold*1e-12, "s")<< "(now"<< siStr( m_reactStep*1e-12, "s")
                +"): transformers integrate at reactStep, their noise response will be off";
    }

    m_matrix->createMatrix( m_eNodeList );
    SimHistory::self()->start( this );   // Which elements can't be saved yet

    /// qDebug() << "\nCircuit Matrix looks good";

    /*double sps100 = 100*(double)m_psPerSec/1e12; // Speed %

    qDebug()  << "\nSpeed:" <<         sps100      << "%"
              << "\nSpeed:" << (double)m_psPerSec  << "\tps per Sec"
              << "\nFPS:  " <<         m_fps       << "\tFrames per Sec"
              << "\nFrame:" << (double)m_psPF      << "\tps per Frame"
              << "\nNonLi:" << (double)m_maxNlstp  << "\tMax Iterations"
              << "\nReact:" << (double)m_reactStep << "\tps Reactive step";*/

    qDebug() << "    Simulation Running... \n";

    if( paused ) // We are debugging
    {
        m_oldState = SIM_RUNNING;
        m_state    = SIM_PAUSED;
    }
    else m_state = SIM_RUNNING;

    if( m_timerId != 0 ) this->killTimer( m_timerId );               // Stop Timer
    m_refTime  = m_RefTimer.nsecsElapsed();
    m_loopTime = m_refTime;
    m_timerTime = m_loopTime;
    m_speedBase = true;         // Real speed measured from the first frame (timerEvent): 0 counted the
                                // time since the program started, startSim() one frame the worker hadn't run
    m_realFPS = m_fps;
    m_timerId = this->startTimer( m_timerTick_ms, Qt::PreciseTimer ); // Init Timer
}

void Simulator::stopSim()
{
    if( m_timerId != 0 ){                   // Stop Timer
        this->killTimer( m_timerId );
        m_timerId = 0;
    }
    m_state = SIM_STOPPED;
    if( !m_CircuitFuture.isFinished() ) m_CircuitFuture.waitForFinished();
    stopNoise();

    qDebug() << "\n    Simulation Stopped ";
    qDebug() << "\n-------------------------------------------------\n ";

    for( eNode* node  : m_eNodeList  )  node->setVolt( 0 );
    for( eElement* el : m_elementList ) el->initialize();
    for( Updatable* el : m_updateList ) el->updateStep();

    clearEventList();
    m_startSettle.clear();
    m_changedNode = NULL;
    m_settled = nullptr;
    m_settledGen++;
    m_reactive.clear();
    m_stepScale = 1;
    m_lockLimit = false;
    SimHistory::self()->stop();
}

void Simulator::startNoise()
{
    m_noiseFrom = m_noiseTo = 0;
    if( !m_noise.on ) return;
    m_noiseSeed = m_noise.seed;
    while( m_noiseSeed == 0 ) m_noiseSeed = ( (uint64_t)std::random_device{}() << 32 ) ^ std::random_device{}();
    m_noiseSeed &= 0xFFFFFFFFFFFFULL;   // 48 bits: short enough to type back
    if( m_noiseSeed == 0 ) m_noiseSeed = 1;
    m_noiseEl = new eNoise( m_noise, m_noiseSeed );   // Adds itself to the element list
    if( CircuitWidget::self() ) CircuitWidget::self()->updateNoise();
}

void Simulator::stopNoise()
{
    if( !m_noiseEl ) return;
    eNoise* n = m_noiseEl;
    m_noiseFrom = n->firstActive();
    m_noiseTo   = n->activeUntil();
    m_noiseEl = nullptr;
    delete n;                           // Removes itself from the element and event lists
    if( CircuitWidget::self() ) CircuitWidget::self()->updateNoise();
}

bool Simulator::noiseActiveDuring( uint64_t t0, uint64_t t1 )
{
    uint64_t from = m_noiseFrom, to = m_noiseTo;
    if( m_noiseEl ){ from = m_noiseEl->firstActive(); to = m_noiseEl->activeUntil(); }
    return from && from <= t1 && to >= t0;
}

QString Simulator::siStr( double v, QString unit )
{
    static const char* pre[] = { "p","n","µ","m","","k","M","G","T" };
    if( v == 0 ) return "0 "+unit;
    int e = (int)std::floor( std::log10( std::fabs( v ) )/3 );
    if( e < -4 ) e = -4;
    if( e >  4 ) e =  4;
    return QString::number( v/std::pow( 1000, e ), 'g', 4 )+" "+pre[e+4]+unit;
}

double Simulator::parseSi( QString v, QString unit, bool* ok )
{
    QString t = v.trimmed();
    if( !unit.isEmpty() && t.endsWith( unit, Qt::CaseInsensitive ) ) t.chop( unit.size() );
    t = t.trimmed();
    double mult = 1;
    static const QString pre = "pnuµmkMGT";
    static const double  val[] = { 1e-12, 1e-9, 1e-6, 1e-6, 1e-3, 1e3, 1e6, 1e9, 1e12 };
    if( !t.isEmpty() ){
        int i = pre.indexOf( t.right(1) );
        if( i >= 0 ){ mult = val[i]; t.chop(1); }
    }
    double d = t.trimmed().toDouble( ok );
    return d*mult;
}

QString Simulator::noiseSummary()
{
    QString seed;
    if( m_noise.seed ) seed = "seed "+QString::number( m_noise.seed );
    else if( m_noiseEl && m_noiseSeed ) seed = "seed "+QString::number( m_noiseSeed )+" (random)";
    else seed = "random seed";
    QString win = siStr( m_noise.start*1e-12, "s" )+"–"+( m_noise.stop ? siStr( m_noise.stop*1e-12, "s" ) : QString("end") );
    return "×"+QString::number( m_noise.scale, 'g', 4 )+" · "+siStr( m_noise.bw, "Hz" )+" · "+seed+" · "+win;
}

QString Simulator::noiseBadge( bool brief )
{
    if( !m_noise.on ) return "";
    QString st = "armed";
    if( m_noiseEl ){
        eNoise::State s = m_noiseEl->state();
        st = ( s == eNoise::Active ) ? "on" : ( s == eNoise::Ended ) ? "ended" : "armed";
    }
    if( brief ){   // Toolbar: state, scale and window; the seed is in the tooltip
        QString win = siStr( m_noise.start*1e-12, "s" )+"–"+( m_noise.stop ? siStr( m_noise.stop*1e-12, "s" ) : QString("end") );
        return "NOISE "+st+" · ×"+QString::number( m_noise.scale, 'g', 4 )+" · "+win;
    }
    return "NOISE "+st+" · "+noiseSummary();
}

void Simulator::pauseSim() // Only pause simulation, don't update UI
{
    if( m_state <= SIM_PAUSED ) return;
    m_oldState = m_state;
    m_holdSince = m_RefTimer.nsecsElapsed();
    m_state = SIM_PAUSED;
}

// The GUI timer (each frame) and every CLI command call this before they read the circuit. The
// worker used to be stopped in the middle of a step: the frame then drew, and a CLI read could
// return, the values of an unfinished Newton iteration (a bridge diode of board_mains read 10 A
// where no solved step goes above 2.2 A, and Kirchhoff's current law broken by amps). It now
// finishes the step first, unless that takes over 50 ms (breakStep). A CLI command arriving while
// the worker runs waited for nothing at all and raced it; it now waits here too.
void Simulator::holdWorker()
{
    if( m_CircuitFuture.isFinished() ) return;
    if( m_state != SIM_RUNNING ){ m_CircuitFuture.waitForFinished(); return; }  // Pausing or stopping
    m_holdSince = m_RefTimer.nsecsElapsed();
    m_state = SIM_WAITING;
    m_CircuitFuture.waitForFinished();
    m_state = SIM_RUNNING;
}

// GUI thread, after a CLI command held the worker: run the rest of this frame's span now instead
// of at the next timer tick, so a script polling in a loop does not slow the simulation down.
// The parts' updateStep() runs first, as at each frame: a change the command made (a Push
// released, a property set) is applied there, and the worker must not run on without it
void Simulator::resumeWorker()
{
    if( m_state != SIM_RUNNING || !m_CircuitFuture.isFinished() || m_circTime >= m_runEnd ) return;
    for( Updatable* el : m_updateList ) el->updateStep();  // As each frame: what the command changed
    m_resumeRun = true;                                     // (a released Push, a property) applies first
    m_CircuitFuture = QtConcurrent::run( this, &Simulator::runCircuit );
}

void Simulator::resumeSim()
{
    if( m_state != SIM_PAUSED ) return;
    m_state = m_oldState;
    m_speedBase = true;         // The pause doesn't count in the real speed
}

/*void Simulator::stopTimer()
{
    if( m_timerId == 0 ) return;
    this->killTimer( m_timerId );
    m_timerId = 0;

    InfoWidget::self()->setRate( 0, 0 );
    CircuitWidget::self()->setMsg( " "+tr("Stopped")+" ", 1 );
    Circuit::self()->update();
    qDebug() << "\n    Simulation Stopped ";
    m_state = SIM_STOPPED;
}*/

/*void Simulator::initTimer()
{
    if( m_timerId != 0 ) return;
    CircuitWidget::self()->setMsg( " "+tr("Running")+" ", 0 );
    m_refTime  = m_RefTimer.nsecsElapsed();
    m_loopTime = m_refTime;
    m_timerId = this->startTimer( m_timerTick_ms, Qt::PreciseTimer );
    m_state = SIM_RUNNING;
}*/

void Simulator::setFps( uint64_t fps )
{
    m_fps = fps;
    setPsPerSec( m_psPerSec );
}

// Simulation settings (specs/sim-control.md §3.8): changed during a run, they are logged with the
// simulated time; the reactive step, slope steps, supply ramp and noise are read at the start
void Simulator::logRunChange( QString what, QString from, QString to, bool nextStart )
{
    if( !isRunning() || from == to ) return;
    qInfo().noquote() << "Settings at t = "+siStr( m_circTime*1e-12, "s" )+": "+what+" "+from+" → "+to
                         +( nextStart ? " (applies at the next start)" : "" );
}

void Simulator::setreactStep( uint64_t rs )
{
    logRunChange("reactive step", siStr( m_reactStep*1e-12, "s"), siStr( rs*1e-12, "s"), true );
    m_reactStep = rs;
}

void Simulator::setSlopeSteps( int steps )
{
    logRunChange("slope steps", QString::number( m_slopeSteps ), QString::number( steps ), true );
    m_slopeSteps = steps;
}

void Simulator::setSupplyRamp( uint64_t ps )
{
    logRunChange("supply ramp", siStr( m_supplyRamp*1e-12, "s"), siStr( ps*1e-12, "s"), true );
    m_supplyRamp = ps;
}

void Simulator::setMaxNlSteps( uint32_t steps )
{
    logRunChange("max. iterations", QString::number( m_maxNlstp ), QString::number( steps ), false );
    m_maxNlstp = steps;
    m_nlBudget = steps;
}

void Simulator::setNoise( const NoiseSettings& s )
{
    auto text = [this](){ return m_noise.on ? noiseSummary()+( m_noise.off.isEmpty() ? "" : " · off: "+m_noise.off.join(",") )
                                            : QString("off"); };
    QString before = text();
    m_noise = s;
    logRunChange("noise", before, text(), true );
}

bool Simulator::simCore( StateIO& s )
{
    m_coreError.clear();
    if( s.saving() && !stepComplete() ){ m_coreError = "the step was not solved"; return false; }
    s.io( m_circTime );
    s.io( m_nlBudget );
    s.io( m_stepScale );

    if( !s.saving() ){ m_changedNode = nullptr; m_voltChanged = nullptr; m_nonLinear = nullptr; }
    if( !s.mark( 0x45444f4e ) ) return false;                  // NODE
    for( eNode* n : m_eNodeList ) n->simState( s );

    if( !s.mark( 0x4d454c45 ) ) return false;                  // ELEM
    for( int i=0; i<m_elementList.size(); ++i )
    {
        if( !s.mark( (uint32_t)i ) ) return false;             // Each element: a field list that differs is caught here
        m_elementList.at( i )->simState( s );
    }
    if( !s.mark( 0x544e5645 ) ) return false;                  // EVNT
    uint32_t n = 0;
    if( s.saving() )
    {
        for( eElement* e = m_firstEvent; e; e = e->nextEvent ){
            if( e->simIndex < 0 || e->simIndex >= m_elementList.size() || m_elementList.at( e->simIndex ) != e ){
                m_coreError = "an event of "+e->getId()+", an element outside the list";
                return false;
            }
            n++;
        }
        s.io( n );
        for( eElement* e = m_firstEvent; e; e = e->nextEvent ){
            int32_t idx = e->simIndex;
            s.io( idx );
            s.io( e->eventTime );
        }
        return true;
    }
    s.io( n );
    for( eElement* el : m_elementList ){ el->eventTime = 0; el->nextEvent = nullptr; }
    m_firstEvent = nullptr;
    eElement* last = nullptr;
    for( uint32_t k=0; k<n; ++k )
    {
        int32_t idx = -1; uint64_t time = 0;
        s.io( idx );
        s.io( time );
        if( s.overflow() || idx < 0 || idx >= m_elementList.size() ) return false;
        eElement* e = m_elementList.at( idx );
        e->eventTime = time;
        if( last ) last->nextEvent = e; else m_firstEvent = e;
        last = e;
    }
    m_settled = nullptr;
    m_settledGen++;
    m_NLstep = 0;
    m_converged = true;
    m_runEnd = m_circTime;
    m_resumeRun = false;
    return !s.overflow();
}

// A keyframe was loaded (simCore): its nodes are marked changed, so the matrix takes the restored
// admittances and currents. Solving them again at the restored time used to replace the solved
// voltages with a fresh solution, and where non-linear parts settle within their tolerance
// (Scherz 12.57b: diodes and zeners of a ternary AND) on another point than the first pass took:
// the replay then differed. Now the matrix is stamped and solved for its factors, then the
// keyframe's voltages are put back and the callbacks that solve queued are dropped: the step
// stays the solved one it was
void Simulator::syncAfterRestore()
{
    std::vector<double> volts;
    volts.reserve( m_eNodeList.size() );
    for( eNode* n : m_eNodeList ) volts.push_back( n->getVolt() );
    if( m_changedNode ) solveMatrix();
    for( int i=0; i<m_eNodeList.size(); ++i ) m_eNodeList.at( i )->restoreVolt( volts[i] );
    for( eElement* e = m_voltChanged; e; e = e->nextChanged ) e->added = false;
    for( eElement* e = m_nonLinear;   e; e = e->nextChanged ) e->added = false;
    m_voltChanged = nullptr;
    m_nonLinear = nullptr;
    m_changedNode = nullptr;
    m_converged = true;
}

void Simulator::setSpeedLock( bool on )
{
    logRunChange("speed lock", m_speedLock ? "on" : "off", on ? "on" : "off", false );
    m_speedLock = on;     // The factor follows at the next frame
}

void Simulator::setLockMaxStep( uint64_t ps )
{
    logRunChange("max. reactive step", siStr( lockMaxStep()*1e-12, "s"),
                 siStr( ( ps ? ps : 100*m_reactStep )*1e-12, "s"), false );
    m_lockMaxStep = ps;
}

uint64_t Simulator::lockedStep()
{
    uint64_t step = m_reactStep*(uint64_t)m_stepScale;
    uint64_t max = qMax( lockMaxStep(), m_reactStep );
    return ( step > max ) ? max : step;
}

// Once per frame, worker idle. The worker runs one frame's span (m_psPF) per frame and is cut
// at the next frame when it hasn't finished: m_simPsPF / m_psPF is how much of its span the
// solver managed, whatever the GUI does. Below 95 % for 3 frames: double the factor; done with
// the load below 50 % for 10 frames: halve it, never below ×1
void Simulator::speedLockFrame()
{
    bool want = m_speedLock && !m_fastRun && !m_stopAt && !m_debug;   // D15: skips, run --for at ×1
    if( !want )
    {
        if( m_stepScale > 1 ) applyStepScale( 1 );
        m_lockSlow = m_lockIdle = 0;
        m_lockLimit = false;
        return;
    }
    if( m_lockCap != lockMaxStep() ){ m_lockCap = lockMaxStep(); applyStepScale( m_stepScale ); }
    if( !m_lockRan || m_psPF == 0 ) return;     // No run last frame (paused, just started)

    double done = (double)m_simPsPF/(double)m_psPF;
    if( done < 0.95 )
    {
        m_lockIdle = 0;
        if( ++m_lockSlow < 3 ) return;
        m_lockSlow = 0;
        bool atMax = m_reactive.isEmpty() || m_reactStep*(uint64_t)m_stepScale >= qMax( lockMaxStep(), m_reactStep );
        if( atMax )
        {
            if( !m_lockLimit )
                qInfo().noquote() << "Speed lock at t = "+siStr( m_circTime*1e-12, "s" )+": "
                    +( m_reactive.isEmpty() ? QString("can't keep up: events, not steps (no capacitor or inductor to coarsen)")
                                            : "can't keep up at the max. reactive step "+siStr( lockedStep()*1e-12, "s" ) );
            m_lockLimit = true;
            return;
        }
        applyStepScale( m_stepScale*2 );
        return;
    }
    m_lockSlow = 0;
    m_lockLimit = false;
    if( done >= 0.99 && m_simLoad < 50 && m_stepScale > 1 )
    {
        if( ++m_lockIdle < 10 ) return;
        m_lockIdle = 0;
        applyStepScale( m_stepScale/2 );
    }
    else m_lockIdle = 0;
}

void Simulator::applyStepScale( int s )
{
    if( s < 1 ) s = 1;
    int old = m_stepScale;
    m_stepScale = s;
    for( eReactive* r : m_reactive ) r->setStepScale( s );
    if( s == old ) return;

    // Messages: a new largest factor and the return to ×1; every change in the debug log
    QString line = "Speed lock at t = "+siStr( m_circTime*1e-12, "s" )+": step ×"+QString::number( s )
                  +" (reactive step "+siStr( lockedStep()*1e-12, "s" )+")";
    if( s > m_lockMaxScale || ( s == 1 && old > 1 && !m_fastRun && !m_stopAt ) ) qInfo().noquote() << line;
    else qDebug().noquote() << "[lock]" << line;
    if( s > m_lockMaxScale ) m_lockMaxScale = s;
}

void Simulator::setSkipInterval( uint64_t ps )
{
    m_skipInterval = ps;
    if( CircuitWidget::self() ) CircuitWidget::self()->syncSkip( ps );
}

void Simulator::setStepsPerSec( uint64_t sps )
{
    if( sps < 1 ) sps = 1;

    m_stepsPS = sps;           // Steps per second
    setPsPerSec( m_stepsPS*m_stepSize );
}

void Simulator::setPsPerSec( uint64_t psPs )
{
    if( psPs < 1 ) psPs = 1;

    m_psPerSec = psPs;    // picosecond/second
    m_psPF = psPs/m_fps;  // picosecond/frame

    uint64_t fps = m_fps;
    if( m_psPF == 0 ) // We must lower fps to get at least 1 ps per frame
    {
        m_psPF = 1;
        fps = psPs;
    }
    m_timerTick_ms = 1000/fps;  // in ms

    InfoWidget::self()->setTargetSpeed( 100*m_psPerSec/1e12 );
    if( CircuitWidget::self() ) CircuitWidget::self()->syncTimebase( m_psPerSec ); // Toolbar timebase
}

void Simulator::clearEventList()
{
    m_firstEvent = NULL;
}
void Simulator::addEvent( uint64_t time, eElement* el )
{
    if( m_state < SIM_STARTING ) return;

    if( el->eventTime )
    { qDebug() << "Warning: Simulator::addEvent Repeated event"<<el->getId(); return; }

    time += m_circTime;
    eElement* last  = NULL;
    eElement* event = m_firstEvent;

    while( event ){
        if( time <= event->eventTime ) break; // Insert event here
        last  = event;
        event = event->nextEvent;
    }
    el->eventTime = time;

    if( last ) last->nextEvent = el;
    else       m_firstEvent = el; // List was empty or insert First

    el->nextEvent = event;
}

void Simulator::cancelEvents( eElement* el )
{
    if( el->eventTime == 0 ) return;
    eElement* event = m_firstEvent;
    eElement* last  = NULL;
    eElement* next  = NULL;
    el->eventTime = 0;

    while( event ){
        next = event->nextEvent;
        if( el == event )
        {
            if( last ) last->nextEvent = next;
            else       m_firstEvent = next;
            event->nextEvent = NULL;
        }
        else last = event;
        event = next;
}   }

void Simulator::addToEnodeList( eNode* nod )
{ if( !m_eNodeList.contains(nod) ) m_eNodeList.append( nod ); }

void Simulator::addToElementList( eElement* el )
{
    if( m_elementList.contains( el ) ) return;
    m_elementList.append( el );
    if( isRunning() ) SimHistory::self()->end("the circuit changed during the run");   // Keyframes no longer fit
}

void Simulator::remFromElementList( eElement* el )
{
    if( m_elementList.removeOne( el ) && isRunning() ) SimHistory::self()->end("the circuit changed during the run");
}

void Simulator::addToUpdateList( Updatable* el )
{ if( !m_updateList.contains(el) ) m_updateList.append(el); }

void Simulator::remFromUpdateList( Updatable* el )
{ m_updateList.removeOne(el); }

void Simulator::addToSocketList( Socket* el )
{ if( !m_socketList.contains(el) ) m_socketList.append(el); }

void Simulator::remFromSocketList( Socket* el )
{ m_socketList.removeOne(el); }

#include "moc_simulator.cpp"
