/***************************************************************************
 *   Copyright (C) 2026 by Jeroen de Zwart                                 *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef JFET_H
#define JFET_H

#include "e-jfet.h"
#include "component.h"

class LibraryItem;

class Jfet : public Component, public eJfet
{
    public:
        Jfet( QString type, QString id );
        ~Jfet();

 static Component* construct( QString type, QString id );
 static LibraryItem* libraryItem();

        virtual void initialize() override;
        virtual void updateStep() override;
        virtual void voltChanged() override;
        virtual void stepSettled() override;

        virtual void setPchannel( bool p ) override;

        QString modelStr() { return m_model; }
        void setModelStr( QString model );

        double vp() { return m_pChannel ? -m_vto : m_vto; }   // Pinch-off VGS with its real sign
        void setVp( double v ) { setVto( m_pChannel ? -v : v ); }

        virtual void paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w ) override;

    private:
        enum region_t { regNone=-1, regOff=0, regActive, regOhmic };
        region_t m_region;
};

#endif
