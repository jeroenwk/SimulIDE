/***************************************************************************
 *   Copyright (C) 2020 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef BATTERY_H
#define BATTERY_H

#include "comp2pin.h"
#include "e-resistor.h"

class LibraryItem;

class Battery : public Comp2Pin, public eResistor
{
    public:
        Battery( QString type, QString id );
        ~Battery();

        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( Battery )

        // K7: typed values set the Voltage (not shown by default: an empty ShowProp is
        // not saved, so a shown default would show up on every older circuit)
        QString valueProperty() override { return "Voltage"; }

 static Component* construct( QString type, QString id );
 static LibraryItem* libraryItem();

        double volt() { return m_volt; }
        virtual double nominalVolt() override { return m_volt; }
        void setVolt( double volt );

        virtual void setRes( double resist ) override;

        virtual void stamp() override;
        virtual void updateStep() override;
        virtual void runEvent() override;      // Supply ramp steps (Simulator::supplyRamp)

        virtual void paint( QPainter* p, const QStyleOptionGraphicsItem* option, QWidget* widget ) override;

    private:
        double m_volt;

        bool m_connected;
        int  m_rampK;          // Supply ramp step, RAMP_STEPS = at full voltage
        double rampFactor();
        void startRamp();
        void stampVolt();
};

#endif
