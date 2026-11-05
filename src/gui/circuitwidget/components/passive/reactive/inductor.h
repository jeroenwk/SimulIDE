/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef INDUCTOR_H
#define INDUCTOR_H

#include "reactive.h"
#include "chargegauge.h"

class LibraryItem;

class Inductor : public Reactive, public GaugeOwner
{
    public:
        Inductor( QString type, QString id );
        ~Inductor();

 static Component* construct( QString type, QString id );
 static LibraryItem* libraryItem();

        double indCurrent() { return m_curSource; }   // Norton source of the companion model (relay)

        virtual void setCurrentValue( double c ) override;
        virtual void updateStep() override;

        Pin* getPin( int n ) { return m_pin[n]; }

        // Current display (specs/inductor-current.md)
        double indCurr() { return seriesCurrent(); }   // From pin 0 to pin 1 (A)

        double currentScale() { return m_currentScale; } // Own 100 % (A), 0 = the circuit's
        void setCurrentScale( double a );

        double maxCurrent() { return m_maxCurrent; }     // Rated current (A), 0 = not set
        void setMaxCurrent( double a );

        bool   ownCurrentRef() { return m_currentScale > 0; }
        double currentRef();          // Own CurrentScale if set, else ChargeView::iref()
        double currentLevel();        // |I| / currentRef() (may exceed 1)
        QStringList currentFlags();   // "overcurrent", "overflow"
        bool   currentShown( bool active=true ); // Display on (if active) and both pins connected
        void   chargeDisplayChanged();

        // GaugeOwner
        virtual double  gaugeLevel() override { return currentLevel(); }
        virtual bool    gaugeWarn() override;
        virtual double  gaugeTick() override;
        virtual QString gaugeText( int labelMode ) override;

        virtual void paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w ) override;

    protected:
        virtual double updtRes()  override { return m_inductance/m_tStep; }
        virtual double updtCurr() override { return m_curSource - m_volt*m_admit; }

        double m_inductance;
        double m_currentScale;
        double m_maxCurrent;

        ChargeGauge* m_gauge;
};

#endif
