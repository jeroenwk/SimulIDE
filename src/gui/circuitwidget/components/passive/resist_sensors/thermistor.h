/***************************************************************************
 *   Copyright (C) 2020 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef Thermistor_H
#define Thermistor_H

#include "thermistorbase.h"

class LibraryItem;

class Thermistor : public ThermistorBase
{
    public:
        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( Thermistor )
        Thermistor( QString type, QString id );
        ~Thermistor();

        static Component* construct( QString type, QString id );
        static LibraryItem* libraryItem();

        int bVal() { return m_bVal; }
        void setBval( int bval );

        int r25() { return m_r25; }
        void setR25( int r25 );

        virtual void updateStep() override;

        virtual void paint( QPainter* p, const QStyleOptionGraphicsItem* option, QWidget *widget ) override;

    private:
        int m_bVal;
        int m_r25;
};

#endif
