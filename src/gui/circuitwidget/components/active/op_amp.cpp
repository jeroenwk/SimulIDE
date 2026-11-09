/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QPainter>

#include <QtMath>
#include <QDebug>
#include <limits>

#include "op_amp.h"
#include "itemlibrary.h"
#include "propdialog.h"
#include "connector.h"
#include "simulator.h"
#include "e-node.h"
#include "iopin.h"

#include "doubleprop.h"
#include "boolprop.h"

#define tr(str) simulideTr("OpAmp",str)

Component* OpAmp::construct( QString type, QString id )
{ return new OpAmp( type, id ); }

LibraryItem* OpAmp::libraryItem()
{
    return new LibraryItem(
        tr("OpAmp"),
        "Other Active",
        "opamp.png",
        "opAmp",
        OpAmp::construct );
}

OpAmp::OpAmp( QString type, QString id )
     : Component( type, id )
     , eElement( id )
     , m_aux( id+"-aux" )
{
    m_aux.setNumEpins( aNum );
    m_area = QRect( -18, -8*2, 36, 8*2*2 );
    setLabelPos(-16,-32, 0);

    m_pin.resize( 5 );
    m_pin[0] = m_inputP = new IoPin( 180, QPoint(-16-8,-8), id+"-inputNinv", 0, this, input );
    m_pin[0]->setFontSize( 9 );
    m_pin[0]->setSpace( 1.7 );
    m_pin[0]->setLabelText("+");
    m_pin[0]->setLabelColor( Qt::red );

    m_pin[1] = m_inputN = new IoPin( 180, QPoint(-16-8, 8), id+"-inputInv", 1, this, input );
    m_pin[1]->setFontSize( 9 );
    m_pin[1]->setSpace( 1.7 );
    m_pin[1]->setLabelText("–");  // U+2013
    m_pin[1]->setLabelColor( QColor( 0, 0, 0 ) );

    m_output = new IoPin(   0, QPoint( 16+8, 0), id+"-output",   2, this, source );
    m_pin[2] = m_output;

    m_pin[3] = new Pin( 90, QPoint(0,-16), id+"-powerPos", 3, this );
    m_pin[3]->setFontSize( 9 );
    m_pin[3]->setSpace( 1.7 );
    m_pin[3]->setLabelText("+");
    m_pin[3]->setLabelColor( Qt::red );

    m_pin[4] = new Pin( 270, QPoint(0, 16), id+"-powerNeg", 4, this );
    m_pin[4]->setFontSize( 9 );
    m_pin[4]->setSpace( 1.7 );
    m_pin[4]->setLabelText("–");  // U+2013
    m_pin[4]->setLabelColor( QColor( 0, 0, 0 ) );
    
    m_switchPins = false;
    setPowerPins( false );

    m_gain = 1000;
    m_outImp = cero_doub;
    m_voltPosDef = 5;
    m_voltNegDef = 0;

    addPropGroup( { tr("Main"), {
        new DoubProp<OpAmp>("Gain", tr("Gain"),""
                           , this, &OpAmp::gain, &OpAmp::setGain ),

        new DoubProp<OpAmp>("Out_Imped", tr("Output Impedance"),"Ω"
                           , this, &OpAmp::outImp, &OpAmp::setOutImp ),
        showStateProp()
    },0} );
    enableStates( true );

    addPropGroup( { tr("Supply"), {
        new BoolProp<OpAmp>("Power_Pins", tr("Use Supply Pins"),""
                           , this, &OpAmp::powerPins, &OpAmp::setPowerPins, propNoCopy ),

        new BoolProp<OpAmp>("Switch_Pins", tr("Switch Supply Pins"),""
                           , this, &OpAmp::switchPins, &OpAmp::setSwitchPins, propNoCopy ),

        new DoubProp<OpAmp>("Volt_Pos", tr("V+"),"V"
                           , this, &OpAmp::voltPos,&OpAmp::setVoltPos ),

        new DoubProp<OpAmp>("Volt_Neg", tr("V-"),"V"
                           , this, &OpAmp::voltNeg, &OpAmp::setVoltNeg ),
    },0 } );
}
OpAmp::~OpAmp(){}

