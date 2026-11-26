/***************************************************************************
 *   Copyright (C) 2026 by the SimulIDE fork contributors                  *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

// Light sensors with a light level set on the part, as on the LDR (docs/PHOTO.md):
//
//   Photodiode:       a diode (eDiode) with the photocurrent I = S*lux from cathode to
//                     anode across it, and a shunt for the dark current.
//   Phototransistor:  an NPN (eBJT) with the photocurrent I = S*lux from collector to
//                     base, so IC = gain*I. The base pin is optional (most have none).
//
// S (Sensitivity, A/lx) is the photocurrent per lux of illuminant A (the datasheets'
// "EA = 1 klx" figures). The optocoupler (later) uses the same photocurrent source,
// driven by its LED current instead of the dial.

#ifndef PHOTOSENSOR_H
#define PHOTOSENSOR_H

#include "dialed.h"
#include "e-element.h"

class LibraryItem;
class eDiode;
class eBJT;
class eResistor;
class eNode;
class Pin;

class PhotoSensor : public Dialed, public eElement
{
    public:
        PhotoSensor( QString type, QString id );
        ~PhotoSensor();

        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( PhotoSensor )

        virtual void initialize() override;
        virtual void stamp() override;
        virtual void updateStep() override;
        virtual void voltChanged() override;   // Light pin: lux from its voltage

        double getVal() { return m_value; }
        void setVal( double v );
        double minVal() { return m_minVal; }
        void setMinVal( double v );
        double maxVal() { return m_maxVal; }
        void setMaxVal( double v );
        double getStep() { return m_step; }
        void setStep( double s ) { m_step = s < 0 ? 0 : s; }

        QString model() { return m_model; }
        virtual void setModel( QString m )=0;

        double sens() { return m_sens; }
        void setSens( double s );

        double lux() { return m_lightPin ? m_pinLux : m_value; }   // lx in use: the pin's or the dial's
        double photoCurrent() { return m_sens*lux(); }   // A

        bool lightPin() { return m_lightPin; }
        void setLightPin( bool b );
        double luxPerV() { return m_luxPerV; }
        void setLuxPerV( double l ) { m_luxPerV = l < 0 ? 0 : l; }

        virtual void setLinkedValue( double v, int i=0 ) override;
        virtual void stateRestored() override;

    public slots:
        virtual void dialChanged( int val ) override;

    protected:
        void updtValue();
        void custom();                    // A model value set by hand: the model becomes Custom
        virtual void updateProxy() override;
        virtual void setNodes()=0;        // Photocurrent ePins (P_PH_IN, P_PH_OUT) onto the part's nodes
        virtual void paintArrows( QPainter* p, double x, double y );
        Pin* createLightPin( QPoint pos, int index, QString id );   // Off until Light_Pin is set

        Pin*   m_lightP;     // Light input pin (Light_Pin): lux = max(0, V) * Lux_per_V
        bool   m_lightPin;
        double m_luxPerV;
        double m_pinLux;

        enum { P_PH_IN=0, P_PH_OUT, P_BASE_COUNT };   // Photocurrent: into P_PH_IN, out of P_PH_OUT

        double m_value;      // lx
        double m_minVal;
        double m_maxVal;
        double m_step;
        bool   m_setFromCode;
        double m_sens;       // A/lx
        double m_stamped;    // Photocurrent in the matrix

        bool    m_setting;   // Applying a model: keep its name
        QString m_model;
};

struct photodiodeData_t {
    double sens;    // A/lx
    double satCur;  // A
    double emCoef;
    double brkDown; // V
    double rShunt;  // Ω (dark current)
    double cj0;     // F
    double vj;      // V
    double mj;
};

class Photodiode : public PhotoSensor
{
    public:
        Photodiode( QString type, QString id );
        ~Photodiode();

        virtual void simState( StateIO& s ) override;
        SIM_STATE( Photodiode )

 static Component* construct( QString type, QString id );
 static LibraryItem* libraryItem();

        virtual void initialize() override;
        virtual void stamp() override;

        virtual void setModel( QString m ) override;

        double satCur();
        void setSatCur( double v );
        double emCoef();
        void setEmCoef( double v );
        double brkDown();
        void setBrkDown( double v );
        double rShunt() { return m_data.rShunt; }
        void setRShunt( double v );
        double cj0();
        void setCj0( double v );
        double vj();
        void setVj( double v );
        double mj();
        void setMj( double v );

        double diodeCurrent();   // Anode to cathode, at the pins (A)

        virtual void remove() override;
        virtual void paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w ) override;

    protected:
        virtual void setNodes() override;
        void applyData();

        enum { P_SH_A=P_BASE_COUNT, P_SH_K, P_COUNT };

        photodiodeData_t m_data;
        eDiode*    m_diode;
        eResistor* m_shunt;

 static QHash<QString, photodiodeData_t> m_models;
};

struct phototranData_t {
    double sens;    // A/lx, base photocurrent
    double gain;
    double satCur;  // A
    double bvceo;   // V
    double cje;     // F
    double cjc;     // F
};

class Phototransistor : public PhotoSensor
{
    public:
        Phototransistor( QString type, QString id );
        ~Phototransistor();

        virtual void simState( StateIO& s ) override;
        SIM_STATE( Phototransistor )

 static Component* construct( QString type, QString id );
 static LibraryItem* libraryItem();

        virtual void initialize() override;
        virtual void stamp() override;

        virtual void setModel( QString m ) override;

        double gain();
        void setGain( double g );
        double satCur();
        void setSatCur( double v );
        double bvceo();
        void setBvceo( double v );
        double cje();
        void setCje( double v );
        double cjc();
        void setCjc( double v );

        bool basePin() { return m_basePin; }
        void setBasePin( bool b );

        double collectorCurrent();   // Into the collector (A)

        virtual void remove() override;
        virtual void paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w ) override;

    protected:
        virtual void setNodes() override;
        virtual void updateProxy() override;   // Dial at the lower left, clear of the emitter
        void applyData();

        enum { P_B=P_BASE_COUNT, P_COUNT };

        bool m_basePin;
        phototranData_t m_data;
        eBJT*  m_bjt;
        eNode* m_baseNode;   // Internal base, when the base pin is off or open

 static QHash<QString, phototranData_t> m_models;
};

#endif
