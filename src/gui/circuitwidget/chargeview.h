/***************************************************************************
 *   Capacitor charge display: gauge, plate colours, live label           *
 *   See specs/capacitor-charge.md                                         *
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
        enum refSource_t { refNone=0, refOverride, refSources, refPeak };

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

        // Over-voltage warnings blink at 2 Hz
        bool blinkOn() { return m_blinkOn; }

        // Plate colour for a node voltage on the vref scale
        QColor plateColor( double volt );

        void attach();      // Register with the current Simulator (on every simulation start)
        void clear();       // Simulation stopped: repaint capacitors without the display

        virtual void updateStep() override;

        static QString voltStr( double v );   // "820 mV", "19.6 V"

    private:
        ChargeView();

        double sourcesMax();
        void   collectSources( QList<Component*>* list, double& max, int depth );
        void   repaintCaps();

        bool   m_enabled;
        int    m_labelMode;
        bool   m_blinkOn;
        double m_vref;
        int    m_refSource;
        double m_peak;          // Fallback: highest capacitor node voltage since start
};

#endif
