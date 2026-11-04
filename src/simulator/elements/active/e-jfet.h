/***************************************************************************
 *   Copyright (C) 2026 by Jeroen de Zwart                                 *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef EJFET_H
#define EJFET_H

#include <QHash>

#include "e-element.h"

// Junction FET, the SPICE level-1 (Shichman-Hodges) model, N or P channel:
//   cut-off   VGS < Vto            ID = 0
//   linear    0 < VDS < VGS-Vto    ID = Beta·VDS·(2(VGS-Vto) - VDS)·(1 + Lambda·VDS)
//   saturated VDS > VGS-Vto        ID = Beta·(VGS-Vto)²·(1 + Lambda·VDS)
// (so IDSS = Beta·Vto² at VGS = 0), drain and source swapped when VDS < 0, plus the gate junction
// to the source and to the drain: IG = Is·(exp(V/(N·Vt)) - 1) each.
// Pins: 0 drain, 1 source, 2 gate.

struct jfetData_t{
    bool   pChannel;
    double beta;    // A/V²
    double vto;     // V, negative: pinch-off voltage (same sign convention for P channel, as in SPICE)
    double lambda;  // 1/V
    double satCur;  // A, gate junction
    double emCoef;
};

class eJfet : public eElement
{
    public:
        eJfet( QString id );
        ~eJfet();

        virtual void initialize() override;
        virtual void stamp() override;
        virtual void voltChanged() override;

        bool pChannel() { return m_pChannel; }
        virtual void setPchannel( bool p ) { m_pChannel = p; m_changed = true; }

        double idss() { return m_beta*m_vto*m_vto; }
        void setIdss( double idss );

        double vto() { return m_vto; }
        void setVto( double vto );

        double lambda() { return m_lambda; }
        void setLambda( double l );

        double satCur() { return m_satCur; }
        void setSatCur( double is );

        double emCoef() { return m_emCoef; }
        void setEmCoef( double n );

        QString model() { return m_model; }
        void setModel( QString model );

 static void getModels();
 static QStringList modelNames();

    protected:
        void channel( double vgs, double vds, double* id, double* gm, double* gds );
        double limitStep( double vnew, double vold );

        void updateVcrit();

        bool   m_pChannel;
        double m_beta;
        double m_vto;
        double m_lambda;
        double m_satCur;
        double m_emCoef;
        double m_vt;     // N·kT/q
        double m_vCrit;

        double m_vgs;    // Last linearization point (N-channel equivalent voltages)
        double m_vgd;
        double m_vds;

        QString m_model;

        eElement m_aux;  // Internal pins, one per matrix term (see stamp())

 static QHash<QString, jfetData_t> m_jfets;
 static QStringList m_names;
};

#endif
