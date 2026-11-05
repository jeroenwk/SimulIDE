/***************************************************************************
 *   Copyright (C) 2026 by Jeroen de Zwart                                 *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef LEDSTRIP_H
#define LEDSTRIP_H

#include "ledbase.h"

class LibraryItem;

// A string of identical LEDs, N in series and M such strings in parallel, as one element.
// SimulIDE's LED (eLed) is piecewise linear: off below its forward voltage, then a series
// resistance. Identical LEDs add up exactly in that model, so the whole strip is one eLed with
// forward voltage N x Vf, resistance N x R / M and max current M x Imax. Properties are per LED.
class LedStrip : public LedBase
{
    public:
        LedStrip( QString type, QString id );
        ~LedStrip();

 static Component* construct( QString type, QString id );
 static LibraryItem* libraryItem();

        virtual void initialize() override;

        QString color() { return colorStr(); }
        void setColor( QString c );

        int  count() { return m_count; }
        void setCount( int n );

        int  strings() { return m_strings; }
        void setStrings( int m );

        double ledVf() { return m_ledVf; }
        void setLedVf( double v );

        double ledRes() { return m_ledRes; }
        void setLedRes( double r );

        double ledImax() { return m_ledImax; }
        void setLedImax( double i );

        QString totals();
        void setTotals( QString ) {;}    // Read only: shown in the value label

    protected:
        void drawBackground( QPainter* p ) override;
        void drawForeground( QPainter* p ) override;

    private:
        void updateModel();

        int    m_count;     // LEDs in series
        int    m_strings;   // strings in parallel
        double m_ledVf;     // forward voltage of one LED (V)
        double m_ledRes;    // series resistance of one LED (Ohm)
        double m_ledImax;   // max current of one LED (A)
};

#endif
