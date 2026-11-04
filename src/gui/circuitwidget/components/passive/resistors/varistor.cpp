/***************************************************************************
 *   Varistor (metal-oxide, MOV): symmetric clamp, modelled as two zener   *
 *   junctions in anti-series plus the slope resistance above the knee     *
 ***( see copyright.txt file at root folder )*******************************/

#include <QPainter>

#include "varistor.h"
#include "itemlibrary.h"
#include "simulator.h"
#include "e-diode.h"
#include "e-node.h"
#include "pin.h"

#include "doubleprop.h"

#define tr(str) simulideTr("Varistor",str)

// A real MOV follows I ~ V^alpha (alpha ~ 20-40): almost nothing below its voltage, then a steep
// rise. Here each direction is a zener knee at the 1 mA voltage (a few nA below it) and above it a
// straight line through the datasheet's maximum clamping voltage at its peak current. That is
// pessimistic between the knee and the peak current (a real MOV clamps lower there) and right at
// the datasheet point. Not modelled: the capacitance (~100 pF), energy rating and ageing.

Component* Varistor::construct( QString type, QString id )
{ return new Varistor( type, id ); }

LibraryItem* Varistor::libraryItem()
{
    return new LibraryItem(
        tr("Varistor"),
        "Resistors",
        "varistor.png",
        "Varistor",
        Varistor::construct );
}

Varistor::Varistor( QString type, QString id )
        : Comp2Pin( type, id )
        , eElement( id )
{
    m_area = QRectF(-11,-4.5, 22, 9 );
    m_pin[0]->setLength( 5 );
    m_pin[1]->setLength( 5 );

    m_volt1mA   = 560;              // 07D561K
    m_clampVolt = 925;
    m_clampCurr = 10;

    m_midA = nullptr;
    m_midB = nullptr;

    // lPin --ePin0-- diode1 --ePin1-- midA --ePin2-- diode2 (reversed) --ePin3-- midB --ePin4-- resistor -- rPin
    setNumEpins( 5 );

    m_diode1 = new eDiode( id+"-dio1" );
    m_diode1->setEpin( 0, m_ePin[0] );          // Anode
    m_diode1->setEpin( 1, m_ePin[1] );          // Cathode

    m_diode2 = new eDiode( id+"-dio2" );
    m_diode2->setEpin( 0, m_ePin[3] );          // Anode, towards rPin
    m_diode2->setEpin( 1, m_ePin[2] );          // Cathode, at midA

    m_resistor = new eResistor( m_elmId+"-res" );
    m_resistor->setEpin( 0, m_ePin[4] );
    m_resistor->setEpin( 1, m_pin[1] );

    updateValues();

    addPropGroup( { tr("Main"), {
        new DoubProp<Varistor>("Volt1mA", tr("Varistor Voltage (1 mA)"), "V"
                              , this, &Varistor::volt1mA, &Varistor::setVolt1mA ),

        new DoubProp<Varistor>("ClampVolt", tr("Max Clamping Voltage"), "V"
                              , this, &Varistor::clampVolt, &Varistor::setClampVolt ),

        new DoubProp<Varistor>("ClampCurr", tr("at Peak Current"), "A"
                              , this, &Varistor::clampCurr, &Varistor::setClampCurr ),
    }, 0 } );

    setShowProp("Volt1mA");
}
Varistor::~Varistor()
{
    delete m_diode1;
    delete m_diode2;
    delete m_resistor;
}

void Varistor::setVolt1mA( double v )   { if( v > 1 ) m_volt1mA = v; updateValues(); }
void Varistor::setClampVolt( double v ) { if( v > 0 ) m_clampVolt = v; updateValues(); }
void Varistor::setClampCurr( double i ) { if( i > 0 ) m_clampCurr = i; updateValues(); }

void Varistor::updateValues()
{
    // Is = 1 nA, n = 1: the forward junction drops ~0.4 V at 1 mA, the reverse one breaks down at
    // the rest of the 1 mA voltage; leakage below the knee stays in the nA (real MOV: < 10 uA)
    for( eDiode* d : { m_diode1, m_diode2 } ){
        d->setSatCur( 1e-9 );
        d->setEmCoef( 1 );
        d->setBrkDownV( m_volt1mA-0.4 );
        d->setMaxCurrent( m_clampCurr );
    }
    double res = ( m_clampVolt-m_volt1mA )/m_clampCurr;
    if( res < 1e-3 ) res = 1e-3;
    m_resistor->setRes( res );
}

void Varistor::initialize()
{
    m_midA = new eNode( m_elmId+"-midA");
    m_midB = new eNode( m_elmId+"-midB");
}

void Varistor::stamp()
{
    eNode* node0 = m_pin[0]->getEnode();

    m_diode1->getEpin(0)->setEnode( node0 );
    m_diode1->getEpin(1)->setEnode( m_midA );
    m_diode2->getEpin(1)->setEnode( m_midA );
    m_diode2->getEpin(0)->setEnode( m_midB );
    m_resistor->getEpin(0)->setEnode( m_midB );
}

double Varistor::current() { return m_resistor->current(); }

void Varistor::paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w )
{
    Component::paint( p, o, w );
    p->drawRect( m_area );
    QPen pen = p->pen();                         // The VDR mark: a diagonal with a flat foot
    pen.setWidthF( 1.2 );
    p->setPen( pen );
    p->drawLine( QPointF(-9, 7 ), QPointF(-5, 7 ) );
    p->drawLine( QPointF(-5, 7 ), QPointF( 7,-7 ) );
    Component::paintSelected( p );
}
