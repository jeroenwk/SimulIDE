/***************************************************************************
 *   Copyright (C) 2026 by the SimulIDE fork contributors                  *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

// LM2575 step-down switching regulator (docs/LM2575.md): a switch from VIN to OUTPUT, turned on
// at the start of each 52 kHz cycle and off after the duty cycle set by the error amplifier,
// or as soon as its current passes the limit (cycle by cycle). The external diode, inductor
// and capacitor make the buck converter. FEEDBACK senses the output: 1.23 V on the ADJ model
// (a divider), the output itself on the fixed models (3.3, 5.0, 12, 15 V).

#ifndef LM2575_H
#define LM2575_H

#include "pllvco.h"

class Lm2575 : public PllVcoBase
{
    public:
        Lm2575( QString type, QString id );
        ~Lm2575();

        virtual void simState( StateIO& s ) override;
        SIM_STATE( Lm2575 )

 static Component* construct( QString type, QString id );
 static LibraryItem* libraryItem();

        virtual void initialize() override;
        virtual void stamp() override;
        virtual void stepSettled() override;
        virtual void runEvent() override;

        QString model() { return m_model; }
        void setModel( QString m );

        double duty() { return m_duty; }
        bool switchOn() { return m_on; }
        double switchCurrent();
        int limited() { return m_limited; }   // Cycles cut by the current limit

        virtual void remove() override;

    private:
        virtual void applyOutputs() override {}
        void startCycle();
        void setSwitch( bool on );

        enum { P_VIN=0, P_OUT, P_GND, P_FB, P_ONOFF };
        enum { E_SUP1=0, E_SUP2, E_COUNT };

        QString m_model;
        double m_vnom;        // Output of a fixed model, 0 = ADJ
        eResistor* m_sw;      // VIN -> OUTPUT
        eResistor* m_sup;     // Quiescent current

        bool   m_on;
        bool   m_enabled;
        int    m_next;        // Next event: 0 cycle start, 1 switch off, 2 off by the limit
        uint64_t m_cycleT;    // Start of this cycle (ps)
        double m_duty;
        double m_ui;          // Integral term
        double m_lastFb;
        bool   m_lastValid;
        int    m_limited;
};

#endif
