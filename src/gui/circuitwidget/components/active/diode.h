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
        virtual void setResSafe( double resist ) override { m_resistor->setResSafe(resist);}

        virtual void paint( QPainter* p, const QStyleOptionGraphicsItem* option, QWidget* widget ) override;

    private:
        bool m_isZener;
        int  m_region;           // Part state: -1 none, 0 off, 1 forward, 2 breakdown, 3 over current

        eNode* m_midEnode;
        eResistor* m_resistor;
};

#endif
