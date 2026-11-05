/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef CAPACITORBASE_H
#define CAPACITORBASE_H

#include <QGraphicsItem>
#include <QMutex>
#include <QVector>

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

        double chargeScale() { return m_chargeScale; }  // Own 100 % (V), 0 = the circuit's
        void setChargeScale( double v );

        // Charge display
        double capVolt();             // V(pin0) - V(pin1)
        double chargeRef();           // 100 %: own ChargeScale if set, else ChargeView::vref()
        bool   ownChargeRef() { return m_chargeScale > 0; }
        double chargeLevel();         // |V| / chargeRef() (may exceed 1)
        QStringList chargeFlags();    // "overvoltage", "reversed", "overflow"
        void chargeDisplayChanged();  // Setting toggled or simulation stopped

        // Charge log (Log charge on): the voltage every sampleStep of simulated time, held
        // between steps, recorded in the simulation thread. takeSamples() hands the EventLog
        // the points up to `now` (GUI thread) and the time of the first one
        static constexpr uint64_t sampleStep = 100*1000*1000;   // 0.1 ms in ps
        QVector<float> takeSamples( uint64_t now, uint64_t* firstTime );

        virtual void stamp() override;

    protected:
        virtual double updtRes()  override { return m_tStep/m_capacitance; }
        virtual double updtCurr() override { return m_volt*m_admit; }

        // Pen for the plate on pin side 0 or 1: coloured by node potential while shown
        void setPlatePen( QPainter* p, int side );
        bool chargeShown();
        virtual bool isReversed() { return false; }

        virtual void voltSample() override;
        void fillTo( uint64_t time );          // Grid points before `time` at the held voltage

        double m_capacitance;
        double m_maxVolt;
        double m_chargeScale;
        bool   m_logCharge;

        ChargeGauge* m_gauge;

        QMutex         m_sampleLock;
        QVector<float> m_samples;              // Not yet taken
        uint64_t       m_nextSample = 0;       // Time of the next grid point
        double         m_heldVolt = 0;
};

#endif
