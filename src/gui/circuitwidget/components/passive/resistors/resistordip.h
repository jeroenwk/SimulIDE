/***************************************************************************
 *   Copyright (C) 2016 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef RESISTORDIP_H
#define RESISTORDIP_H

#include "component.h"
#include "e-element.h"

#include <QDebug>

class LibraryItem;
class eResistor;
class Pin;

class ResistorDip : public Component, public eElement
{
    public:
        ResistorDip( QString type, QString id );
        ~ResistorDip();

 static Component* construct( QString type, QString id );
 static LibraryItem *libraryItem();

        void setValPos( QPointF ) override {;}

        virtual void stamp() override;
        virtual void updateStep() override;

        int size() { return m_size; }
        void setSize( int size );

        double getRes() { return m_resist; }
        void setRes( double resist );

        bool pullUp() { return m_pullUp; }
        void setPullUp( bool p );

        double puVolt() { return m_puVolt; }
        void setPuVolt( double pv ) { qDebug() << "[ResistorDip] setPuVolt" << pv; m_puVolt = pv; m_changed = true; update(); }

        void createResistors( int c );
        void deleteResistors( int d );

        virtual void remove() override;
        
        virtual void paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w ) override;

    protected slots:
        virtual void slotProperties() override;

    private:
        void updtProperties();

        double m_resist;
        int m_size;

        bool m_pullUp;
        double m_puVolt;

        // pins live in Component::m_pin (BH-005: a second m_pin here hid them from getPins())
        std::vector<eResistor*> m_resistor;

        static eNode m_puEnode;
};

#endif
