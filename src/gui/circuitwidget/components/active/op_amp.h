/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef OPAMP_H
#define OPAMP_H

#include "component.h"
#include "e-element.h"

class LibraryItem;
class IoPin;

class OpAmp : public Component, public eElement
{
    public:
        OpAmp( QString type, QString id );
        ~OpAmp();

        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( OpAmp )
        
        static Component* construct( QString type, QString id );
        static LibraryItem* libraryItem();

        virtual void initialize() override;
        virtual void stamp() override;
        virtual void updateStep() override;
        virtual void voltChanged() override;
        virtual void stepSettled() override;

        double gain() { return m_gain; }
        void setGain( double g ) { m_gain = g; m_changed = true; }

        double outImp() { return m_outImp; }
        void setOutImp( double imp );

        double voltPos() { return m_voltPosDef; }
        void setVoltPos( double v ) { m_voltPosDef = v; m_changed = true; }

        double voltNeg() { return m_voltNegDef; }
        void setVoltNeg( double v ) { m_voltNegDef = v; m_changed = true; }

        // Errors and speed, all 0 = off (the ideal model, bit for bit as before):
        double slewRate() { return m_slewRate*1e-6; }     // V/µs (the unit's multiplier is its first letter)
        void setSlewRate( double sr );
        double biasCurr() { return m_ib; }                // A, into each input
        void setBiasCurr( double ib );
        double offsetCurr() { return m_ios; }             // A, Ib+ - Ib-
        void setOffsetCurr( double ios );
        double offsetVolt() { return m_vos; }             // V, added to V+ - V-
        void setOffsetVolt( double vos );
        double gbw() { return m_gbw; }                    // Hz, gain-bandwidth product
        void setGbw( double gbw );

        virtual void runEvent() override;                 // Slew steps

        bool powerPins() {return m_powerPins; }
        void setPowerPins( bool set );

        bool switchPins() { return m_switchPins; }
        void setSwitchPins( bool s );

        virtual QPainterPath shape() const;
        virtual void paint( QPainter* p, const QStyleOptionGraphicsItem* option, QWidget* widget );

    protected slots:
        virtual void slotProperties() override;

    protected:
        void updtProperties();
        void stampMode();            // Matrix terms of m_mode (BH-032)

        void stampBias();
        void startSlew( double vs );
        uint64_t slewStep();         // ps between slew steps
        double gbwAlpha();           // GBW pole: share of the last output kept (0 without GBW)

        enum { LIN=0, SAT_P, SAT_N, SLEW };   // SLEW: output a source at m_slewV, moved by events
        enum { aOP=0, aON, aPO, aNO, aNum };  // Internal pins, see stamp()

        eElement m_aux;
        int    m_mode;               // LIN: out = gain*(V+ - V-) stamped in the matrix; SAT_P/N: out = rail
        int    m_flips;              // Unstable LIN -> rail flips in this time step
        int    m_changes;            // Region changes in this time step
        bool   m_held;               // Too many changes: on a rail until the next time step
        uint64_t m_flipTime;
        bool   m_checkStable;        // First LIN solve after a rail: is the loop stable?
        double m_satVd, m_satOut;    // vd and output at the rail before it
        double m_stampI, m_stampVpos, m_stampVneg;
        double m_constIn;            // Input voltages not on a matrix node (ground or open), as gain*(V+ - V-)
        bool   m_dirty;
        int    m_linIter;            // Iterations since the last change to LIN
        double m_lastVd;
        double m_nudge;              // Tiny output offset while waiting: makes the solver call back
        bool   m_diag[2];            // aOP/aON on the diagonal (output wired to that input)

        bool m_powerPins;
        bool m_switchPins;

        double m_accuracy;
        double m_gain;
        double m_voltPos;
        double m_voltNeg;
        double m_voltPosDef;
        double m_voltNegDef;
        double m_outImp;

        double m_slewRate;           // V/s, 0 = none
        double m_ib, m_ios, m_vos;
        double m_gbw;                // Hz, 0 = flat gain
        double m_gbwPrev;            // Output at the last settled step (the pole's state)
        uint64_t m_gbwT;             // Time of m_gbwPrev
        uint64_t m_stampT;           // Time of the last stampMode()
        double m_slewV;              // SLEW: output source voltage
        double m_lastOut;            // Output at the last solved step (slew check)
        uint64_t m_lastT;
        bool   m_lastValid;
        int    m_slewDir;            // Direction of the last slew step (0 = none yet)
        double m_grace;              // Jump allowed once after leaving SLEW (V)
        eElement m_bias;             // Bias currents: ePin 0 on V+, 1 on V-

        int m_region;                // Part state: -1 none, 0 linear, 1 SAT+, 2 SAT- (stepSettled)

        IoPin* m_inputP;
        IoPin* m_inputN;
        IoPin* m_output;
};

#endif
