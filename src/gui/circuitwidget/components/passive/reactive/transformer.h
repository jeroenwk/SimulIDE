/***************************************************************************
 *   Copyright (C) 2022 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef TRANSFORMER_H
#define TRANSFORMER_H

#include "e-element.h"
#include "component.h"
#include "e-coil.h"
#include "chargegauge.h"

class LibraryItem;
class Transformer;

// Current display of one coil (specs/inductor-current.md)
class CoilGauge : public GaugeOwner
{
    public:
        CoilGauge( Transformer* t, eCoil* c, QString n ) { xfmr = t; coil = c; name = n; gauge = nullptr; }

        virtual double  gaugeLevel() override;
        virtual QString gaugeText( int labelMode ) override;

        Transformer* xfmr;
        eCoil*  coil;
        QString name;      // "P01", "S11": as the pin at the coil's end
        ChargeGauge* gauge;
};

class Transformer : public Component, public eElement
{
        struct winding_t
        {
            QString definition;
            QString topology;
            QString prefix;
            QList<Pin*>   pins;
            QList<eCoil*> coils;
            int nCoils;
            int start;
            int angle;
            int x;
        };

    public:
        Transformer( QString type, QString id );
        ~Transformer();

 static Component* construct( QString type, QString id );
 static LibraryItem* libraryItem();

        virtual void stamp() override;
        virtual void runEvent() override;

        double baseInd() { return m_baseInd; }
        void setBaseInd( double i );

        QString primary() { return m_primary.definition; }
        void setPrimary( QString p );

        QString secondary() { return m_secondary.definition; }
        void setSecondary( QString s );

        double coupCoeff() { return m_coupCoeff; }
        void setCoupCoeff( double c );

        // Current display
        double currentScale() { return m_currentScale; } // Own 100 % (A), 0 = the circuit's
        void setCurrentScale( double a );
        bool   ownCurrentRef() { return m_currentScale > 0; }
        double currentRef();
        double maxCoilCurrent();       // Largest |I| of its coils
        bool   currentShown();
        void   chargeDisplayChanged();
        const QList<CoilGauge*>& coilGauges() { return m_coilGauges; }

        virtual void updateStep() override;

        virtual void paint( QPainter* p, const QStyleOptionGraphicsItem* option, QWidget* widget ) override;

    private:
        void setupInducts( winding_t* w );
        void createCoils();
        int  getLeght( winding_t* w );
        void modify( winding_t* w );
        void create( winding_t* w );
        void buildGauges();

        int m_lenght;

        double m_baseInd;
        double m_coupCoeff;

        uint64_t m_reacStep;

        winding_t m_primary;
        winding_t m_secondary;

        QList<eCoil*> m_coils;

        double m_currentScale;
        QList<CoilGauge*> m_coilGauges;

        std::vector<std::vector<double>> m_inva;
};
#endif
