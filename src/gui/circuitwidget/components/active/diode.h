/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef DIODE_H
#define DIODE_H

#include "e-diode.h"
#include "linkercomponent.h"

class LibraryItem;

class Diode : public LinkerComponent, public eDiode
{
    public:
        Diode( QString type, QString id, bool zener=false );
        ~Diode();

 static Component* construct( QString type, QString id );
 static LibraryItem* libraryItem();

        virtual void voltChanged() override;

        virtual bool setPropStr( QString prop, QString val ) override;

        virtual void initialize() override;
        virtual void stamp() override;
        virtual void updateStep() override;
        virtual void stepSettled() override;

        virtual double res() override{ return m_resistor->res(); }
        double diodeCurrent() { return m_resistor->current(); } // Anode to cathode (CLI read)
        double avgCurrent()  { return m_avgCur; }   // |I| through a 100 ms low-pass (vs Max Current)
        double peakCurrent() { return m_peakCur; }  // Largest |I| of a solved step since the start
        virtual void setResSafe( double resist ) override { m_resistor->setResSafe(resist);}

        virtual void paint( QPainter* p, const QStyleOptionGraphicsItem* option, QWidget* widget ) override;

    private:
        bool m_isZener;
        int  m_region;           // Part state: -1 none, 0 off, 1 forward, 2 breakdown, 3 over current
                                 // (average), 4 surge (peak above Surge Current)
        void average( uint64_t t );
        void setRegion( uint64_t t );   // Part state from the held current and the average

        double   m_signedCur;    // I of the last solved step, anode to cathode

        double   m_absCur;       // |I| of the last solved step, held since m_lastT
        double   m_avgCur;       // Low-passed |I| (time constant AVG_TAU)
        double   m_peakCur;      // Largest |I| since the start
        uint64_t m_lastT;
        uint64_t m_surgeT;       // Time of the last peak above Surge Current
        bool     m_surged;       // A peak above Surge Current in the last SURGE_HOLD

        eNode* m_midEnode;
        eResistor* m_resistor;
};

#endif
