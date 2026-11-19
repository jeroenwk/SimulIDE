/***************************************************************************
 *   Varistor (metal-oxide, MOV): symmetric clamp, one non-linear element  *
 *   with the datasheet's power law I = 1 mA (|V|/V1mA)^alpha              *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef VARISTOR_H
#define VARISTOR_H

#include "comp2pin.h"
#include "e-resistor.h"

class LibraryItem;

// Through the two datasheet points (V1mA at 1 mA, the clamping voltage at its test current),
// alpha = ln(Iclamp/1 mA) / ln(Vclamp/V1mA): 18.35 for a 07D561K (560 V, 925 V at 10 A). One
// element between the two pins, no inner nodes (the earlier model: two zener junctions in
// anti-series plus a resistor). States IDLE / CLAMPING / OVER (docs/PART_STATES.md)
class Varistor : public Comp2Pin, public eResistor
{
    public:
        Varistor( QString type, QString id );
        ~Varistor();

 static Component* construct( QString type, QString id );
 static LibraryItem* libraryItem();

        virtual void initialize() override;
        virtual void stamp() override;
        virtual void voltChanged() override;
        virtual void stepSettled() override;

        double volt1mA() { return m_volt1mA; }
        void setVolt1mA( double v );

        double clampVolt() { return m_clampVolt; }
        void setClampVolt( double v );

        double clampCurr() { return m_clampCurr; }
        void setClampCurr( double i );

        double maxCurrent() { return m_maxCurr; }      // Peak current rating (8/20 µs), A
        void setMaxCurrent( double i ) { if( i > 0 ) m_maxCurr = i; }

        double alpha() { return m_alpha; }

        double current();               // From lPin to rPin, A, at the present pin voltages

        virtual void paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w ) override;

    private:
        void updateValues();
        double curr( double v );        // I(V), A
        double cond( double v );        // dI/dV, S
        bool   currConverged( double v );

        double m_volt1mA;               // Varistor voltage at 1 mA (the part number: 561 = 560 V)
        double m_clampVolt;             // Maximum clamping voltage ...
        double m_clampCurr;             // ... at this peak current (datasheet)
        double m_maxCurr;               // Peak current rating

        double m_alpha;                 // Power-law exponent
        double m_gLeak;                 // Leakage conductance, S

        double m_volt;                  // Linearization point, V
        bool   m_firstIter;
        int    m_state;                 // 0 IDLE, 1 CLAMPING, 2 OVER; -1 none yet
};

#endif
