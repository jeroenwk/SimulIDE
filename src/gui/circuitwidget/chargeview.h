/***************************************************************************
 *   Capacitor charge and inductor current display: gauges, colours, label *
 *   See specs/capacitor-charge.md and specs/inductor-current.md           *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef CHARGEVIEW_H
#define CHARGEVIEW_H

#include <QString>
#include <QStringList>
#include <QColor>

#include "updatable.h"

class Component;

class ChargeView : public Updatable
{
    public:
        static ChargeView* self();

        enum labelMode_t { labelOff=0, labelVolt, labelPercent, labelBoth };
        enum refSource_t { refNone=0, refOverride, refSources, refPeak, refCapacitor, refPart };

        bool enabled() { return m_enabled; }
        void setEnabled( bool en );

        int  labelMode() { return m_labelMode; }
        void setLabelMode( int mode );
        static QStringList labelModes() { return {"off","volt","percent","both"}; }

        // Shown on screen: enabled and the simulation is running or paused
        bool active();

        // Reference voltage (100 %) and where it comes from. Valid while running,
        // and computed on demand otherwise (CLI read).
        double vref();
        int    refSource() { vref(); return m_refSource; }
        static QString refSourceStr( int s );

        // Reference current (100 %) of inductors and transformer coils: the circuit's
        // currentScale, else the peak |I| of all of them since the start (parts with
        // their own CurrentScale left out). Updated once per frame, and on demand.
        double iref();
        int    irefSource() { iref(); return m_irefSource; }

                // Over-voltage warnings blink at 2 Hz
        bool blinkOn() { return m_blinkOn; }

        // Plate colour for a node voltage on the vref scale, or on `ref` if > 0
        // (a capacitor's own ChargeScale)
        QColor plateColor( double volt, double ref=0 );

        // Coil colour for a current on the scale `ref` (> 0): grey at 0, the Animate
        // high colour at +ref, low colour at -ref
        static QColor scaleColor( double value, double ref );

        void attach();      // Register with the current Simulator (on every simulation start)
        void clear();       // Simulation stopped: repaint capacitors without the display

        virtual void updateStep() override;

        static QString voltStr( double v );   // "820 mV", "19.6 V"
        static QString ampStr( double a );    // "63 mA", "-1.2 A", "0 A" below 1 nA
        // Currents below 1 nA are numerical noise: 0 for levels, colours and the peak
        static double  cleanAmps( double a ) { return ( a < 1e-9 && a > -1e-9 ) ? 0 : a; }
        static QString pctStr( double level, bool zero );   // "63 %", "<1 %"

    private:
        ChargeView();

        double sourcesMax();
        void   collectSources( QList<Component*>* list, double& max, int depth );
        void   repaintCaps();     // Capacitors, inductors and transformers

        bool   m_enabled;
        int    m_labelMode;
        bool   m_blinkOn;
        double m_vref;
        int    m_refSource;
        double m_peak;          // Fallback: highest capacitor node voltage since start
        double m_iref;
        int    m_irefSource;
        double m_ipeak;         // Highest inductor / coil current since start
};

#endif