// BH-032: the op amp used to guess its output from its own last input and output (a secant
// iteration per op amp). Two high-gain stages coupled through resistors or diodes (AoE 4.63
// absolute value) never converged that way: each one's move upset the other. Now the linear
// region is stamped in the matrix, so the solver sees every feedback loop in one solve:
//   output node: G*(Vout - gain*(V+ - V-)) = current from the rest of the circuit, G = 1/Out_Imped
// (a voltage-controlled current source from the inputs into the output node, plus the output's own
// G to ground, which the IoPin stamps). At a rail the output is a source at the rail voltage.
// The region is chosen by iterating: LIN while gain*vd stays between the rails, SAT_P/SAT_N while
// gain*vd is beyond the rail it sits on. With positive feedback (comparators with hysteresis,
// astables) the LIN solution is the unstable middle point: the first LIN solve after a rail
// measures the loop (d vd / d out between the rail and the LIN point); gain*slope > 1 means
// unstable, and the output runs to the other rail instead.
static eNode s_gnd("");   // Node 0, for terms between the output and itself

#define OPAMP_LOG qDebug() << "[opamp]" << m_elmId
static const bool s_trace = qEnvironmentVariableIsSet("SIMULIDE_OPAMP_TRACE"); // Every iteration

void OpAmp::initialize()
{
    m_accuracy = 5e-6;
    m_mode = SAT_P;            // A rail first: a positive-feedback circuit starts at a rail, as a real one
    m_flips = 0;
    m_flipTime = 0;
    m_checkStable = false;
    m_satVd = m_satOut = 0;
    m_stampI = m_stampVpos = m_stampVneg = 0;
    m_constIn = 0;
    m_dirty = true;
    m_voltPos = m_voltPosDef;
    m_voltNeg = m_voltNegDef;
    m_region = -1;
    m_output->setTrueCurrent( std::numeric_limits<double>::quiet_NaN() );
    clearStates();
}

void OpAmp::stamp()
{
    for( int i=0; i<aNum; ++i ) m_aux.getEpin( i )->setEnode( NULL );
    m_diag[0] = m_diag[1] = false;

    eNode* oN = m_output->getEnode();
    eNode* pN = m_inputP->getEnode();
    eNode* nN = m_inputN->getEnode();
    if( pN ) pN->addToNoLinList( this );
    if( nN ) nN->addToNoLinList( this );
    if( m_powerPins ){
        if( m_pin[3]->getEnode() ) m_pin[3]->getEnode()->addToNoLinList( this );
        if( m_pin[4]->getEnode() ) m_pin[4]->getEnode()->addToNoLinList( this );
    }
    if( !oN ) return;
    oN->addToNoLinList( this );
    m_output->createCurrent();

    auto term = [&]( int aOut, int aIn, eNode* in ){   // gain term of one input
        if( !in || in->getNodeNumber() == 0 ) return;  // Ground or open: a constant (m_constIn)
        ePin* p = m_aux.getEpin( aOut );
        p->setEnode( oN );
        if( in == oN ){ p->setEnodeComp( &s_gnd ); m_diag[aOut] = true; } // Output wired to the input: diagonal
        else{
            p->addSingAdm( in->getNodeNumber(), 0 );
            ePin* q = m_aux.getEpin( aIn );            // Zero entry back: same solver group
            q->setEnode( in );
            q->addSingAdm( oN->getNodeNumber(), 0 );
        }
    };
    term( aOP, aPO, pN );
    term( aON, aNO, nN );
    m_dirty = true;
}

void OpAmp::updateStep()
{
    if( !m_changed ) return;
    m_changed = false;

    m_output->setImpedance( m_outImp );
    m_dirty = true;
    voltChanged();
}

