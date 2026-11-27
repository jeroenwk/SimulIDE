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
        double iMax() { return m_iMax; }                  // A, output current limit (0 = none)
        void setIMax( double i ) { m_iMax = ( i > 0 ) ? i : 0; m_changed = true; }
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

        enum { LIN=0, SAT_P, SAT_N, SLEW, ILIM };   // SLEW: output a source at m_slewV, moved by events; ILIM: a current source at +-I_Max
        enum { aOP=0, aON, aPO, aNO, aR0, aR1, aNum };  // Internal pins, see stamp(); aR0/aR1: output row, rail column

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
        eElement m_sup;              // Supply currents (#506): ePins 0-2 on V+, 3 on the output; 4-7 the same for V-;
                                     // 8+2k+r: supply k's row, rail r's column (a rail on a node, below)
        double m_iMax;               // Output current limit, A (0 = none): ILIM above it (#431)
        int    m_ilimDir;            // ILIM: +1 sourcing, -1 sinking
        double m_impNow;             // The output's impedance as stamped (Out_Imped, or 1 GOhm in ILIM)
        int    m_supDir;             // Supply pin carrying the output current: 0 none, 1 V+ (sourcing), 2 V- (sinking)
        int    m_supFlips;           // Direction changes in this time step
        bool   m_supDiag[2][3];      // That column is the supply node itself: on the diagonal
        // On a rail whose supply pin is on a matrix node, the output is tied to that node in the
        // matrix (1/Out_Imped between them) instead of a source at its voltage of the last solve:
        // a floating supply (the chip's ground on a non-isolated buck) moved with the output and
        // the next solve followed it, a cycle that never settled (create-circuit #426 follow-up)
        bool   m_railOn[2];          // Rail r (0 V+, 1 V-) is on a node other than ground and the output's
        bool   m_railDiag[2][2];     // Supply k's row, rail r's column: the same node (diagonal)
        int    m_railNow;            // Rail tied to in the matrix as stamped: 0, 1, or -1 none
        void   stampSupply( double G, double gG );
        double loadCurrent( double ideal );   // A, delivered by the output to the rest of its node

        int m_region;                // Part state: -1 none, 0 linear, 1 SAT+, 2 SAT- (stepSettled)

        IoPin* m_inputP;
        IoPin* m_inputN;
        IoPin* m_output;
};

#endif
