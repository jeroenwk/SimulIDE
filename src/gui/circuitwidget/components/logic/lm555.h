/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef LM555_H
#define LM555_H

#include "e-element.h"
#include "component.h"
#include "e-resistor.h"
#include "e-pin.h"

class LibraryItem;
class IoPin;
class eNode;

class Lm555 : public Component, public eElement
{
    public:

        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( Lm555 )
        Lm555( QString type, QString id );
        ~Lm555();
        
        static Component* construct( QString type, QString id );
        static LibraryItem *libraryItem();

        bool cmos() { return m_cmos; }
        void setCmos( bool c ) { m_cmos = c; m_voltPos = -1e12; m_changed = true; }

        virtual void initialize() override;
        virtual void stamp() override;
        virtual void voltChanged() override;
        virtual void runEvent() override;
        
        virtual void paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w ) override;

    protected:
        IoPin* m_output;
        Pin* m_discharge;

        Pin* m_Gnd;
        Pin* m_Vcc;
        Pin* m_cv;
        Pin* m_trigger;
        Pin* m_threshold;
        Pin* m_Reset;

        ePin m_ePinA;
        ePin m_ePinB;
        ePin m_ePinC;
        ePin m_ePinD;

        eResistor m_resA;
        eResistor m_resB;
        eResistor m_resD;

        eNode* m_thrEnode;

        double m_voltPos;
        double m_voltNeg;
        bool   m_cmos;               // CMOS output stage (7555, TLC555): high at Vcc, not Vcc - 1.3 V

        uint64_t m_propDelay;

        bool m_outState;
        bool m_reset;            // Reset pin below 0.7 V (part state)
};

#endif
