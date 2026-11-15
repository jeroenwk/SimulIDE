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
        Meter( QString type, QString id );
        ~Meter();

        virtual bool setPropStr( QString prop, QString val ) override;

        bool swithchPins() { return m_switchPins; }

        double value();                          // Measured value as displayed (CLI)
        QString unit() { return m_unit; }
        void setSwitchPins( bool s );

        QString reading() { return m_readingStr; }  // Instant, Mean or RMS (create-circuit #109)
        void setReading( QString r );
        double avgTime() { return m_avgTime; }      // Seconds of simulated time per Mean/RMS block
        void setAvgTime( double t );

        virtual void initialize() override;
        virtual void stamp() override;
        virtual void voltChanged() override;
        virtual void stepSettled() override;
        virtual void updateStep() override;
        virtual void updateLabelColors() override;   // Display colours follow the theme

        virtual void paint( QPainter* p, const QStyleOptionGraphicsItem* option, QWidget* widget ) override;

    protected:
        virtual void setflip() override;

        virtual double instant() = 0;    // The value now (V across, A through)
        double measured();               // What the display shows: instant(), or the last block's mean / RMS

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
        QGraphicsSimpleTextItem m_display;
};

#endif
