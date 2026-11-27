/***************************************************************************
 *   Copyright (C) 2026 by Jeroen de Zwart                                  *
 *                                                                         *
 *   Power Meter: a Meter in the lead of a source that shows the power     *
 *   through it, with the r.m.s. voltage, the r.m.s. current and the power *
 *   factor (docs/POWER_METER.md)                                           *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef POWERMETER_H
#define POWERMETER_H

#include "meter.h"

class LibraryItem;

class PowerMeter : public Meter
{
    public:
        PowerMeter( QString type, QString id );
        ~PowerMeter();

        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( PowerMeter )

 static Component* construct( QString type, QString id );
 static LibraryItem* libraryItem();

        double voltRms();                 // GUI thread: over the block (so far, or the last one)
        double currRms();
        double meanPower();               // The block's mean of v x i
        double apparentPower();           // V r.m.s. x I r.m.s.
        double powerFactor();             // Mean power / apparent power, -1 to 1

        virtual double measured() override;
        virtual void setReading( QString r ) override;
        virtual void initialize() override;
        virtual void stamp() override;
        virtual void stepSettled() override;
        virtual void updateStep() override;
        virtual void paint( QPainter* p, const QStyleOptionGraphicsItem* option, QWidget* widget ) override;

    protected:
        virtual void setflip() override;
        virtual double instant() override;

        void  restartAt( uint64_t t );    // The block starts here (worker idle)
        void  integrateVI( uint64_t t );  // Worker thread (stepSettled): the held V and I up to t
        void  closeBlock( double sumV2, double sumI2, double sumP );
        void  valuesAt( uint64_t t, double& vrms, double& irms, double& pmean );   // No state change: GUI thread

        double   m_heldV = 0, m_heldI = 0;// The red lead's voltage to ground, and the current into it, since the last step
        double   m_sumV2 = 0, m_sumI2 = 0, m_sumP = 0;   // Integrated since the block's start (V^2 ps, A^2 ps, W ps)
        uint64_t m_pmBlockStart = 0;
        uint64_t m_pmLastT = 0;
        double   m_vRms = 0, m_iRms = 0, m_pMean = 0;   // The block that closed last
        bool     m_pmHaveResult = false;

        static QString eng( double v, const QString& unit );   // "641.3 mW"

        static const double pfMinV;         // No power factor below this r.m.s. voltage ...
        static const double pfMinI;         // ... or this r.m.s. current
};

#endif
