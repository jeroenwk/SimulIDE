/***************************************************************************
 *   Copyright (C) 2018 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef BJT_H
#define BJT_H

#include "e-bjt.h"
#include "component.h"

class LibraryItem;

class BJT : public Component, public eBJT
{
    public:
        BJT( QString type, QString id );
        ~BJT();
        
 static Component* construct( QString type, QString id );
 static LibraryItem* libraryItem();

        virtual void initialize() override;
        virtual void updateStep() override;
        virtual void voltChanged() override;
        virtual void stepSettled() override;

        virtual void setPnp( bool pnp ) override;

        virtual void paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w ) override;

    private:
        enum region_t { regNone=-1, regOff=0, regActive, regSat };
        region_t m_region;
};

#endif
