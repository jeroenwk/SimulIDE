/***************************************************************************
 *   Copyright (C) 2026 by the SimulIDE fork contributors                  *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QPainter>
#include <QtMath>

#include "pllvco.h"
#include "theme.h"
#include "itemlibrary.h"
#include "simulator.h"
#include "e-resistor.h"
#include "e-diode.h"
#include "e-node.h"
#include "pin.h"
#include "appfonts.h"

static const double G_OFF = 1e-9;   // High impedance

// ---------------------------------------------------------------- NortonOut

void NortonOut::init( eElement* owner, QString id, int firstEpin, Pin* p )
{
    pin = p;
    a = owner->getEpin( firstEpin );
    b = owner->getEpin( firstEpin+1 );
    res = new eResistor( id );
    res->setEpin( 0, a );
    res->setEpin( 1, b );
    res->setAdmit( G_OFF );
}

void NortonOut::setNodes( eNode* ref )
{
    a->setEnode( pin->getEnode() );
    b->setEnode( pin->getEnode() ? ref : nullptr );
    a->createCurrent();
    b->createCurrent();
    m_v = 0; m_g = G_OFF;
    res->setAdmit( G_OFF );
}

void NortonOut::set( double v, double g )
{
    if( !a->getEnode() ) return;
    if( g != m_g ){ m_g = g; res->setAdmit( g ); }
    else if( qFabs( v-m_v ) < 1e-9 ) return;
    m_v = v;
    a->stampCurrent( g*v );
    b->stampCurrent(-g*v );
}

double NortonOut::current()
{
    if( !a->getEnode() || !b->getEnode() ) return 0;
    return m_g*( m_v-( a->getVoltage()-b->getVoltage() ) );
}

void NortonOut::remove() { delete res; }

void NortonOut::simState( StateIO& s ) { s.io( m_v ); s.io( m_g ); }

// ---------------------------------------------------------------- PllVcoBase

PllVcoBase::PllVcoBase( QString type, QString id )
          : Component( type, id )
          , eElement( id )
{
    m_pending = false;
}
PllVcoBase::~PllVcoBase(){}

Pin* PllVcoBase::addPin( int i, QString id, QString label, bool right, int row )
{
    int x = right ? 32 : -32;
    int y = -28+row*8;
    Pin* p = new Pin( right ? 0 : 180, QPoint( x, y ), m_id+"-"+id, i, this );
    p->setLabelText( label );
    p->setFontSize( 4 );
    p->setSpace( 1.2 );
    m_pin[i] = p;
    return p;
}

void PllVcoBase::watch( std::initializer_list<int> pins )
{
    for( int i : pins ) if( m_pin[i]->getEnode() ) m_pin[i]->changeCallBack( this );
}

void PllVcoBase::voltChanged()
{
    Simulator::self()->addToSettled( this );
}

void PllVcoBase::later()
{
    if( m_pending ) return;
    m_pending = true;
    Simulator::self()->addEvent( 1, this );
}

void PllVcoBase::runEvent()
{
    m_pending = false;
    applyOutputs();
}

void PllVcoBase::paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w )
{
    Component::paint( p, o, w );
    p->setBrush( Theme::body() );
    p->drawRect( m_area );
    QFont f = AppFonts::sans();
    f.setPixelSize( 7 );
    f.setBold( true );
    p->setFont( f );
    p->save();
    p->rotate(-90 );
    p->drawText( QRectF( m_area.top(), -6, m_area.height(), 12 ), Qt::AlignCenter, m_title );
    p->restore();
    Component::paintSelected( p );
}

void PllVcoBase::simState( StateIO& s )
{
    eElement::simState( s );
    s.io( m_pending );
}

// ---------------------------------------------------------------- CD4046B

#define tr(str) simulideTr("Cd4046",str)

Component* Cd4046::construct( QString type, QString id )
{ return new Cd4046( type, id ); }

LibraryItem* Cd4046::libraryItem()
{
    return new LibraryItem(
        tr("PLL 4046"),
        "Other Active",
        "ic2.png",
        "Pll4046",
        Cd4046::construct );
}

Cd4046::Cd4046( QString type, QString id )
      : PllVcoBase( type, id )
{
    m_title = "4046";
    m_area = QRectF(-24,-34, 48, 68 );
    setLabelPos(-24,-48, 0 );

    m_pin.resize( 16 );   // Pin numbers - 1; inputs on the left, outputs on the right
    addPin( P_SIG, "sig",  "SIG IN",  false, 0 );
    addPin( P_CMP, "comp", "COMP IN", false, 1 );
    addPin( P_VIN, "vcoin","VCO IN",  false, 2 );
    addPin( P_INH, "inh",  "INH",     false, 3 );
    addPin( P_C1A, "c1a",  "C1A",     false, 4 );
    addPin( P_C1B, "c1b",  "C1B",     false, 5 );
    addPin( P_R1,  "r1",   "R1",      false, 6 );
    addPin( P_R2,  "r2",   "R2",      false, 7 );
    addPin( P_VDD, "vdd",  "VDD",     true,  0 );
    addPin( P_PC1, "pc1",  "PC1 OUT", true,  1 );
    addPin( P_PC2, "pc2",  "PC2 OUT", true,  2 );
    addPin( P_PP,  "pp",   "PCP OUT", true,  3 );
    addPin( P_VCO, "vcoout","VCO OUT",true,  4 );
    addPin( P_SF,  "sf",   "DEM OUT", true,  5 );
    addPin( P_ZEN, "zener","ZENER",   true,  6 );
    addPin( P_VSS, "vss",  "VSS",     true,  7 );

    setNumEpins( E_COUNT );
    m_pc1.init( this, id+"-pc1", E_OUT+0,  m_pin[P_PC1] );
    m_pc2.init( this, id+"-pc2", E_OUT+2,  m_pin[P_PC2] );
    m_pp .init( this, id+"-pp",  E_OUT+4,  m_pin[P_PP ] );
    m_vco.init( this, id+"-vco", E_OUT+6,  m_pin[P_VCO] );
    m_sf .init( this, id+"-sf",  E_OUT+8,  m_pin[P_SF ] );
    m_r1 .init( this, id+"-r1",  E_OUT+10, m_pin[P_R1 ] );
    m_r2 .init( this, id+"-r2",  E_OUT+12, m_pin[P_R2 ] );

    m_sw6 = new eResistor( id+"-sw6" );
    m_sw6->setEpin( 0, m_ePin[E_SW6] );
    m_sw6->setEpin( 1, m_ePin[E_SW6R] );
    m_sw7 = new eResistor( id+"-sw7" );
    m_sw7->setEpin( 0, m_ePin[E_SW7] );
    m_sw7->setEpin( 1, m_ePin[E_SW7R] );
    m_biasH = new eResistor( id+"-biasH" );   // Signal input self-bias: 1 MΩ to VDD/2 [DOC R14]
    m_biasH->setEpin( 0, m_ePin[E_BH1] );
    m_biasH->setEpin( 1, m_ePin[E_BH2] );
    m_biasL = new eResistor( id+"-biasL" );
    m_biasL->setEpin( 0, m_ePin[E_BL1] );
    m_biasL->setEpin( 1, m_ePin[E_BL2] );
    m_zener = new eDiode( id+"-zener" );
    m_zener->setEpin( 0, m_ePin[E_ZA] );
    m_zener->setEpin( 1, m_ePin[E_ZK] );
    m_zener->setModel("Zener Default");
    m_zener->setBrkDownV( 5.5 );               // [DOC VZ 5.5 V typ at 50 µA]
}
Cd4046::~Cd4046(){}

void Cd4046::remove()
{
    for( NortonOut* n : { &m_pc1, &m_pc2, &m_pp, &m_vco, &m_sf, &m_r1, &m_r2 } ) n->remove();
    delete m_sw6; delete m_sw7; delete m_biasH; delete m_biasL; delete m_zener;
    Component::remove();
}

void Cd4046::initialize()
{
    m_pending = false;
    m_phaseA = true;
    m_iVco = 0;
    m_iStamped = 0;
    m_swState = 0;
    m_sig = m_cmp = false;
    m_up = m_dn = false;
    m_inh = false;
}

void Cd4046::stamp()
{
    eNode* vss = m_pin[P_VSS]->getEnode();
    for( NortonOut* n : { &m_pc1, &m_pc2, &m_pp, &m_vco, &m_sf, &m_r1, &m_r2 } ) n->setNodes( vss );
    eNode* n6 = m_pin[P_C1A]->getEnode();
    eNode* n7 = m_pin[P_C1B]->getEnode();
    m_ePin[E_C6  ]->setEnode( n6 );
    m_ePin[E_C7  ]->setEnode( n7 );
    m_ePin[E_CREF]->setEnode( vss );
    for( int i : { E_C6, E_C7, E_CREF } ) m_ePin[i]->createCurrent();
    m_ePin[E_SW6 ]->setEnode( n6 );
    m_ePin[E_SW6R]->setEnode( n6 ? vss : nullptr );
    m_ePin[E_SW7 ]->setEnode( n7 );
    m_ePin[E_SW7R]->setEnode( n7 ? vss : nullptr );
    m_sw6->setAdmit( G_OFF );
    m_sw7->setAdmit( G_OFF );
    eNode* nSig = m_pin[P_SIG]->getEnode();
    eNode* nVdd = m_pin[P_VDD]->getEnode();
    m_ePin[E_BH1]->setEnode( nSig ? nVdd : nullptr );
    m_ePin[E_BH2]->setEnode( nVdd ? nSig : nullptr );
    m_ePin[E_BL1]->setEnode( nSig ? vss : nullptr );
    m_ePin[E_BL2]->setEnode( vss ? nSig : nullptr );
    m_biasH->setRes( 2e6 );
    m_biasL->setRes( 2e6 );
    m_ePin[E_ZA]->setEnode( m_pin[P_ZEN]->getEnode() ? vss : nullptr );
    m_ePin[E_ZK]->setEnode( vss ? m_pin[P_ZEN]->getEnode() : nullptr );

    watch({ P_SIG, P_CMP, P_VIN, P_INH, P_C1A, P_C1B, P_R1, P_R2, P_VDD, P_VSS });
    later();   // Outputs from the start
}

double Cd4046::rOut()   // [INFERRED from IOL: 1 mA at 0.4 V (5 V), 2.6 mA at 0.5 V (10 V)]
{
    double vdd = m_pin[P_VDD]->getVoltage()-m_pin[P_VSS]->getVoltage();
    if( vdd < 3 ) vdd = 3;
    return 1900/vdd;
}

double Cd4046::vcoFreq()   // f = I / (2 C 5 V) for the C on pins 6-7: here per farad
{
    return m_iVco/10;
}

void Cd4046::stepSettled()
{
    double vss = m_pin[P_VSS]->getVoltage();
    double vdd = m_pin[P_VDD]->getVoltage()-vss;
    double h   = 0.05*vdd;
    double vs  = m_pin[P_SIG]->getVoltage()-vss;
    double vc  = m_pin[P_CMP]->getVoltage()-vss;
    bool sig = m_sig ? vs > vdd/2-h : vs > vdd/2+h;
    bool cmp = m_cmp ? vc > vdd/2-h : vc > vdd/2+h;
    if( sig && !m_sig ) m_up = true;          // Phase comparator II: rising edges
    if( cmp && !m_cmp ) m_dn = true;
    if( m_up && m_dn ) m_up = m_dn = false;
    m_sig = sig;
    m_cmp = cmp;

    m_inh = m_pin[P_INH]->getVoltage()-vss > vdd/2;
    m_iVco = m_inh ? 0 : qMax( 0.0, m_r1.current() )+qMax( 0.0, m_r2.current() );
    if( !m_inh ){                              // VCO: reverse at 2.5 V across C1, the side charged positive
        double v6 = m_pin[P_C1A]->getVoltage(), v7 = m_pin[P_C1B]->getVoltage();
        double vd = m_phaseA ? v6-v7 : v7-v6;
        if( vd >= 2.5 ) m_phaseA = !m_phaseA;
    }
    later();
}

void Cd4046::applyOutputs()
{
    double vss = m_pin[P_VSS]->getVoltage();
    double vdd = m_pin[P_VDD]->getVoltage()-vss;
    double g = 1/rOut();
    m_pc1.set( ( m_sig != m_cmp ) ? vdd : 0, g );
    if     ( m_up && !m_dn ) m_pc2.set( vdd, g );
    else if( m_dn && !m_up ) m_pc2.set( 0, g );
    else                     m_pc2.set( 0, G_OFF );
    m_pp.set( ( !m_up && !m_dn ) ? vdd : 0, g );
    m_vco.set( ( !m_inh && m_phaseA ) ? vdd : 0, g );
    double vin = qMax( 0.0, m_pin[P_VIN]->getVoltage()-vss );
    if( m_inh ){
        m_sf.set( 0, G_OFF );
        m_r1.set( 0, G_OFF );
        m_r2.set( 0, G_OFF );
    }else{
        m_sf.set( qMax( 0.0, vin-1.8 ), 1/200.0 );   // [DOC offset 1.8 V typ; 200 Ω INFERRED]
        m_r1.set( qMin( vin, vdd ), 1 );
        m_r2.set( vdd, 1 );
    }
    double i = m_iVco;                       // Timing current into the side charged positive
    if( qFabs( i-m_iStamped ) > 1e-6*qFabs( i )+1e-15 || m_swState == 0 ){
        m_iStamped = i;
        m_ePin[E_C6  ]->stampCurrent( m_phaseA ? i : 0 );
        m_ePin[E_C7  ]->stampCurrent( m_phaseA ? 0 : i );
        m_ePin[E_CREF]->stampCurrent(-i );
    }
    int sw = m_inh ? 3 : ( m_phaseA ? 1 : 2 );
    if( sw != m_swState ){
        m_swState = sw;
        m_sw6->setAdmit( sw == 2 ? 0.1 : G_OFF );   // 10 Ω to VSS on the side not charged
        m_sw7->setAdmit( sw == 1 ? 0.1 : G_OFF );
        m_ePin[E_C6]->stampCurrent( m_phaseA && !m_inh ? i : 0 );
        m_ePin[E_C7]->stampCurrent( !m_phaseA && !m_inh ? i : 0 );
    }
}

void Cd4046::simState( StateIO& s )
{
    PllVcoBase::simState( s );
    for( NortonOut* n : { &m_pc1, &m_pc2, &m_pp, &m_vco, &m_sf, &m_r1, &m_r2 } ) n->simState( s );
    s.io( m_iStamped );
    s.io( m_swState );
    s.io( m_phaseA );
    s.io( m_iVco );
    s.io( m_sig );
    s.io( m_cmp );
    s.io( m_up );
    s.io( m_dn );
    s.io( m_inh );
}

// ---------------------------------------------------------------- NE566

#undef  tr
#define tr(str) simulideTr("Ne566",str)

Component* Ne566::construct( QString type, QString id )
{ return new Ne566( type, id ); }

LibraryItem* Ne566::libraryItem()
{
    return new LibraryItem(
        tr("VCO 566"),
        "Other Active",
        "ic2.png",
        "Vco566",
        Ne566::construct );
}

Ne566::Ne566( QString type, QString id )
     : PllVcoBase( type, id )
{
    m_title = "566";
    m_area = QRectF(-24,-18, 48, 36 );
    setLabelPos(-24,-32, 0 );

    m_pin.resize( 8 );
    addPin( P_VC,  "vc",  "VC",  false, 1 );   // rows 1..4: y -20..4
    addPin( P_R1,  "r1",  "R1",  false, 2 );
    addPin( P_C1,  "c1",  "C1",  false, 3 );
    addPin( P_GND, "gnd", "GND", false, 4 );
    addPin( P_VP,  "vp",  "V+",  true,  1 );
    addPin( P_SQ,  "sq",  "SQR", true,  2 );
    addPin( P_TRI, "tri", "TRI", true,  3 );
    addPin( P_NC,  "nc",  "NC",  true,  4 );
    for( Pin* p : m_pin ) p->setY( p->y()+8 );
    m_area = QRectF(-24,-18, 48, 36 );

    setNumEpins( E_COUNT );
    m_sq .init( this, id+"-sq",  E_OUT+0, m_pin[P_SQ ] );
    m_tri.init( this, id+"-tri", E_OUT+2, m_pin[P_TRI] );
    m_r1 .init( this, id+"-r1",  E_OUT+4, m_pin[P_R1 ] );
    m_vcIn = new eResistor( id+"-vcin" );
    m_vcIn->setEpin( 0, m_ePin[E_VC1] );
    m_vcIn->setEpin( 1, m_ePin[E_VC2] );
    m_supply = new eResistor( id+"-supply" );
    m_supply->setEpin( 0, m_ePin[E_S1] );
    m_supply->setEpin( 1, m_ePin[E_S2] );
}
Ne566::~Ne566(){}

void Ne566::remove()
{
    for( NortonOut* n : { &m_sq, &m_tri, &m_r1 } ) n->remove();
    delete m_vcIn; delete m_supply;
    Component::remove();
}

void Ne566::initialize()
{
    m_pending = false;
    m_charging = true;
    m_i = 0;
    m_iStamped = 0;
}

void Ne566::stamp()
{
    eNode* gnd = m_pin[P_GND]->getEnode();
    for( NortonOut* n : { &m_sq, &m_tri, &m_r1 } ) n->setNodes( gnd );
    eNode* n7 = m_pin[P_C1]->getEnode();
    m_ePin[E_C7  ]->setEnode( n7 );
    m_ePin[E_CREF]->setEnode( n7 ? gnd : nullptr );
    m_ePin[E_C7  ]->createCurrent();
    m_ePin[E_CREF]->createCurrent();
    eNode* nVc = m_pin[P_VC]->getEnode();
    m_ePin[E_VC1]->setEnode( nVc );
    m_ePin[E_VC2]->setEnode( nVc ? gnd : nullptr );
    m_vcIn->setRes( 1e6 );                          // [DOC control terminal 1 MΩ]
    eNode* nVp = m_pin[P_VP]->getEnode();
    m_ePin[E_S1]->setEnode( nVp );
    m_ePin[E_S2]->setEnode( nVp ? gnd : nullptr );
    m_supply->setRes( 12/7e-3 );                    // [DOC ICC 7 mA typ at 12 V]
    watch({ P_VC, P_R1, P_C1, P_VP, P_GND });
    later();
}

void Ne566::stepSettled()
{
    double gnd = m_pin[P_GND]->getVoltage();
    double vs  = m_pin[P_VP]->getVoltage()-gnd;
    m_i = qMax( 0.0, -m_r1.current() );             // Into pin 6 through R1 from V+
    double v7 = m_pin[P_C1]->getVoltage()-gnd;
    if     (  m_charging && v7 >= 0.625*vs ) m_charging = false;   // Thresholds 3/8, 5/8 of the
    else if( !m_charging && v7 <= 0.375*vs ) m_charging = true;    // supply [INFERRED: V+/4 swing]
    later();
}

void Ne566::applyOutputs()
{
    double gnd = m_pin[P_GND]->getVoltage();
    double vs  = m_pin[P_VP]->getVoltage()-gnd;
    double v7  = m_pin[P_C1]->getVoltage()-gnd;
    m_r1.set( m_pin[P_VC]->getVoltage()-gnd, 1 );   // Pin 6 held at the control voltage
    double i = m_charging ? m_i : -m_i;
    if( qFabs( i-m_iStamped ) > 1e-6*qFabs( i )+1e-15 ){
        m_iStamped = i;
        m_ePin[E_C7  ]->stampCurrent( i );
        m_ePin[E_CREF]->stampCurrent(-i );
    }
    m_sq.set( m_charging ? 0.95*vs : 0.5*vs, 1/50.0 );     // 5.4 Vpp at 12 V [DOC], levels [INFERRED]
    m_tri.set( 0.5*vs+0.8*( v7-0.5*vs ), 1/50.0 );         // 2.4 Vpp at 12 V [DOC]
}

void Ne566::simState( StateIO& s )
{
    PllVcoBase::simState( s );
    for( NortonOut* n : { &m_sq, &m_tri, &m_r1 } ) n->simState( s );
    s.io( m_charging );
    s.io( m_i );
    s.io( m_iStamped );
}
