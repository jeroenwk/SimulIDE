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
#include "circuitwidget.h"

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
     , m_bias( id+"-bias" )
{
    m_bias.setNumEpins( 2 );
    m_slewRate = m_ib = m_ios = m_vos = 0;
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
    addPropGroup( { tr("Errors"), {
        new DoubProp<OpAmp>("SlewRate", tr("Slew Rate (0 = none)"),"V/µs"
                           , this, &OpAmp::slewRate, &OpAmp::setSlewRate ),
        new DoubProp<OpAmp>("Ib", tr("Input Bias Current"),"nA"
                           , this, &OpAmp::biasCurr, &OpAmp::setBiasCurr ),
        new DoubProp<OpAmp>("Ios", tr("Input Offset Current"),"nA"
                           , this, &OpAmp::offsetCurr, &OpAmp::setOffsetCurr ),
        new DoubProp<OpAmp>("Vos", tr("Input Offset Voltage"),"mV"
                           , this, &OpAmp::offsetVolt, &OpAmp::setOffsetVolt ),
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
    m_changes = 0;
    m_held = false;
    m_flipTime = 0;
    m_checkStable = false;
    m_satVd = m_satOut = 0;
    m_stampI = m_stampVpos = m_stampVneg = 0;
    m_constIn = 0;
    m_dirty = true;
    m_linIter = 0;
    m_lastVd = 0;
    m_nudge = 0;
    m_voltPos = m_voltPosDef;
    m_voltNeg = m_voltNegDef;
    m_region = -1;
    m_slewV = 0;
    m_lastOut = 0;
    m_lastT = 0;
    m_lastValid = false;
    m_slewDir = 0;
    m_grace = 0;
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
    m_bias.getEpin( 0 )->setEnode( nullptr );
    m_bias.getEpin( 1 )->setEnode( nullptr );
    if( m_ib != 0 || m_ios != 0 ){
        m_bias.getEpin( 0 )->setEnode( pN );
        m_bias.getEpin( 1 )->setEnode( nN );
        m_bias.getEpin( 0 )->createCurrent();
        m_bias.getEpin( 1 )->createCurrent();
        stampBias();
    }
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
    double I = ( m_mode == LIN  ) ? G*( m_constIn+m_nudge )
             : ( m_mode == SAT_P) ? G*m_voltPos
             : ( m_mode == SAT_N) ? G*m_voltNeg : G*m_slewV;
    m_output->stampCurrent( I );
    m_stampI = I;
    m_stampVpos = m_voltPos;
    m_stampVneg = m_voltNeg;
    m_dirty = false;
}

void OpAmp::stepSettled()
{
    if( m_output->getEnode() ){   // Into the part: G*(Vout - source), the source is in the aux pins too
        double src = ( m_mode == LIN ) ? m_gain*( m_inputP->getVoltage()-m_inputN->getVoltage()+m_vos )
                   : ( m_mode == SAT_P ) ? m_voltPos : ( m_mode == SAT_N ) ? m_voltNeg : m_slewV;
        m_output->setTrueCurrent( ( m_output->getVoltage()-src )/m_outImp );
        m_lastOut = m_output->getVoltage();
        m_lastT = Simulator::self()->circTime();
        m_lastValid = true;
        if( m_mode != SLEW ) m_grace = 0;
    }
    double ideal = ( m_inputP->getVoltage()-m_inputN->getVoltage()+m_vos )*m_gain;
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
    double vd = vp-vn+m_vos;
    double ideal = vd*m_gain;
    double out = m_output->getVoltage();

    if( m_mode == SLEW ){                      // The output ramps in runEvent(); only follow the rails
        if( m_slewV > m_voltPos ){ m_slewV = m_voltPos; m_dirty = true; }
        if( m_slewV < m_voltNeg ){ m_slewV = m_voltNeg; m_dirty = true; }
        if( m_dirty ){ sim->notCorverged(); stampMode(); }
        return;
    }
    // Inputs on ground or open are constants in the LIN current (and the offset voltage)
    double cst = m_vos;
    eNode* pN = m_inputP->getEnode();
    eNode* nN = m_inputN->getEnode();
    if( !pN || pN->getNodeNumber() == 0 ) cst += vp;
    if( !nN || nN->getNodeNumber() == 0 ) cst -= vn;
    cst *= m_gain;
    if( qFabs( cst-m_constIn ) > 1e-9*( 1+qFabs( cst ) ) ){ m_constIn = cst; if( m_mode == LIN ) m_dirty = true; }

    if( sim->circTime() != m_flipTime ){ m_flipTime = sim->circTime(); m_flips = 0; m_changes = 0; m_held = false; }

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
        // Beyond a rail: go there, but not while the input still moves. Another op amp in the loop
        // may have changed region in the same iteration (composite amplifier: U1 goes LIN while U2
        // is still at its rail, so U1's loop is open for one solve); without this wait the two
        // swap regions in step forever.
        bool settled = m_linIter >= 2 && qFabs( vd-m_lastVd ) <= 1e-3*qFabs( vd )+1e-9;
        if( mode == LIN && ( settled || m_linIter >= 8 ) ){
            if     ( ideal > m_voltPos+eps ) mode = SAT_P;
            else if( ideal < m_voltNeg-eps ) mode = SAT_N;
        }
        m_linIter++;
    }
    else if( mode == SAT_P ){                  // Leaving a rail: always through a LIN solve, which
        if( ideal < m_voltPos-eps ) mode = LIN;  // finds the feedback (or its instability)
    }
    else{
        if( ideal > m_voltNeg+eps ) mode = LIN;
    }
    // A loop with no static solution in this time step (a relaxation through another part: an
    // SCR crowbar collapses the supply, the input swings, the SCR drops out, the supply comes
    // back...) made the op amp change region for ever. The solver then gave up after its
    // iteration budget (minutes of CPU) and took the step with the op amp in LIN far beyond its
    // rails (kV outputs; create-circuit #368). After MAX_CHANGES changes in one step, sit on the
    // rail the ideal output points to until the next step: the swing then plays out in time.
    static const int MAX_CHANGES = 12;
    if( m_held ) mode = m_mode;
    else if( mode != m_mode && ++m_changes > MAX_CHANGES ){
        mode = ( ideal >= 0.5*( m_voltPos+m_voltNeg ) ) ? SAT_P : SAT_N;
        m_held = true;
        OPAMP_LOG << "t" << sim->circTime() << m_changes << "region changes in one step: held at"
                  << ( mode == SAT_P ? "SAT+" : "SAT-" ) << "until the next step (ideal" << ideal << ")";
    }
    m_lastVd = vd;
    bool waiting = !m_held && ( mode == LIN ) && ( ideal > m_voltPos+eps || ideal < m_voltNeg-eps );
    if( mode == LIN && m_mode != LIN ){         // Leaving a rail: check the loop at the next solve
        m_linIter = 0;
        m_checkStable = true;
        m_satVd  = vd;
        m_satOut = out;
    }
    if( mode != m_mode ){ m_mode = mode; m_dirty = true; }
    else if( mode == SAT_P && qFabs( m_voltPos-m_stampVpos ) > m_accuracy ) m_dirty = true;
    else if( mode == SAT_N && qFabs( m_voltNeg-m_stampVneg ) > m_accuracy ) m_dirty = true;

    if( s_trace ) OPAMP_LOG << "t" << sim->circTime() << "vp" << vp << "vn" << vn << "out" << out
                            << "ideal" << ideal << "mode" << m_mode << "dirty" << m_dirty << "check" << m_checkStable;
    // A waiting op amp is only called again when one of its nodes changes. With nothing else
    // moving (an open-loop comparator), that never happens: a tiny output offset that flips sign
    // on every wait (1e-9 of the output) changes the output node, so the solver calls back.
    if( waiting ){ m_nudge = ( m_nudge > 0 ? -1e-9 : 1e-9 )*( 1+qFabs( ideal ) ); m_dirty = true; }
    else if( m_nudge != 0 ){ m_nudge = 0; if( m_mode == LIN ) m_dirty = true; }

    // Slew rate: the output may move at most SR*dt since the last solved step. A solve that went
    // further (a step input, a rail change) is replaced by a source ramping from there (SLEW)
    if( m_slewRate > 0 && m_lastValid && !waiting && !m_dirty ){
        // The inputs were constant until the previous solved step (else this op amp was solved
        // there too): the output may have moved since then, at most one reactive step (a
        // sampled waveform's step) after an idle gap
        uint64_t prev = qMax( m_lastT, sim->prevSolveTime() );
        uint64_t gap = sim->circTime() > prev ? sim->circTime()-prev : 0;
        double dt = qMin( gap, slewStep() )*1e-12;
        double maxDv = m_slewRate*dt+m_grace;
        double dv = out-m_lastOut;
        if( qFabs( dv ) > maxDv*1.001+1e-6 ){
            startSlew( m_lastOut+( dv > 0 ? maxDv : -maxDv ) );
            return;
        }
    }
    if( !m_dirty && !m_checkStable && !waiting ) return;   // Converged
    sim->notCorverged();
    if( m_dirty ) stampMode();
}

uint64_t OpAmp::slewStep()   // Steps of 0.2 % of the supply span, 1 ns to 10 us
{
    double span = qMax( 1.0, m_voltPos-m_voltNeg );
    double dt = 0.002*span/m_slewRate;
    if( dt < 1e-9 ) dt = 1e-9;
    if( dt > 1e-5 ) dt = 1e-5;
    return (uint64_t)( dt*1e12 );
}

void OpAmp::startSlew( double vs )
{
    Simulator* sim = Simulator::self();
    m_slewV = qBound( m_voltNeg, vs, m_voltPos );
    m_slewDir = 0;
    m_grace = 0;
    m_mode = SLEW;
    m_checkStable = false;
    m_nudge = 0;
    stampMode();
    sim->notCorverged();
    sim->cancelEvents( this );
    sim->addEvent( slewStep(), this );
}

// One slew step: move the source towards where the linear (or rail) solution wants the output.
// The target comes from the present inputs, which follow the output through any feedback.
void OpAmp::runEvent()
{
    if( m_mode != SLEW || m_slewRate <= 0 ) return;
    Simulator* sim = Simulator::self();
    uint64_t step = slewStep();
    double dv = m_slewRate*step*1e-12;
    double vd = m_inputP->getVoltage()-m_inputN->getVoltage()+m_vos;
    double target = qBound( m_voltNeg, m_gain*vd, m_voltPos );
    // With feedback the target moves with the output: the loop gain sets how far to go. The
    // linear solution is within reach when out + (target-out)/(1+loop) is within dv; the loop
    // is unknown here, so take the step and let the LIN solve judge: leave SLEW when the
    // remaining distance at this step is below dv.
    double dist = target-m_slewV;
    int dir = ( dist > 0 ) ? 1 : -1;
    // Done when the target is within this step (no feedback: a rail or the open-loop value), or
    // when the direction reverses: with feedback the target is the open-loop gain times the
    // error, which changes sign as the output crosses the linear solution
    if( qFabs( dist ) <= dv || ( m_slewDir != 0 && dir != m_slewDir ) ){
        double ideal = m_gain*vd;
        if     ( ideal >= m_voltPos ) m_mode = SAT_P;
        else if( ideal <= m_voltNeg ) m_mode = SAT_N;
        else{
            m_mode = LIN;
            m_linIter = 0;
            m_checkStable = true;
            m_satVd  = vd;
            m_satOut = m_slewV;
        }
        m_lastOut = m_slewV;                   // The LIN solve starts from here: within one step
        m_lastT = sim->circTime();
        m_grace = 2*dv;
        m_slewDir = 0;
        m_dirty = true;
        stampMode();
        return;
    }
    m_slewV += dir*dv;
    m_slewDir = dir;
    stampMode();
    sim->addEvent( step, this );
}

void OpAmp::stampBias()
{
    m_bias.getEpin( 0 )->stampCurrent(-( m_ib+m_ios/2 ) );   // Drawn from each input node
    m_bias.getEpin( 1 )->stampCurrent(-( m_ib-m_ios/2 ) );
}

void OpAmp::setSlewRate( double sr )   // V/µs
{
    if( sr < 0 ) sr = 0;
    m_slewRate = sr*1e6;
    if( Simulator::self()->isRunning() ) CircuitWidget::self()->powerCircOff();
}
void OpAmp::setBiasCurr( double ib )
{
    m_ib = ib;
    if( Simulator::self()->isRunning() ) CircuitWidget::self()->powerCircOff();
}
void OpAmp::setOffsetCurr( double ios )
{
    m_ios = ios;
    if( Simulator::self()->isRunning() ) CircuitWidget::self()->powerCircOff();
}
void OpAmp::setOffsetVolt( double vos )
{
    m_vos = vos;
    m_changed = true;   // A constant in the LIN current: restamped in updateStep()
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

void OpAmp::simState( StateIO& s )
{
    eElement::simState( s );
    pinCurrents( s );
    s.io( m_mode );
    s.io( m_flips );
    s.io( m_changes );
    s.io( m_held );
    s.io( m_flipTime );
    s.io( m_checkStable );
    s.io( m_satVd );
    s.io( m_satOut );
    s.io( m_stampI );
    s.io( m_stampVpos );
    s.io( m_stampVneg );
    s.io( m_constIn );
    s.io( m_dirty );
    s.io( m_linIter );
    s.io( m_lastVd );
    s.io( m_nudge );
    s.io( m_diag );
    s.io( m_powerPins );
    s.io( m_switchPins );
    s.io( m_accuracy );
    s.io( m_gain );
    s.io( m_voltPos );
    s.io( m_voltNeg );
    s.io( m_voltPosDef );
    s.io( m_voltNegDef );
    s.io( m_outImp );
    s.io( m_region );
    s.io( m_slewRate );
    s.io( m_ib );
    s.io( m_ios );
    s.io( m_vos );
    s.io( m_slewV );
    s.io( m_lastOut );
    s.io( m_lastT );
    s.io( m_lastValid );
    s.io( m_slewDir );
    s.io( m_grace );
    m_bias.simState( s );
}
