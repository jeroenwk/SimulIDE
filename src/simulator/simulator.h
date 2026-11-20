/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef SIMULATOR_H
#define SIMULATOR_H

#include "e-node.h"
#include "e-element.h"
#include "noise.h"

enum simState_t{
    SIM_STOPPED=0,
    SIM_ERROR,
    SIM_STARTING,
    SIM_PAUSED,
    SIM_WAITING,
    SIM_RUNNING,
    SIM_DEBUG,
};

#include <QElapsedTimer>
#include <QMutex>
#include <QStringList>
#include <QFuture>

class BaseProcessor;
class Updatable;
class eElement;
class Socket;
class eNode;
class CircMatrix;

class Simulator : public QObject
{
        friend class eNode;

    Q_OBJECT
    public:
        Simulator( QObject* parent=0 );
        ~Simulator();

 static Simulator* self() { return m_pSelf; }

         void addEvent( uint64_t time, eElement* el );
         void cancelEvents( eElement* el );

        void startSim( bool paused=false );
        void pauseSim();

        // GUI thread: let the worker finish the time step it is solving and stop there, so what
        // the GUI and the CLI read next is a converged step, not a Newton iteration in progress
        void holdWorker();
        void resumeWorker();
        void resumeSim();
        void stopSim();

        void setWarning( int warning ) { m_warning = warning; }
        
        uint64_t fps() { return m_fps; }
        void setFps( uint64_t fps );
        uint64_t psPerFrame() { return m_psPF; }
        uint64_t simPsPF() { return m_simPsPF; }

        uint64_t psPerSec() { return m_psPerSec; } // Speed picosecond/second
        void setPsPerSec( uint64_t psPs );

        uint64_t stepSize() { return m_stepSize; }
        void setStepSize( uint64_t stepSize ) { m_stepSize = stepSize; }

        uint64_t stepsPerSec() { return m_stepsPS; }
        void setStepsPerSec( uint64_t sps );

        uint64_t reactStep() { return m_reactStep; }
        void setreactStep( uint64_t rs ) { m_reactStep = rs; }

        void  setSlopeSteps( int steps ) { m_slopeSteps = steps; }

        // Supply ramp (create-circuit sim-coldstart): DC sources (Battery, Rail, Fixed Voltage)
        // rise from 0 to their value over this time at the start of a run, as a bench supply
        // does. 0 = a step (the default). Instant steps can start a loop in a state a real
        // supply never visits (an op amp railed, a cascode dead)
        uint64_t supplyRamp() { return m_supplyRamp; }
        void setSupplyRamp( uint64_t ps ) { m_supplyRamp = ps; }
        static const int RAMP_STEPS = 50;
        int slopeSteps( ) { return m_slopeSteps; }

        void  setMaxNlSteps( uint32_t steps ) { m_maxNlstp = steps; m_nlBudget = steps; }

        struct NlReport { uint64_t time; uint32_t iterations; QStringList ids; };  // Step left unconverged
        QList<NlReport> takeNlReports();            // GUI thread: reports since the last call
        uint32_t maxNlSteps( ) { return m_maxNlstp; }
        
        bool isRunning() { return (m_state >= SIM_STARTING); }
        bool isPaused()  { return (m_state == SIM_PAUSED); }
        bool isPauseDebug() { return (m_state == SIM_PAUSED && m_debug == true); }

        uint64_t circTime() { return m_circTime; }

        // Hold simulation time at this point (0 = no limit). Used by CLI "sim run --for"
        void setStopAt( uint64_t ps ) { m_stopAt = ps; }
        uint64_t stopAt() { return m_stopAt; }

        void timerEvent( QTimerEvent* e );

        double realSpeed() { return m_realSpeed; } // 0 to 10000 => 0 to 100%

        simState_t simState() { return m_state; }

        inline void notCorverged() { m_converged = false; }

        // Call el->stepSettled() once this time step has converged (once per step, however often called)
        inline void addToSettled( eElement* el )
        {
            if( el->settledGen == m_settledGen ) return;
            el->settledGen  = m_settledGen;
            el->nextSettled = m_settled;
            m_settled = el;
        }

        // el->stepSettled() after every solved step, changed nodes or not (one watcher: the
        // CLI averager reading a pin with no node, e.g. an unconnected gate output)
        void setStepWatch( eElement* el ) { m_stepWatch = el; }
        eElement* stepWatch() { return m_stepWatch; }

        void addToStartSettle( eElement* el ) { m_startSettle.append( el ); }
        // With noise on: a seeded random start state (0/1) for a logic element, -1 when noise is
        // off or its part is in noiseOff (the element keeps its own start state)
        int randomStart( eElement* el );
        // With noise on: the seed of an element's own random sequence, 0 when noise is off
        // or its part is in noiseOff
        uint64_t noiseSeed( eElement* el, const QString& tag );

        void addToEnodeList( eNode* nod );

        void addToElementList( eElement* el );
        void remFromElementList( eElement* el );
        
        double eNodeVolt( int n ) { return ( n > 0 && n <= m_eNodeList.size() ) ? m_eNodeList.at( n-1 )->getVolt() : 0; } // Node 0 = ground