void OpAmp::stampMode()
{
    double G  = 1/m_outImp;
    double gG = ( m_mode == LIN ) ? m_gain*G : 0;
    for( int i : { aOP, aON } )
    {
        ePin* p = m_aux.getEpin( i );
        if( !p->getEnode() ) continue;
        double s = ( i == aOP ) ? gG : -gG;               // +gain*G*V+, -gain*G*V-
        if( m_diag[i] ) p->stampAdmitance(-s );   // Same node: -s on the diagonal
        else                    p->stampSingAdm( s );
    }
    double I = ( m_mode == LIN  ) ? G*m_constIn
             : ( m_mode == SAT_P) ? G*m_voltPos : G*m_voltNeg;
    m_output->stampCurrent( I );
    m_stampI = I;
    m_stampVpos = m_voltPos;
    m_stampVneg = m_voltNeg;
    m_dirty = false;
}

void OpAmp::stepSettled()
{
    if( m_output->getEnode() ){   // Into the part: G*(Vout - source), the source is in the aux pins too
        double src = ( m_mode == LIN ) ? m_gain*( m_inputP->getVoltage()-m_inputN->getVoltage() )
                   : ( m_mode == SAT_P ) ? m_voltPos : m_voltNeg;
        m_output->setTrueCurrent( ( m_output->getVoltage()-src )/m_outImp );
    }
    double ideal = ( m_inputP->getVoltage()-m_inputN->getVoltage() )*m_gain;
    double h = 0.01;
    int r = m_region;
    if( r == 1 ){ if( ideal < m_voltPos-h ) r = 0; }
    else if( r == 2 ){ if( ideal > m_voltNeg+h ) r = 0; }
    if( r <= 0 ){
        if     ( ideal >= m_voltPos+h ) r = 1;   // Also at the first step: inputs at 0 V sit
        else if( ideal <= m_voltNeg-h ) r = 2;   // exactly on a 0 V rail, that is not SAT-
        else r = 0;
    }
    if( r == m_region ) return;
    m_region = r;
    if     ( r == 0 ) reportState("LINEAR", stOn );
    else if( r == 1 ) reportState("SAT+",   stWait );
    else              reportState("SAT-",   stWait );
}

void OpAmp::voltChanged() // Called when any pin node change volt
{
    Simulator* sim = Simulator::self();
    sim->addToSettled( this );   // State from this step's final values
    if( !m_output->getEnode() ) return;

    if( m_powerPins ){
        m_voltPos = m_pin[3]->getVoltage();
        m_voltNeg = m_pin[4]->getVoltage();
    }else{
        m_voltPos = m_voltPosDef;
        m_voltNeg = m_voltNegDef;
    }
    if( m_dirty ){            // Start or property change: solve with the current mode's stamp first,
        stampMode();          // so the next call judges real voltages (the rail check needs them)
        sim->notCorverged();
        return;
    }
    if( m_powerPins ){
        m_voltPos = m_pin[3]->getVoltage();
        m_voltNeg = m_pin[4]->getVoltage();
    }else{
        m_voltPos = m_voltPosDef;
        m_voltNeg = m_voltNegDef;
    }
    double vp = m_inputP->getVoltage();
    double vn = m_inputN->getVoltage();
    double vd = vp-vn;
    double ideal = vd*m_gain;
    double out = m_output->getVoltage();

    // Inputs on ground or open are constants in the LIN current
    double cst = 0;
    eNode* pN = m_inputP->getEnode();
    eNode* nN = m_inputN->getEnode();
    if( !pN || pN->getNodeNumber() == 0 ) cst += vp;
    if( !nN || nN->getNodeNumber() == 0 ) cst -= vn;
    cst *= m_gain;
    if( qFabs( cst-m_constIn ) > 1e-9*( 1+qFabs( cst ) ) ){ m_constIn = cst; if( m_mode == LIN ) m_dirty = true; }

    if( sim->circTime() != m_flipTime ){ m_flipTime = sim->circTime(); m_flips = 0; }

    double eps = 1e-6*( 1+qFabs( m_voltPos )+qFabs( m_voltNeg ) );
    int mode = m_mode;
    if( mode == LIN )
    {
        if( m_checkStable )                    // First LIN solve after a rail
        {
            m_checkStable = false;
            double dOut = out-m_satOut;
            if( qFabs( dOut ) > 1e-12 && m_gain*( vd-m_satVd )/dOut > 1 && m_flips < 4 )
            {                                  // Positive feedback: the LIN point is unstable
                m_flips++;
                mode = ( m_satOut >= m_voltPos-eps ) ? SAT_N : SAT_P;
                if( s_trace ) OPAMP_LOG << "unstable loop (gain x slope" << m_gain*( vd-m_satVd )/dOut << "), to" << ( mode == SAT_P ? "SAT+" : "SAT-" );
            }
        }
        if( mode == LIN ){
            if     ( ideal > m_voltPos+eps ) mode = SAT_P;
            else if( ideal < m_voltNeg-eps ) mode = SAT_N;
        }
    }
    else if( mode == SAT_P ){                  // Leaving a rail: always through a LIN solve, which
        if( ideal < m_voltPos-eps ) mode = LIN;  // finds the feedback (or its instability)
    }
    else{
        if( ideal > m_voltNeg+eps ) mode = LIN;
    }
    if( mode == LIN && m_mode != LIN ){         // Leaving a rail: check the loop at the next solve
        m_checkStable = true;
        m_satVd  = vd;
        m_satOut = out;
    }
    if( mode != m_mode ){ m_mode = mode; m_dirty = true; }
    else if( mode == SAT_P && qFabs( m_voltPos-m_stampVpos ) > m_accuracy ) m_dirty = true;
    else if( mode == SAT_N && qFabs( m_voltNeg-m_stampVneg ) > m_accuracy ) m_dirty = true;

    if( s_trace ) OPAMP_LOG << "t" << sim->circTime() << "vp" << vp << "vn" << vn << "out" << out
                            << "ideal" << ideal << "mode" << m_mode << "dirty" << m_dirty << "check" << m_checkStable;
    if( !m_dirty && !m_checkStable ) return;   // Converged
    sim->notCorverged();
    if( m_dirty ) stampMode();
}

