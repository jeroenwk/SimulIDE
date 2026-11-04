/***************************************************************************
 *   Varistor (metal-oxide, MOV): symmetric clamp, modelled as two zener   *
 *   junctions in anti-series plus the slope resistance above the knee     *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef VARISTOR_H
#define VARISTOR_H

#include "comp2pin.h"
#include "e-element.h"

class LibraryItem;
class eDiode;
class eResistor;
class eNode;

class Varistor : public Comp2Pin, public eElement
{
    public:
        Varistor( QString type, QString id );
        ~Varistor();

 static Component* construct( QString type, QString id );
 static LibraryItem* libraryItem();

        virtual void initialize() override;
        virtual void stamp() override;

        double volt1mA() { return m_volt1mA; }
        void setVolt1mA( double v );

        double clampVolt() { return m_clampVolt; }
        void setClampVolt( double v );

        double clampCurr() { return m_clampCurr; }
        void setClampCurr( double i );

        double current();               // From lPin to rPin, A

        virtual void paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w ) override;

    private:
        void updateValues();

        double m_volt1mA;               // Varistor voltage at 1 mA (the part number: 561 = 560 V)
        double m_clampVolt;             // Maximum clamping voltage ...
        double m_clampCurr;             // ... at this peak current (datasheet)

        eNode* m_midA;
        eNode* m_midB;

        eDiode*    m_diode1;
        eDiode*    m_diode2;
        eResistor* m_resistor;
};

#endif
