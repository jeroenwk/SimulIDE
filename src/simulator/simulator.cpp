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

    m_errors[0] = "";
    //m_errors[1] = "Could not solve Matrix";
    m_errors[2] = "Add Event: NULL free event";
    m_errors[3] = "LAST_SIM_EVENT reached";

    m_warnings[1] = "NonLinear Not Converging";
    m_warnings[2] = "Probably Circuit Error";  // Warning if matrix diagonal element = 0.
    m_warnings[100] = "AVR crashed !!!";

    resetSim();
    CircuitWidget::self()->setMsg( " "+tr("Stopped")+" ", 1 );

    m_RefTimer.start();
}
Simulator::~Simulator()
{
    m_CircuitFuture.waitForFinished();
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

    if( !m_CircuitFuture.isFinished() ) // Stop remaining parallel thread
    {
        simState_t state = m_state;
        m_state = SIM_WAITING;
        m_CircuitFuture.waitForFinished();
        m_state = state;
    }
    if( m_debug && m_state == SIM_PAUSED ) CircuitWidget::self()->debugPaused();
    if( m_noiseEl ) CircuitWidget::self()->updateNoise();   // Armed / on / ended

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
    runSettled();
    if( m_state < SIM_RUNNING ) return;

    eElement* event = m_firstEvent;
    uint64_t endRun = m_circTime + m_psPF; // Run upto next Timer event
    if( m_stopAt && endRun > m_stopAt ) endRun = (m_stopAt > m_circTime) ? m_stopAt : m_circTime;
    uint64_t nextTime;

    while( event )                              // Simulator event loop
    {
        if( event->eventTime > endRun ) break;  // All events for this Timer Tick are done

        nextTime = m_circTime;
        while( m_circTime == nextTime )         // Run all event with same timeStamp
        {
            m_circTime = event->eventTime;
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
    eElement* el = m_settled;
    m_settled = nullptr;
    m_settledGen++;
    if( m_state < SIM_RUNNING ) return;   // Stopped in the middle of the step: values not final
    while( el ){
        eElement* next = el->nextSettled;
        el->stepSettled();
        el = next;
    }
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
            if( m_state < SIM_RUNNING ){ m_converged = false; break; }    // Loop broken without converging
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
        if( m_state < SIM_RUNNING ) break;    // Loop broken without converging
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
        if( caps ) qInfo().noquote() << "Noise: junction capacitance on"<< caps << "junctions ("
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
    m_changedNode = NULL;
    m_settled = nullptr;
    m_settledGen++;
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
    m_state = SIM_PAUSED;
}

void Simulator::resumeSim()
{
    if( m_state != SIM_PAUSED ) return;
    m_state = m_oldState;
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
{ if( !m_elementList.contains(el) ) m_elementList.append(el); }

void Simulator::remFromElementList( eElement* el )
{ m_elementList.removeOne(el); }

void Simulator::addToUpdateList( Updatable* el )
{ if( !m_updateList.contains(el) ) m_updateList.append(el); }

void Simulator::remFromUpdateList( Updatable* el )
{ m_updateList.removeOne(el); }

void Simulator::addToSocketList( Socket* el )
{ if( !m_socketList.contains(el) ) m_socketList.append(el); }

void Simulator::remFromSocketList( Socket* el )
{ m_socketList.removeOne(el); }

#include "moc_simulator.cpp"