        void addToUpdateList( Updatable* el );
        void remFromUpdateList( Updatable* el );

        const QList<eElement*>& elementList() { return m_elementList; }

        // Transient noise (docs/NOISE_SPEC.md): settings saved with the circuit, used at the next start
        NoiseSettings& noise() { return m_noise; }
        void setNoise( const NoiseSettings& s ) { m_noise = s; }
        eNoise* noiseEngine() { return m_noiseEl; }   // Null when noise is off or stopped
        uint64_t noiseSeedUsed() { return m_noiseSeed; } // 0 before the first start with noise
        // Was noise active at some time in [t0, t1] (ps) of this run (or of the last one, after a stop)?
        bool noiseActiveDuring( uint64_t t0, uint64_t t1 );
        bool noiseWasActive() { return noiseActiveDuring( 0, UINT64_MAX ); }
        QString noiseBadge( bool brief=false );        // Toolbar text, empty when noise is off
        QString noiseSummary();                        // "×1 · 10 MHz · seed 7 · 0–1 ms"
 static QString siStr( double v, QString unit );       // 1e7,"Hz" -> "10 MHz"
 static double  parseSi( QString v, QString unit, bool* ok ); // "10MHz","Hz" -> 1e7; "1ms","s" -> 1e-3

        void addToSocketList( Socket* el );
        void remFromSocketList( Socket* el );

    private:
 static Simulator* m_pSelf;

        // Accelerate calls from eNode:
        inline void addToChangedNodes( eNode* nod ) { nod->nextCH = m_changedNode; m_changedNode = nod; }
        inline void addToChangedList( eElement* el ) { el->nextChanged = m_voltChanged; m_voltChanged = el; }
        inline void addToNoLinList( eElement* el ) { el->nextChanged = m_nonLinear;  m_nonLinear = el; }


        void createNodes();
        void resetSim();
        void runCircuit();
        inline void solveCircuit();
        inline void solveMatrix();

        inline void clearEventList();

        //inline void stopTimer();
        //inline void initTimer();

        eElement* m_firstEvent;

        QFuture<void> m_CircuitFuture;

        CircMatrix* m_matrix;

        QHash<int, QString> m_errors;
        QHash<int, QString> m_warnings;

        QList<eNode*> m_eNodeList;

        eNode*    m_changedNode;
        eElement* m_voltChanged;
        eElement* m_nonLinear;
        eElement* m_settled = nullptr;
        eElement* m_stepWatch = nullptr;
        uint64_t  m_settledGen = 1;
        inline void runSettled();

        // Worker: a stop was asked (frame, pause: m_state < SIM_RUNNING). Leave the step unsolved
        // only when stopping for good, or when finishing it has taken over 50 ms of wall time
        inline bool breakStep() {
            return m_state < SIM_PAUSED || m_RefTimer.nsecsElapsed()-m_holdSince > 50000000; }
        inline bool stepComplete() {
            return m_converged && !m_changedNode && !m_nonLinear && !m_voltChanged; }
        qint64 m_holdSince = 0;   // Wall time (m_RefTimer) the stop was asked
        uint64_t m_runEnd = 0;    // Simulated time the current frame's run goes up to
        bool m_resumeRun = false; // The next run goes on to m_runEnd (resumeWorker)
        QList<eElement*> m_startSettle;
        void startSettle();

        QList<eElement*> m_elementList;
        QList<Updatable*> m_updateList;
        QList<Socket*> m_socketList;

        simState_t m_state;
        simState_t m_oldState;

        bool m_debug;
        bool m_converged;
        bool m_pauseCirc;

        int m_error;
        int m_warning;
        int m_timerId;
        int m_timerTick_ms;
        int m_slopeSteps;
        uint64_t m_supplyRamp;

        double m_realFPS;
        uint64_t m_fps;
        uint32_t m_NLstep;
        uint32_t m_maxNlstp;
        uint32_t m_nlBudget;       // Iterations allowed now: less after a step that did not converge
        QStringList m_nlIds;       // Elements still iterating in the last allowed pass
        QList<NlReport> m_nlReports;
        QMutex m_nlLock;

        uint64_t m_reactStep;
        uint64_t m_psPerSec;
        uint64_t m_stepSize;  ///
        uint64_t m_stepsPS;   ///
        uint64_t m_psPF;
        uint64_t m_simPsPF;
        double   m_realSpeed;

        uint64_t m_timerTime;
        uint64_t m_circTime;
        uint64_t m_stopAt;
        uint64_t m_tStep;
        uint64_t m_lastStep;
        uint64_t m_refTime;
        uint64_t m_lastRefT;
        uint64_t m_loopTime;
        uint64_t m_guiTime;
        uint64_t m_updtTime;
        double   m_simLoad;

        QElapsedTimer m_RefTimer;

        NoiseSettings m_noise;
        eNoise*  m_noiseEl = nullptr;
        uint64_t m_noiseFrom = 0;   // Active interval of the last run with noise (ps), kept after a stop
        uint64_t m_noiseTo   = 0;
        uint64_t m_noiseSeed = 0;
        void startNoise();
        void stopNoise();
};
 #endif