void OpAmp::setOutImp( double imp )
{
    if( imp < cero_doub ) imp = cero_doub;
    m_outImp = imp;
    m_changed = true;
    if( !Simulator::self()->isRunning() ) updateStep();
}

void OpAmp::setPowerPins( bool set )
{
    m_pin[3]->setEnabled( set );
    m_pin[3]->setVisible( set );
    m_pin[4]->setEnabled( set );
    m_pin[4]->setVisible( set );
    
    if( !set )
    {
        m_pin[3]->removeConnector();
        m_pin[4]->removeConnector();
    }
    m_powerPins = set;
    updtProperties();
}

void OpAmp::setSwitchPins( bool s )
{
    m_switchPins = s;
    int angleP = s ? 270 : 90;
    int angleN = s ? 90  : 270;
    qreal yP   = s ?  16 : -16;
    qreal yN   = s ? -16 : 16;
    m_pin[3]->setPinAngle( angleP );
    m_pin[3]->setY( yP );
    m_pin[4]->setPinAngle( angleN );
    m_pin[4]->setY( yN );
}

void OpAmp::updtProperties()
{
    if( !m_propDialog ) return;
    m_propDialog->showProp("Volt_Pos", !m_powerPins );
    m_propDialog->showProp("Volt_Neg", !m_powerPins );
    m_propDialog->showProp("Switch_Pins", m_powerPins );
    m_propDialog->adjustWidgets();
}

void OpAmp::slotProperties()
{
    Component::slotProperties();
    updtProperties();
}

QPainterPath OpAmp::shape() const
{
    QPainterPath path;
    
    QVector<QPointF> points;
    
    points << QPointF(-16,-16 )
           << QPointF(-16, 16 )
           << QPointF( 16,  1 )
           << QPointF( 16, -1 );
        
    path.addPolygon( QPolygonF(points) );
    path.closeSubpath();
    return path;
}

void OpAmp::paint( QPainter* p, const QStyleOptionGraphicsItem* option, QWidget* widget )
{
    Component::paint( p, option, widget );

    QPen pen = p->pen();
    pen.setWidth(2);
    p->setPen(pen);

 static const QPointF points[4] = {
        QPointF(-16,-16 ),
        QPointF(-16, 16 ),
        QPointF( 16,  1 ),
        QPointF( 16, -1 )            };

    p->drawPolygon(points, 4);

    Component::paintSelected( p );
}
