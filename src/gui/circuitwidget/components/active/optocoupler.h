/***************************************************************************
 *   Copyright (C) 2026 by the SimulIDE fork contributors                  *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

// Optocouplers (docs/OPTOCOUPLER.md): an infrared LED (eDiode) whose forward current IF
// drives the output side, which has no electrical connection to it:
//
//   Optocoupler (4N35):   phototransistor, base photocurrent k*IF, so IC = CTR*IF
//                         (CTR = gain*k); optional base pin.
//   Opto-Triac (MOC3021, MOC3041): triac switch between MT2 and MT1, fired while IF is
//                         above IFT, held above IH; the zero-cross types fire only while
//                         |V(MT2-MT1)| is below the inhibit voltage.
//   Opto Logic (6N137):   open-collector output pulled low while IF is above ITH and the
//                         enable is high, after the propagation delay; supply current.

#ifndef OPTOCOUPLER_H
#define OPTOCOUPLER_H

#include <QHash>

#include "component.h"
#include "e-element.h"

class LibraryItem;
class eDiode;
class eBJT;
class eResistor;
class eNode;

struct optoLed_t {
    double satCur;   // A
    double emCoef;
};

class OptoBase : public Component, public eElement
{
    public:
        OptoBase( QString type, QString id );
        ~OptoBase();

        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( OptoBase )

        virtual void stamp() override;

        QString model() { return m_model; }
        virtual void setModel( QString m )=0;

        double ledIs() { return m_led_d.satCur; }
        void setLedIs( double v );
        double ledN() { return m_led_d.emCoef; }
        void setLedN( double v );

        double ledCurrent();   // IF, anode to cathode (A)

        virtual void remove() override;
        virtual void paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w ) override;

    protected:
        void custom();
        void applyLed();
        void paintLed( QPainter* p );   // Box, LED on the left, light arrows

        bool    m_setting;
        QString m_model;
        optoLed_t m_led_d;
        eDiode* m_led;
};

// ---------------------------------------------------------------- transistor output

struct optoTran_t {
    optoLed_t led;
    double ctr;      // IC / IF in the active region
    double gain;     // Output transistor gain
    double bvceo;    // V
    double cje, cjc; // F
};

class OptoTransistor : public OptoBase
{
    public:
        OptoTransistor( QString type, QString id );
        ~OptoTransistor();

        virtual void simState( StateIO& s ) override;
        SIM_STATE( OptoTransistor )

 static Component* construct( QString type, QString id );
 static LibraryItem* libraryItem();

        virtual void initialize() override;
        virtual void stamp() override;
        virtual void voltChanged() override;

        virtual void setModel( QString m ) override;

        double ctr() { return m_data.ctr; }
        void setCtr( double v );
        double gain() { return m_data.gain; }
        void setGain( double v );
        double bvceo() { return m_data.bvceo; }
        void setBvceo( double v );
        double cje() { return m_data.cje; }
        void setCje( double v );
        double cjc() { return m_data.cjc; }
        void setCjc( double v );

        bool basePin() { return m_basePin; }
        void setBasePin( bool b );

        double photoCurrent() { return m_stamped; }

        virtual void remove() override;
        virtual void paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w ) override;

    private:
        void applyData();
        enum { P_PH_IN=0, P_PH_OUT, P_B, P_COUNT };

        bool m_basePin;
        double m_stamped;
        optoTran_t m_data;
        eBJT* m_bjt;
        eNode* m_baseNode;

 static QHash<QString, optoTran_t> m_models;
};

// ---------------------------------------------------------------- triac output

struct optoTriac_t {
    optoLed_t led;
    double ift;      // A, LED trigger current
    double ih;       // A, holding current
    double ron;      // Ω, on-state slope (VTM)
    double roff;     // Ω, off-state (IDRM)
    double vinh;     // V, zero-cross inhibit voltage (0 = random phase)
};

class OptoTriac : public OptoBase
{
    public:
        OptoTriac( QString type, QString id );
        ~OptoTriac();

        virtual void simState( StateIO& s ) override;
        SIM_STATE( OptoTriac )

 static Component* construct( QString type, QString id );
 static LibraryItem* libraryItem();

        virtual void initialize() override;
        virtual void stamp() override;
        virtual void voltChanged() override;
        virtual void stepSettled() override;
        virtual void runEvent() override;

        virtual void setModel( QString m ) override;

        double ift() { return m_data.ift; }
        void setIft( double v );
        double ih() { return m_data.ih; }
        void setIh( double v );
        double ron() { return m_data.ron; }
        void setRon( double v );
        double vinh() { return m_data.vinh; }
        void setVinh( double v );

        bool isOn() { return m_state; }
        double mainCurrent();

        virtual void remove() override;
        virtual void paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w ) override;

    private:
        enum { P_D1A=0, P_D1K, P_D2A, P_D2K, P_RM, P_COUNT };

        bool m_state;
        optoTriac_t m_data;
        eNode* m_midNode;
        eDiode* m_d1;
        eDiode* m_d2;
        eResistor* m_sw;

 static QHash<QString, optoTriac_t> m_models;
};

// ---------------------------------------------------------------- logic output

struct optoLogic_t {
    optoLed_t led;
    double ith;      // A, input threshold current
    double ron;      // Ω, output low (VOL)
    double tphl;     // s
    double tplh;     // s
    double icc;      // A, supply current at 5 V
    double renPu;    // Ω, enable pull-up
};

class OptoLogic : public OptoBase
{
    public:
        OptoLogic( QString type, QString id );
        ~OptoLogic();

        virtual void simState( StateIO& s ) override;
        SIM_STATE( OptoLogic )

 static Component* construct( QString type, QString id );
 static LibraryItem* libraryItem();

        virtual void initialize() override;
        virtual void stamp() override;
        virtual void voltChanged() override;
        virtual void stepSettled() override;
        virtual void runEvent() override;

        virtual void setModel( QString m ) override;

        double ith() { return m_data.ith; }
        void setIth( double v );
        double tphl() { return m_data.tphl; }
        void setTphl( double v );
        double tplh() { return m_data.tplh; }
        void setTplh( double v );

        bool outLow() { return m_low; }

        virtual void remove() override;
        virtual void paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w ) override;

    private:
        bool wanted();
        enum { P_PU=0, P_CCV, P_CCG, P_COUNT };   // Internal ends: rout at GND, rcc

        bool m_low;       // Output transistor on
        bool m_target;    // State the pending event goes to
        optoLogic_t m_data;
        eResistor* m_rcc;
        eResistor* m_rout;
        eResistor* m_ren;

 static QHash<QString, optoLogic_t> m_models;
};

#endif
