/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef METER_H
#define METER_H

#include "e-resistor.h"
#include "component.h"

class IoPin;

class Meter : public Component, public eResistor
{
    public:
        Meter( QString type, QString id, bool rms = true );   // rms: RMS is one of the readings
        ~Meter();

        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( Meter )

        virtual bool setPropStr( QString prop, QString val ) override;

        bool swithchPins() { return m_switchPins; }

        double value();                          // Measured value as displayed (CLI)
        QString unit() { return m_unit; }
        void setSwitchPins( bool s );

        QString reading() { return m_readingStr; }  // Instant, Mean or RMS (create-circuit #109)
        virtual void setReading( QString r );   // Virtual: a subclass restarts its own block here
        double avgTime() { return m_avgTime; }      // Seconds of simulated time per Mean/RMS block
        void setAvgTime( double t );

        virtual void initialize() override;
        virtual void stamp() override;
        virtual void voltChanged() override;
        virtual void stepSettled() override;
        virtual void runEvent() override;        // Sets the output pin (wired output only)
        virtual void updateStep() override;
        virtual void updateLabelColors() override;   // Display colours follow the theme

        virtual void paint( QPainter* p, const QStyleOptionGraphicsItem* option, QWidget* widget ) override;

    protected:
        virtual void setflip() override;

        virtual double instant() = 0;    // The value now (V across, A through)
        double currentThrough();        // Into the meter at lPin, now: as exactly as the nodes allow
        virtual double measured();       // What the display shows: instant(), or the last block's mean / RMS
        // A Reading or an Avg_Time changed during a run: the block starts at t (worker idle).
        // Virtual: a subclass restarts its own block too
        virtual void restartBlock( uint64_t t );

        enum { rInstant = 0, rMean, rRms } m_reading;
        QString  m_readingStr;
        double   m_avgTime;
        // Mean / RMS: the value held since m_lastT, integrated per block of m_block ps
        void     integrateTo( uint64_t t );                // Worker thread (stepSettled)
        double   blockValue( double sum, double sum2, double span );
        double   evalAt( uint64_t t );                 // No state change: safe from the GUI thread
        uint64_t m_block;
        uint64_t m_blockStart;
        uint64_t m_lastT;
        double   m_held;
        double   m_sum, m_sum2;
        double   m_result;
        bool     m_haveResult;

        QString m_unit;
        double m_dispValue;
        bool m_switchPins;

        IoPin* m_outPin;
        // Output pin wired (set at stamp): its voltage follows the reading in simulation time,
        // set by an event at the step where the reading changed, not once per GUI frame
        bool     m_outLive;
        uint64_t m_outT;                          // Time the output was last set
        int      m_outN;                          // Times it was set at m_outT
        void     outputNow();                     // Schedule the output update at this time
        void     setOutput( double v );
        QGraphicsSimpleTextItem m_display;
};

#endif
