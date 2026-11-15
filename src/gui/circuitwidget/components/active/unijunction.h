/***************************************************************************
 *   Copyright (C) 2026 by the SimulIDE fork contributors                  *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

// Programmable unijunction transistor (PUT) and unijunction transistor (UJT),
// both as the two-transistor latch of their datasheets (docs/UNIJUNCTION.md):
//
//   anode ──PNP emitter      PNP base ── gate ── NPN collector
//   PNP collector ── mid ── NPN base,  mid ──Rsh── cathode,  NPN emitter ── cathode
//
// The PUT's gate is a pin, set by an outside divider; the UJT has its divider inside
// (R_BB from base 2 to base 1, tapped at eta), its emitter is the PUT's anode and its
// base 1 the cathode. The shunt Rsh keeps the NPN off below the peak current; the NPN
// gain sets the valley (holding) current.

#ifndef UNIJUNCTION_H
#define UNIJUNCTION_H

#include <QHash>

#include "component.h"
#include "e-element.h"

class LibraryItem;
class eBJT;
class eResistor;
class eNode;

struct unijData_t {
    double gainP;   // PNP current gain
    double gainN;   // NPN current gain: sets the valley current
    double shunt;   // NPN base-emitter shunt (Ω): sets the peak current
    double rbb;     // UJT interbase resistance (Ω)
    double eta;     // UJT intrinsic standoff ratio
};

class Unijunction : public Component, public eElement
{
    public:
        Unijunction( QString type, QString id, bool ujt );
        ~Unijunction();

        virtual void initialize() override;
        virtual void stamp() override;

        QString model() { return m_model; }
        void setModel( QString m );

        double gainP() { return m_data.gainP; }
        void setGainP( double g );
        double gainN() { return m_data.gainN; }
        void setGainN( double g );
        double shunt() { return m_data.shunt; }
        void setShunt( double r );

        // Junction capacitances at 0 V (pF in the GUI; 0 = none, typical values while noise is on)
        double cj1();
        void setCj1( double c );
        double cj2();
        void setCj2( double c );
        double cj3();
        void setCj3( double c );

        virtual void remove() override;

    protected:
        void custom();           // A parameter set by hand: the model becomes Custom
        void applyData();
        QString modelList();

        bool m_ujt;
        bool m_setting;          // Applying a model: don't switch to Custom

        QString m_model;
        unijData_t m_data;

        eBJT* m_pnp;
        eBJT* m_npn;
        eResistor* m_rsh;
        eResistor* m_rb1;        // UJT only
        eResistor* m_rb2;        // UJT only

        eNode* m_midNode;
        eNode* m_gateNode;       // UJT only (the PUT's gate is a pin)

 static QHash<QString, unijData_t> m_puts;
 static QHash<QString, unijData_t> m_ujts;
 static void getModels();
};

class PUT : public Unijunction
{
    public:
        PUT( QString type, QString id );
        ~PUT();

 static Component* construct( QString type, QString id );
 static LibraryItem* libraryItem();

        virtual void paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w ) override;
};

class UJT : public Unijunction
{
    public:
        UJT( QString type, QString id );
        ~UJT();

 static Component* construct( QString type, QString id );
 static LibraryItem* libraryItem();

        double rbb() { return m_data.rbb; }
        void setRbb( double r );
        double eta() { return m_data.eta; }
        void setEta( double e );

        virtual void paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w ) override;
};

#endif
