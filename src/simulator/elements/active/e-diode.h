/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef EDIODE_H
#define EDIODE_H

#include <QHash>

#include "e-resistor.h"
#include "e-junctioncap.h"

struct diodeData_t
{
    double satCur;
    double emCoef;
    double brkDow;
    double resist;
};

class eNode;

class eDiode : public eResistor
{
    public:
        eDiode( QString id );
        ~eDiode();

        virtual void stamp() override;
        virtual void voltChanged() override;
        virtual void noiseSources( QList<NoiseSrc>& list ) override;

        double threshold() { return m_vCriti; }
        void   setThreshold( double vCrit );

        double brkDownV(){ return m_bkDown; }
        void   setBrkDownV( double bkDown );

        double satCur() { return m_satCur; }
        void   setSatCur( double satCur );

        double emCoef() { return m_emCoef; }
        void   setEmCoef( double emCoef );

        double maxCurrent() { return m_maxCur; }
        void   setMaxCurrent( double cur ) { m_maxCur = cur; }

        // Junction capacitance C(V) = Cj0/(1-V/Vj)^M (0 = none; typical 4 pF while noise is on)
        double cj0() { return m_cj.cj0(); }
        void setCj0( double v ) { m_cj.setCj0( v ); }
        double vj() { return m_cj.vj(); }
        void setVj( double v ) { m_cj.setVj( v ); }
        double mj() { return m_cj.mj(); }
        void setMj( double v ) { m_cj.setMj( v ); }

        QString model() { return m_model; }
        void setModel( QString model );
        void setModelData( diodeData_t data );

 static void getModels();

    protected:
        double limitStep( double vnew, double vold, double scale, double vc );
        void SetParameters( double sc, double ec, double bv, double sr );
        void updateValues();

        bool m_converged;

        eJunctionCap m_cj;

        double m_vt;
        double m_satCur;
        double m_emCoef;
        double m_vScale;
        double m_vdCoef;
        double m_vzCoef;
        double m_zOfset;
        double m_vCriti;
        double m_vzCrit;

        double m_bkDown;
        double m_maxCur;

        double m_voltPN;
        double m_bAdmit;

        QString m_diodeType;
        QString m_model;

 static QHash<QString, diodeData_t> m_diodes;
 static QHash<QString, diodeData_t> m_zeners;
 static QHash<QString, diodeData_t> m_leds;
};

#endif
