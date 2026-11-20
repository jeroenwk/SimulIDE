/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef LEDBASE_H
#define LEDBASE_H

#include "e-led.h"
#include "component.h"
#include "e-node.h"

class eNode;

class LedBase : public Component, public eLed
{
    public:
        LedBase( QString type, QString id );
        ~LedBase();
        
        enum ledColor_t {
            yellow = 0,
            red,
            green,
            blue,
            orange,
            purple,
            white
        };

        QString colorStr() { return m_ledColorStr; }
        void setColorStr( QString color );

        double peakCurrentRating() { return m_peakLimit; }   // IFP (A), 0 = none
        void setPeakCurrentRating( double c ) { m_peakLimit = c > 0 ? c : 0; }

        double avgCurrent()  { return m_avgCur; }    // I through a 100 ms low-pass (vs Max Current, IF)
        double peakCurrent() { return m_peakCur; }   // Largest I of a solved step since the start

        bool grounded() { return m_grounded; }
        void setGrounded( bool grounded );

        virtual void initialize() override;
        virtual void updateStep() override;
        virtual void voltChanged() override;
        virtual void stepSettled() override;

 static QColor getColor( ledColor_t c, int bright );
 static QString getColorList();

        virtual void paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w ) override;

    protected:
        int m_region;            // Part state: -1 none, 0 off, 1 on, 2 over current (average),
                                 // 3 peak (a step above Peak Current)
        void average( uint64_t t );
        void setRegion( uint64_t t );   // Part state from the held current and the average

        double   m_peakLimit;    // Peak Current rating, IFP (A), 0 = none
        double   m_stepCur;      // I of the last solved step, held since m_lastT
        double   m_avgCur;       // Low-passed I (time constant AVG_TAU)
        double   m_peakCur;      // Largest I since the start
        uint64_t m_lastT;
        uint64_t m_peakT;        // Time of the last step above Peak Current
        bool     m_peaked;       // A step above Peak Current in the last PEAK_HOLD

        virtual void drawBackground( QPainter* p )=0;
        virtual void drawForeground( QPainter* p )=0;
        
        bool   m_grounded;

 static int m_overBright;

        QString m_ledColorStr;
        ledColor_t m_ledColor;

        static eNode m_gndEnode;
};

#endif
