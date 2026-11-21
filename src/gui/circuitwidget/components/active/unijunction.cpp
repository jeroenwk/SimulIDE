/***************************************************************************
 *   Copyright (C) 2026 by the SimulIDE fork contributors                  *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QPainter>
#include <QDomDocument>

#include "unijunction.h"
#include "theme.h"
#include "itemlibrary.h"
#include "simulator.h"
#include "circuit.h"
#include "circuitwidget.h"
#include "mainwindow.h"
#include "utils.h"
#include "e-bjt.h"
#include "e-resistor.h"
#include "e-node.h"
#include "pin.h"

#include "doubleprop.h"
#include "stringprop.h"

#define tr(str) simulideTr("Unijunction",str)

QHash<QString, unijData_t> Unijunction::m_puts;
QHash<QString, unijData_t> Unijunction::m_ujts;

// Internal ePins (both parts use the same numbers; the PUT leaves 5..8 unused)
enum { P_PNP_C=0, P_NPN_C, P_NPN_B, P_SH_A, P_SH_K, P_PNP_B, P_RB2_G, P_RB1_G, P_RB1_K, P_COUNT };

// Built-in models (docs/UNIJUNCTION.md has where each number comes from); more can be
// added in data/unijunctions.model, like the diode models.
void Unijunction::getModels() // Static
{
    if( !m_puts.isEmpty() ) return;
    m_puts.insert("Custom", {0, 0, 0, 0, 0} );
    m_ujts.insert("Custom", {0, 0, 0, 0, 0} );

    QString modelsFile = MainWindow::self()->getDataFilePath("unijunctions.model");
    QDomDocument domDoc = fileToDomDoc( modelsFile, "Unijunction::getModels");
    QDomNode node = domDoc.documentElement().firstChild();
    while( !node.isNull() )
    {
        QDomElement itemset = node.toElement();
        const QString type = itemset.attribute("type").toLower();
        if( itemset.tagName() == "itemset" )
        {
            QDomNode nodei = itemset.firstChild();
            while( !nodei.isNull() )
            {
                QDomElement item = nodei.toElement();
                if( item.tagName() == "item" )
                {
                    unijData_t d;
                    d.gainP = item.attribute("gainP").toDouble();
                    d.gainN = item.attribute("gainN").toDouble();
                    d.shunt = item.attribute("shunt").toDouble();
                    d.rbb   = item.attribute("rbb").toDouble();
                    d.eta   = item.attribute("eta").toDouble();
                    if     ( type == "put") m_puts.insert( item.attribute("name"), d );
                    else if( type == "ujt") m_ujts.insert( item.attribute("name"), d );
                }
                nodei = nodei.nextSibling();
        }   }
        node = node.nextSibling();
    }
}

Unijunction::Unijunction( QString type, QString id, bool ujt )
           : Component( type, id )
           , eElement( id )
{
    getModels();
    m_ujt = ujt;
    m_setting = false;
    m_midNode  = nullptr;
    m_gateNode = nullptr;

    // Defaults if no model file is found: the first datasheet part of each kind
    m_data = ujt ? unijData_t{ 100, 4.2, 1e6, 7000, 0.65 } : unijData_t{ 100, 10, 150e3, 0, 0 };

    m_pin.resize( 3 );
    setNumEpins( P_COUNT );

    m_pnp = new eBJT( id+"-pnp" );
    m_pnp->setPnp( true );
    m_npn = new eBJT( id+"-npn" );
    m_rsh = new eResistor( id+"-shunt" );
    // Three junctions: J1 anode-gate = the PNP's B-E, J2 the centre = the PNP's B-C (the
    // NPN's B-C is the same junction: none there), J3 = the NPN's B-E. Noise defaults:
    // the BJT's typical values [INFERRED: the datasheets give none]
    m_pnp->setNoiseCaps( 4.5e-12, 3.6e-12 );
    m_npn->setNoiseCaps( 4.5e-12, 0 );
    m_rb1 = nullptr;
    m_rb2 = nullptr;
    if( ujt ){
        m_rb1 = new eResistor( id+"-rb1" );
        m_rb2 = new eResistor( id+"-rb2" );
    }
}
Unijunction::~Unijunction(){}

void Unijunction::remove()
{
    delete m_pnp;
    delete m_npn;
    delete m_rsh;
    if( m_rb1 ) delete m_rb1;
    if( m_rb2 ) delete m_rb2;
    Component::remove();
}

QString Unijunction::modelList()
{
    QStringList l = ( m_ujt ? m_ujts : m_puts ).keys();
    l.sort();
    l.removeAll("Custom");
    l.append("Custom");
    return l.join(",");
}

void Unijunction::initialize()
{
    m_midNode = new eNode( m_elmId+"-mid");
    if( m_ujt ) m_gateNode = new eNode( m_elmId+"-gate");
}

void Unijunction::stamp()   // Before the inner elements' own stamps (they come later in the list)
{
    eNode* nodeA = m_pin[0]->getEnode();   // PUT anode, UJT emitter
    eNode* nodeK = m_pin[1]->getEnode();   // PUT cathode, UJT base 1
    eNode* nodeG = m_ujt ? m_gateNode : m_pin[2]->getEnode();
    Q_UNUSED( nodeA );

    m_ePin[P_PNP_C]->setEnode( m_midNode );
    m_ePin[P_NPN_C]->setEnode( nodeG );
    m_ePin[P_NPN_B]->setEnode( m_midNode );
    m_ePin[P_SH_A ]->setEnode( m_midNode );
    m_ePin[P_SH_K ]->setEnode( nodeK );
    if( m_ujt ){
        m_ePin[P_PNP_B]->setEnode( nodeG );
        m_ePin[P_RB2_G]->setEnode( nodeG );
        m_ePin[P_RB1_G]->setEnode( nodeG );
        m_ePin[P_RB1_K]->setEnode( nodeK );
    }
    applyData();
}

void Unijunction::applyData()
{
    m_pnp->setGain( m_data.gainP );
    m_npn->setGain( m_data.gainN );
    m_rsh->setRes( m_data.shunt );
    if( m_ujt ){
        m_rb1->setRes( m_data.rbb*m_data.eta );
        m_rb2->setRes( m_data.rbb*(1-m_data.eta) );
    }
}

void Unijunction::setModel( QString m )
{
    QHash<QString, unijData_t>& models = m_ujt ? m_ujts : m_puts;
    if( !models.contains( m ) ) return;
    m_model = m;
    if( m == "Custom" ) return;
    if( Simulator::self()->isRunning() ) CircuitWidget::self()->powerCircOff();
    m_data = models.value( m );
}

void Unijunction::custom()
{
    if( !m_setting ) m_model = "Custom";
    if( Simulator::self()->isRunning() ) CircuitWidget::self()->powerCircOff();
}

// A value equal to the model's (a circuit being loaded) keeps the model
void Unijunction::setGainP( double g ){ if( g < 1 ) g = 1;       if( g == m_data.gainP ) return; m_data.gainP = g; custom(); }
void Unijunction::setGainN( double g ){ if( g < 0.01 ) g = 0.01; if( g == m_data.gainN ) return; m_data.gainN = g; custom(); }
void Unijunction::setShunt( double r ){ if( r < 1 ) r = 1;       if( r == m_data.shunt ) return; m_data.shunt = r; custom(); }

double Unijunction::cj1(){ return m_pnp->cje(); }
double Unijunction::cj2(){ return m_pnp->cjc(); }
double Unijunction::cj3(){ return m_npn->cje(); }
void Unijunction::setCj1( double c ){ m_pnp->setCje( c < 0 ? 0 : c ); }
void Unijunction::setCj2( double c ){ m_pnp->setCjc( c < 0 ? 0 : c ); }
void Unijunction::setCj3( double c ){ m_npn->setCje( c < 0 ? 0 : c ); }

// ---------------------------------------------------------------- PUT

#undef  tr
#define tr(str) simulideTr("PUT",str)

Component* PUT::construct( QString type, QString id )
{ return new PUT( type, id ); }

LibraryItem* PUT::libraryItem()
{
    return new LibraryItem(
        tr("PUT"),
        "Rectifiers",
        "scr.png",
        "PUT",
        PUT::construct );
}

PUT::PUT( QString type, QString id )
   : Unijunction( type, id, false )
{
    m_area = QRectF(-10,-8, 20, 16 );
    setLabelPos(-12, 10, 0 );

    m_pin[0] = new Pin( 180, QPoint(-16, 0 ), id+"-anode"  , 0, this );
    m_pin[1] = new Pin( 0,   QPoint( 16, 0 ), id+"-cathode", 1, this );
    m_pin[2] = new Pin( 90,  QPoint(-8,-16 ), id+"-gate"   , 2, this );
    m_pin[2]->setLength( 9 );

    m_pnp->setEpin( 0, m_ePin[P_PNP_C] );   // collector, emitter, base
    m_pnp->setEpin( 1, m_pin[0] );
    m_pnp->setEpin( 2, m_pin[2] );
    m_npn->setEpin( 0, m_ePin[P_NPN_C] );
    m_npn->setEpin( 1, m_pin[1] );
    m_npn->setEpin( 2, m_ePin[P_NPN_B] );
    m_rsh->setEpin( 0, m_ePin[P_SH_A] );
    m_rsh->setEpin( 1, m_ePin[P_SH_K] );

    addPropGroup( { tr("Main"), {
        new StrProp<PUT>("Model", tr("Model"), modelList()
                        , this, &PUT::model, &PUT::setModel, 0, "enum" ),
    },0} );
    addPropGroup( { tr("Custom"), {
        new DoubProp<PUT>("GainP", tr("PNP gain"), ""
                         , this, &PUT::gainP, &PUT::setGainP ),
        new DoubProp<PUT>("GainN", tr("NPN gain (sets the valley current)"), ""
                         , this, &PUT::gainN, &PUT::setGainN ),
        new DoubProp<PUT>("Shunt", tr("NPN base shunt (sets the peak current)"), "Ω"
                         , this, &PUT::shunt, &PUT::setShunt ),
    },0} );
    addPropGroup( { tr("Capacitance"), {
        new DoubProp<PUT>("Cj1", tr("Anode-Gate junction (J1) at 0 V"), "pF"
                         , this, &PUT::cj1, &PUT::setCj1 ),
        new DoubProp<PUT>("Cj2", tr("Centre junction (J2) at 0 V"), "pF"
                         , this, &PUT::cj2, &PUT::setCj2 ),
        new DoubProp<PUT>("Cj3", tr("Gate-Cathode junction (J3) at 0 V"), "pF"
                         , this, &PUT::cj3, &PUT::setCj3 ),
    },0} );
    m_setting = true;
    setModel( m_puts.contains("2N6027") ? "2N6027" : "Custom" );
    m_setting = false;
}
PUT::~PUT(){}

void PUT::paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w )
{
    Component::paint( p, o, w );

    p->setBrush( Theme::line() );
 static const QPointF points[3] = {
        QPointF( 7, 0 ),
        QPointF(-8,-7 ),
        QPointF(-8, 7 )              };
    p->drawPolygon( points, 3 );

    QPen pen = p->pen();
    pen.setWidth( 3 );
    p->setPen( pen );
    p->drawLine( 7, -6, 7, 6 );

    Component::paintSelected( p );
}

// ---------------------------------------------------------------- UJT

#undef  tr
#define tr(str) simulideTr("UJT",str)

Component* UJT::construct( QString type, QString id )
{ return new UJT( type, id ); }

LibraryItem* UJT::libraryItem()
{
    return new LibraryItem(
        tr("UJT"),
        "Transistors",
        "bjt.png",
        "UJT",
        UJT::construct );
}

UJT::UJT( QString type, QString id )
   : Unijunction( type, id, true )
{
    m_area = QRectF(-12,-14, 28, 28 );
    setLabelPos( 18, 0, 0 );

    m_pin[0] = new Pin( 180, QPoint(-16, 0 ), id+"-emitter", 0, this );
    m_pin[1] = new Pin( 270, QPoint( 8, 16 ), id+"-base1"  , 1, this );
    m_pin[2] = new Pin( 90,  QPoint( 8,-16 ), id+"-base2"  , 2, this );

    m_pnp->setEpin( 0, m_ePin[P_PNP_C] );
    m_pnp->setEpin( 1, m_pin[0] );
    m_pnp->setEpin( 2, m_ePin[P_PNP_B] );
    m_npn->setEpin( 0, m_ePin[P_NPN_C] );
    m_npn->setEpin( 1, m_pin[1] );
    m_npn->setEpin( 2, m_ePin[P_NPN_B] );
    m_rsh->setEpin( 0, m_ePin[P_SH_A] );
    m_rsh->setEpin( 1, m_ePin[P_SH_K] );
    m_rb2->setEpin( 0, m_pin[2] );
    m_rb2->setEpin( 1, m_ePin[P_RB2_G] );
    m_rb1->setEpin( 0, m_ePin[P_RB1_G] );
    m_rb1->setEpin( 1, m_ePin[P_RB1_K] );

    addPropGroup( { tr("Main"), {
        new StrProp<UJT>("Model", tr("Model"), modelList()
                        , this, &UJT::model, &UJT::setModel, 0, "enum" ),
        new DoubProp<UJT>("RBB", tr("Interbase resistance"), "kΩ"
                         , this, &UJT::rbb, &UJT::setRbb ),
        new DoubProp<UJT>("Eta", tr("Intrinsic standoff ratio"), ""
                         , this, &UJT::eta, &UJT::setEta ),
    },0} );
    addPropGroup( { tr("Custom"), {
        new DoubProp<UJT>("GainP", tr("PNP gain"), ""
                         , this, &UJT::gainP, &UJT::setGainP ),
        new DoubProp<UJT>("GainN", tr("NPN gain (sets the valley current)"), ""
                         , this, &UJT::gainN, &UJT::setGainN ),
        new DoubProp<UJT>("Shunt", tr("NPN base shunt (sets the peak current)"), "Ω"
                         , this, &UJT::shunt, &UJT::setShunt ),
    },0} );
    addPropGroup( { tr("Capacitance"), {
        new DoubProp<UJT>("Cj1", tr("Emitter junction (J1) at 0 V"), "pF"
                         , this, &UJT::cj1, &UJT::setCj1 ),
        new DoubProp<UJT>("Cj2", tr("Centre junction (J2) at 0 V"), "pF"
                         , this, &UJT::cj2, &UJT::setCj2 ),
        new DoubProp<UJT>("Cj3", tr("Base 1 junction (J3) at 0 V"), "pF"
                         , this, &UJT::cj3, &UJT::setCj3 ),
    },0} );
    m_setting = true;
    setModel( m_ujts.contains("2N2646") ? "2N2646" : "Custom" );
    m_setting = false;
}
UJT::~UJT(){}

void UJT::setRbb( double r ){ if( r < 1 ) r = 1; if( r == m_data.rbb ) return; m_data.rbb = r; custom(); }
void UJT::setEta( double e )
{
    if( e < 0.01 ) e = 0.01;
    if( e > 0.99 ) e = 0.99;
    if( e == m_data.eta ) return;
    m_data.eta = e; custom();
}

void UJT::paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w )
{
    Component::paint( p, o, w );

    p->setBrush( Theme::body() );
    p->drawEllipse( m_area );

    QPen pen = p->pen();
    pen.setWidth( 3 );
    p->setPen( pen );
    p->drawLine( 2,-9, 2, 9 );                 // the bar (base 1 to base 2)
    pen.setWidth( 1 );
    p->setPen( pen );
    p->drawLine( 2,-8, 8,-8 );                 // to base 2
    p->drawLine( 2, 8, 8, 8 );                 // to base 1
    p->drawLine( 8,-8, 8,-12 );
    p->drawLine( 8, 8, 8, 12 );
    p->drawLine(-12, 0,-6, 0 );
    p->drawLine(-6, 0, 2, 5 );                 // emitter, slanted onto the bar

    p->setBrush( Theme::line() );
 static const QPointF arrow[3] = {
        QPointF( 0.5, 4.1 ),
        QPointF(-3.4, 4.6 ),
        QPointF(-1.6, 1.7 ) };
    p->drawPolygon( arrow, 3 );

    Component::paintSelected( p );
}

void Unijunction::simState( StateIO& s )
{
    eElement::simState( s );
    s.io( m_ujt );
    s.io( m_setting );
}

void UJT::simState( StateIO& s )
{
    Unijunction::simState( s );
}

void PUT::simState( StateIO& s )
{
    Unijunction::simState( s );
}
