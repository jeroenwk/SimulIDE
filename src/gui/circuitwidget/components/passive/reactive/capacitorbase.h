/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef CAPACITORBASE_H
#define CAPACITORBASE_H

#include <QGraphicsItem>

#include "reactive.h"

class CapacitorBase;

// Charge gauge and live label beside a capacitor (specs/capacitor-charge.md).
// A child item kept upright on screen: its transform undoes the parent's rotation
// and flip, so its coordinates are scene-aligned with the origin at the part centre.
class ChargeGauge : public QGraphicsItem
{
    public:
        ChargeGauge( CapacitorBase* cap );

        QRectF boundingRect() const override { return m_bound; }
        void paint( QPainter* p, const QStyleOptionGraphicsItem*, QWidget* ) override;

        void refresh();     // Layout and repaint (once per frame while shown)

    private:
        CapacitorBase* m_cap;
        QRectF  m_bar;      // Gauge bar (upright coordinates)
        QPointF m_textPos;  // Label baseline start
        QRectF  m_bound;
        QString m_text;
};

class CapacitorBase : public Reactive
{
    friend class ChargeGauge;

    public:
        CapacitorBase( QString type, QString id );
        ~CapacitorBase();

        virtual void setCurrentValue( double c ) override;
        virtual void updateStep() override;

        bool logCharge() { return m_logCharge; }          // Charge events in Messages (EventLog)
        void setLogCharge( bool l ) { m_logCharge = l; }

        double maxVolt() { return m_maxVolt; }
        void setMaxVolt( double v );

        // Charge display
        double capVolt();             // V(pin0) - V(pin1)
        double chargeLevel();         // |V| / vref (may exceed 1)
        QStringList chargeFlags();    // "overvoltage", "reversed", "overflow"
        void chargeDisplayChanged();  // Setting toggled or simulation stopped

    protected:
        virtual double updtRes()  override { return m_tStep/m_capacitance; }
        virtual double updtCurr() override { return m_volt*m_admit; }

        // Pen for the plate on pin side 0 or 1: coloured by node potential while shown
        void setPlatePen( QPainter* p, int side );
        bool chargeShown();
        virtual bool isReversed() { return false; }

        double m_capacitance;
        double m_maxVolt;
        bool   m_logCharge;

        ChargeGauge* m_gauge;
};

#endif
