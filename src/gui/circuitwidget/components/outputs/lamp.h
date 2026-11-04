/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef LAMP_H
#define LAMP_H

#include "e-resistor.h"
#include "comp2pin.h"

class LibraryItem;

class Lamp : public Comp2Pin, public eResistor
{
    public:
        Lamp( QString type, QString id );
        ~Lamp();

 static Component* construct( QString type, QString id );
 static LibraryItem* libraryItem();

        virtual void initialize() override;
        virtual void stamp() override;
        virtual void updateStep() override;
        virtual void voltChanged() override;
        virtual void runEvent() override;

        double maxCurrent()             { return m_maxCurrent; }
        void  setMaxCurrent( double c ) { m_maxCurrent = c; }

        double resistance() { return m_fixedRes; }
        void setResistance( double r );

        // Filament model (on when the rated power is > 0): tungsten, R ~ T^1.2, heated by V^2/R,
        // cooled by radiation ~ (T^4 - T0^4), with the heat capacity that gives the thermal time
        // constant at the rated point. The hot resistance is Vrated^2 / Prated, the cold one that
        // divided by the hot/cold ratio: a cold lamp draws the inrush current.
        double ratedPower() { return m_ratedPower; }
        void setRatedPower( double p );
        double ratedVolt() { return m_ratedVolt; }
        void setRatedVolt( double v );
        double coldRatio() { return m_coldRatio; }
        void setColdRatio( double r );
        double thermalTau() { return m_thermalTau; }
        void setThermalTau( double t );

        bool   thermal()     { return m_ratedPower > 0; }
        double temperature() { return m_temp; }        // K
        double radiated()    { return m_radiated; }    // W
        double filamentRes() { return 1/m_admit; }     // Ohm, now

        virtual void paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w ) override;

    private:
        virtual void updateVI() override;
        void updateBright();
        double overCurrent();
        void setupThermal();

        uint64_t m_prevStep;
        uint32_t m_intensity;

        double m_maxCurrent;
        double m_avgCurrent;
        double m_totalCurrent;
        double m_lastCurrent;
        double m_lastPeriod;
        double m_brightness;

        double m_fixedRes;
        double m_ratedPower;
        double m_ratedVolt;
        double m_coldRatio;
        double m_thermalTau;

        double m_temp;
        double m_tHot;
        double m_rCold;
        double m_kRad;      // W/K^4
        double m_cTh;       // J/K
        double m_radiated;
        uint64_t m_thStep;  // ps
};

#endif
