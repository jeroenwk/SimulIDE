/***************************************************************************
 *   Copyright (C) 2026 by the SimulIDE fork contributors                  *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

// VCO and PLL chips (docs/PLL_VCO.md). Both are relaxation oscillators built the way the chips
// are: the timing current really flows through the external R1 (and R2), and the external
// timing capacitor is charged by it between two thresholds, so the parts drawn in a book's
// circuit set the frequency.
//
//   CD4046B:  VCO (pin 11 held at VCO IN, pin 12 at VDD: I = VCOin/R1 + VDD/R2 charges C1
//             between pins 6 and 7, reversed at +-2.5 V: f = I / (10 V * C1)), phase
//             comparator I (XOR), phase comparator II (edge-triggered, three-state) with the
//             phase pulses output, source follower (demodulator out), inhibit, 5.5 V zener.
//   NE566:    pin 6 held at the control voltage, I = (V+ - VC)/R1 charges and discharges C1
//             (pin 7) between 3/8 and 5/8 of the supply: f = 2 (V+ - VC) / (R1 C1 V+);
//             buffered triangle and square outputs.

#ifndef PLLVCO_H
#define PLLVCO_H

#include "component.h"
#include "e-element.h"

class LibraryItem;
class eResistor;
class eDiode;
class ePin;

// An output driven towards a voltage relative to the reference node, through a resistance
// (Norton: G from the pin to the reference plus a current G*V into the pin). Uses two of the
// owner's ePins (pin side, reference side). G tiny: high impedance
class NortonOut
{
    public:
        void init( eElement* owner, QString id, int firstEpin, Pin* pin );
        void setNodes( eNode* ref );          // In the owner's stamp()
        void set( double v, double g );       // Stamps when changed
        double current();                     // Out of the pin into the circuit (A)
        void remove();
        void simState( StateIO& s );

        eResistor* res = nullptr;
        Pin*  pin = nullptr;
        ePin* a = nullptr;
        ePin* b = nullptr;
        double m_v = 0, m_g = 0;
};

class PllVcoBase : public Component, public eElement
{
    public:
        PllVcoBase( QString type, QString id );
        ~PllVcoBase();

        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( PllVcoBase )

        virtual void voltChanged() override;   // Asks for stepSettled()
        virtual void runEvent() override;      // Applies the outputs decided in stepSettled()

        virtual void paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w ) override;

    protected:
        Pin* addPin( int i, QString id, QString label, bool right, int row );
        virtual void applyOutputs()=0;
        void later();                           // runEvent() 1 ps from now
        void watch( std::initializer_list<int> pins );

        bool m_pending;
        QString m_title;
};

class Cd4046 : public PllVcoBase
{
    public:
        Cd4046( QString type, QString id );
        ~Cd4046();

        virtual void simState( StateIO& s ) override;
        SIM_STATE( Cd4046 )

 static Component* construct( QString type, QString id );
 static LibraryItem* libraryItem();

        virtual void initialize() override;
        virtual void stamp() override;
        virtual void stepSettled() override;

        double vcoFreq();                       // From the present timing current (Hz)
        double vcoCurrent() { return m_iVco; }
        bool   pc2Up() { return m_up; }
        bool   pc2Down() { return m_dn; }

        virtual void remove() override;

    private:
        virtual void applyOutputs() override;
        double rOut();                          // Output resistance at this VDD

        enum { P_PP=0, P_PC1, P_CMP, P_VCO, P_INH, P_C1A, P_C1B, P_VSS,
               P_VIN, P_SF, P_R1, P_R2, P_PC2, P_SIG, P_ZEN, P_VDD };
        // ePins: 7 outputs x 2, the capacitor drive current (pin 6, pin 7, VSS), the switches
        // (2 x 2), the zener (2) and the signal input bias (2 x 2)
        enum { E_OUT=0, E_C6=14, E_C7, E_CREF, E_SW6, E_SW6R, E_SW7, E_SW7R, E_ZA, E_ZK,
               E_BH1, E_BH2, E_BL1, E_BL2, E_COUNT };

        NortonOut m_pc1, m_pc2, m_pp, m_vco, m_sf, m_r1, m_r2;
        eResistor* m_sw6;
        eResistor* m_sw7;
        eResistor* m_biasH;
        eResistor* m_biasL;
        eDiode*    m_zener;

        double m_iStamped;   // Capacitor current in the matrix
        int    m_swState;    // Switches in the matrix: 0 none yet, 1 phase A, 2 phase B, 3 off
        bool   m_phaseA;     // VCO: charging pin 6 (pin 7 grounded); VCO out high
        double m_iVco;       // Timing current
        bool   m_sig, m_cmp; // Input logic levels
        bool   m_up, m_dn;   // Phase comparator II flip-flops
        bool   m_inh;
};

class Ne566 : public PllVcoBase
{
    public:
        Ne566( QString type, QString id );
        ~Ne566();

        virtual void simState( StateIO& s ) override;
        SIM_STATE( Ne566 )

 static Component* construct( QString type, QString id );
 static LibraryItem* libraryItem();

        virtual void initialize() override;
        virtual void stamp() override;
        virtual void stepSettled() override;

        double timingCurrent() { return m_i; }
        bool charging() { return m_charging; }

        virtual void remove() override;

    private:
        virtual void applyOutputs() override;

        enum { P_GND=0, P_NC, P_SQ, P_TRI, P_VC, P_R1, P_C1, P_VP };
        // ePins: 3 outputs x 2, the capacitor current (pin 7, ground), control input (2),
        // supply (2)
        enum { E_OUT=0, E_C7=6, E_CREF, E_VC1, E_VC2, E_S1, E_S2, E_COUNT };

        NortonOut m_sq, m_tri, m_r1;
        eResistor* m_vcIn;     // Control terminal input impedance
        eResistor* m_supply;   // Supply current

        bool   m_charging;
        double m_i;
        double m_iStamped;
};

#endif
