/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef FIXEDVOLT_H
#define FIXEDVOLT_H

#include <QToolButton>
#include <QGraphicsProxyWidget>

#include "component.h"
#include "e-element.h"

class IoPin;
class LibraryItem;
class CustomButton;

class FixedVolt : public Component, public eElement
{
    public:
        FixedVolt( QString type, QString id );
        ~FixedVolt();

        // K7: typed values set the Voltage (not shown by default: an empty ShowProp is
        // not saved, so a shown default would show up on every older circuit)
        QString valueProperty() override { return "Voltage"; }

 static Component* construct( QString type, QString id );
 static LibraryItem* libraryItem();

        virtual void stamp() override;
        virtual void updateStep() override;
        virtual void runEvent() override;      // Supply ramp steps (Simulator::supplyRamp)

        bool out();
        virtual void setOut( bool out );

        double volt() { return m_voltage; }
        virtual double nominalVolt() override { return m_voltage; }
        void setVolt( double v );

        virtual void paint( QPainter* p, const QStyleOptionGraphicsItem* option, QWidget* widget ) override;

    public slots:
        virtual void onbuttonclicked();

    protected:
        void updateOutput();

        double m_voltage;
        int  m_rampK;          // Supply ramp step, RAMP_STEPS = at full voltage

        IoPin* m_outpin;

        CustomButton* m_button;
        QGraphicsProxyWidget* m_proxy;
};

#endif
