/***************************************************************************
 *   Copyright (C) 2026 by Jeroen de Zwart                                 *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef CURRREG_H
#define CURRREG_H

#include "e-currreg.h"
#include "component.h"

class LibraryItem;

// Current Source IC: programmable 3-terminal current source (LM334...), docs/CURRENT_SOURCE_IC.md
class CurrReg : public Component, public eCurrReg
{
    public:
        CurrReg( QString type, QString id );
        ~CurrReg();

 static Component* construct( QString type, QString id );
 static LibraryItem* libraryItem();

        virtual void initialize() override;
        virtual void updateStep() override;
        virtual void voltChanged() override;
        virtual void stepSettled() override;

        QString modelStr() { return m_model; }
        void setModelStr( QString model );

        virtual void paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w ) override;

    private:
        enum region_t { regNone=-1, regOff=0, regOn, regLow, regFault };
        region_t m_region;
        QString  m_fault;
};

#endif
