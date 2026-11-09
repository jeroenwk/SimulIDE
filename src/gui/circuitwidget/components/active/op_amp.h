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

        enum { LIN=0, SAT_P, SAT_N };
        enum { aOP=0, aON, aPO, aNO, aNum };  // Internal pins, see stamp()

        eElement m_aux;
        int    m_mode;               // LIN: out = gain*(V+ - V-) stamped in the matrix; SAT_P/N: out = rail
        int    m_flips;              // Unstable LIN -> rail flips in this time step
        uint64_t m_flipTime;
        bool   m_checkStable;        // First LIN solve after a rail: is the loop stable?
        double m_satVd, m_satOut;    // vd and output at the rail before it
        double m_stampI, m_stampVpos, m_stampVneg;
        double m_constIn;            // Input voltages not on a matrix node (ground or open), as gain*(V+ - V-)
        bool   m_dirty;
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

        int m_region;                // Part state: -1 none, 0 linear, 1 SAT+, 2 SAT- (stepSettled)

        IoPin* m_inputP;
        IoPin* m_inputN;
        IoPin* m_output;
};

#endif
