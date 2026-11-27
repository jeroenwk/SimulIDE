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
     , m_sup( id )                     // ePins "<id>-ePinN": counted in the supply Pins' current
{
    m_bias.setNumEpins( 2 );
    m_sup.setNumEpins( 8 );
    m_supDir = m_supFlips = 0;
    m_iMax = 0;
    m_ilimDir = 0;
    m_slewRate = m_ib = m_ios = m_vos = m_gbw = 0;
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

        new DoubProp<OpAmp>("I_Max", tr("Output Current Limit (0 = none)"),"mA"
                           , this, &OpAmp::iMax, &OpAmp::setIMax ),
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
        new DoubProp<OpAmp>("GBW", tr("Gain-Bandwidth (0 = flat)"),"Hz"
                           , this, &OpAmp::gbw, &OpAmp::setGbw ),
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
    m_gbwPrev = 0;
    m_gbwT = 0;
    m_stampT = 0;
    m_supDir = 0;
    m_supFlips = 0;
    m_ilimDir = 0;
    m_impNow = m_outImp;
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
    if( m_gbw > 0 ) Simulator::self()->addEvent( Simulator::self()->reactStep(), this ); // Pole steps

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

    // #506: the output current comes from a supply pin, the V+ one when sourcing and the V- one
    // when sinking. That pin's node gets the output's own terms with the opposite sign, so the
    // current drawn there is the current delivered (see stampSupply)
    for( int i=0; i<8; ++i ) m_sup.getEpin( i )->setEnode( nullptr );
    if( m_powerPins ){
        eNode* cols[3] = { oN, pN, nN };
        for( int k=0; k<2; ++k ){
            eNode* sN = m_pin[3+k]->getEnode();
            if( !sN || sN->getNodeNumber() == 0 ) continue;   // Ground: nothing to stamp
            for( int j=0; j<3; ++j ){
                ePin* e = m_sup.getEpin( 4*k+j );
                e->setEnode( sN );
                m_supDiag[k][j] = false;
                eNode* cn = cols[j];
                if( !cn || cn->getNodeNumber() == 0 ) continue;   // A constant (in m_constIn)
                if( cn == sN ){ e->setEnodeComp( &s_gnd ); m_supDiag[k][j] = true; }
                else e->addSingAdm( cn->getNodeNumber(), 0 );
            }
            m_sup.getEpin( 4*k )->createCurrent();
            if( oN->getNodeNumber() != 0 && oN != sN ){         // Zero entry back: same solver group
                ePin* b = m_sup.getEpin( 4*k+3 );
                b->setEnode( oN );
                b->addSingAdm( sN->getNodeNumber(), 0 );
            }
        }
    }
    m_dirty = true;
}

void OpAmp::stampSupply( double G, double gG )
{
    // Current delivered by the output: I + gG*(V+ - V-) - G*Vout (I = the stamped constant part).
    // Drawn from the supply node: in its equation -s*V_k - i, so s = +G at Vout, -gG at V+,
    // +gG at V-, i = -I
    for( int k=0; k<2; ++k ){
        ePin* e0 = m_sup.getEpin( 4*k );
        if( !e0->getEnode() ) continue;
        bool on = ( m_supDir == k+1 );
        double s[3] = { on ? G : 0, on ? -gG : 0, on ? gG : 0 };
        for( int j=0; j<3; ++j ){
            ePin* e = m_sup.getEpin( 4*k+j );
            if( m_supDiag[k][j] ) e->stampAdmitance(-s[j] );
            else                  e->stampSingAdm( s[j] );
        }
        e0->stampCurrent( on ? -m_stampI : 0 );
    }
}

void OpAmp::updateStep()
{
    if( !m_changed ) return;
    m_changed = false;

    m_output->setImpedance( m_outImp );
    m_impNow = m_outImp;
    m_dirty = true;
    voltChanged();
}

void OpAmp::stampMode()
{
    // GBW: a single pole at GBW/Gain, integrated implicitly over dt since the last settled step,
    // out = a*out(prev) + (1-a)*gain*vd with a = e^(-wp*dt): the same stamp with gain (1-a)*gain
    // and a constant a*out(prev), so feedback loops through it stay in one solve
    double a  = ( m_mode == LIN ) ? gbwAlpha() : 0;
    // ILIM (#431): a current source of I_Max, its output impedance 1 GOhm (not 0: a matrix row)
    double imp = ( m_mode == ILIM ) ? 1e9 : m_outImp;
    if( imp != m_impNow ){ m_output->setImpedance( imp ); m_impNow = imp; }
    double G  = 1/imp;
    double gG = ( m_mode == LIN ) ? m_gain*( 1-a )*G : 0;
    for( int i : { aOP, aON } )
    {
        ePin* p = m_aux.getEpin( i );
        if( !p->getEnode() ) continue;
        double s = ( i == aOP ) ? gG : -gG;               // +gain*G*V+, -gain*G*V-
        if( m_diag[i] ) p->stampAdmitance(-s );   // Same node: -s on the diagonal
        else                    p->stampSingAdm( s );
    }
    double I = ( m_mode == LIN  ) ? G*( ( 1-a )*m_constIn+a*m_gbwPrev+m_nudge )
             : ( m_mode == SAT_P) ? G*m_voltPos
             : ( m_mode == SAT_N) ? G*m_voltNeg
             : ( m_mode == ILIM ) ? m_ilimDir*m_iMax : G*m_slewV;
    m_output->stampCurrent( I );
    m_stampI = I;
    if( m_powerPins ) stampSupply( G, gG );
    m_stampVpos = m_voltPos;
    m_stampVneg = m_voltNeg;
    m_stampT = Simulator::self()->circTime();
    m_dirty = false;
}

void OpAmp::stepSettled()
{
    double a = gbwAlpha();
    double vdS = m_inputP->getVoltage()-m_inputN->getVoltage()+m_vos;
    double ideal = a*m_gbwPrev+( 1-a )*vdS*m_gain;   // With GBW, where the pole takes the output now
    if( m_output->getEnode() ){   // Into the part: G*(Vout - source), the source is in the aux pins too
        double src = ( m_mode == LIN ) ? ideal
                   : ( m_mode == SAT_P ) ? m_voltPos : ( m_mode == SAT_N ) ? m_voltNeg : m_slewV;
        if( m_mode == ILIM ) m_output->setTrueCurrent(-( m_stampI-m_output->getVoltage()/m_impNow ) );
        else                 m_output->setTrueCurrent( ( m_output->getVoltage()-src )/m_outImp );
        m_lastOut = m_output->getVoltage();
        m_lastT = Simulator::self()->circTime();
        m_lastValid = true;
        if( m_mode != SLEW ) m_grace = 0;
        m_gbwPrev = m_lastOut;
        m_gbwT = m_lastT;
    }
    double h = 0.01;
    int r = m_region;
    if( r == 1 ){ if( ideal < m_voltPos-h ) r = 0; }
    else if( r == 2 ){ if( ideal > m_voltNeg+h ) r = 0; }
    if( r <= 0 ){
        if     ( ideal >= m_voltPos+h ) r = 1;   // Also at the first step: inputs at 0 V sit
        else if( ideal <= m_voltNeg-h ) r = 2;   // exactly on a 0 V rail, that is not SAT-
        else r = 0;
    }
    if( m_mode == ILIM ) r = 3;
    else if( r == 3 ) r = 0;
    if( r == m_region ) return;
    m_region = r;
    if     ( r == 3 ) reportState("I LIMIT", stWait );
    else if( r == 0 ) reportState("LINEAR", stOn );
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
    double a = gbwAlpha();
    double ideal = a*m_gbwPrev+( 1-a )*vd*m_gain;   // a = 0 without GBW
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
    if( m_gbw > 0 && m_mode == LIN && sim->circTime() != m_stampT ) m_dirty = true; // New dt and out(prev)

    if( sim->circTime() != m_flipTime ){ m_flipTime = sim->circTime(); m_flips = 0; m_changes = 0; m_held = false; m_supFlips = 0; }

    double eps = 1e-6*( 1+qFabs( m_voltPos )+qFabs( m_voltNeg ) );
    int mode = m_mode;
    if( mode == ILIM )                         // Out of the limit when the output it wants is back
    {                                          // on this side of the output (or no limit any more)
        double want = qBound( m_voltNeg, ideal, m_voltPos );
        if( m_iMax <= 0 || m_ilimDir*( want-out ) <= 0 )
            mode = ( ideal >= m_voltPos ) ? SAT_P : ( ideal <= m_voltNeg ) ? SAT_N : LIN;
        else if( m_ilimDir > 0 && out > m_voltPos+eps ) mode = SAT_P;   // The load lifts it past a rail
        else if( m_ilimDir < 0 && out < m_voltNeg-eps ) mode = SAT_N;
    }
    else if( mode == LIN )
    {
        if( m_checkStable )                    // First LIN solve after a rail
        {
            m_checkStable = false;
            double dOut = out-m_satOut;
            if( qFabs( dOut ) > 1e-12 && m_gain*( 1-a )*( vd-m_satVd )/dOut > 1 && m_flips < 4 )
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
    // Output current limit (#431): from a solve in this region, beyond I_Max: a current source
    if( m_iMax > 0 && mode == m_mode && mode != ILIM && !m_dirty ){
        double io = loadCurrent( ideal );
        if( s_trace ) OPAMP_LOG << "ilim check: io" << io << "mode" << mode;
        if( qFabs( io ) > m_iMax*1.001 ){ mode = ILIM; m_ilimDir = ( io > 0 ) ? 1 : -1; }
    }
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
    if( m_slewRate > 0 && m_lastValid && !waiting && !m_dirty && m_mode != ILIM ){
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
    // #506: which supply pin the output current goes through, from this solve's current. Only a
    // clear sign changes it
    if( m_powerPins && !m_dirty ){
        double io = loadCurrent( ideal );
        double floor = 1e-9*( 1+qFabs( io ) );
        int dir = ( io > floor ) ? 1 : ( io < -floor ) ? 2 : m_supDir;
        if( dir != m_supDir && m_supFlips < 6 ){
            m_supFlips++;
            m_supDir = dir;
            m_dirty = true;
        }
    }
    if( !m_dirty && !m_checkStable && !waiting ) return;   // Converged
    sim->notCorverged();
    if( m_dirty ) stampMode();
}

// What the output delivers: the current the rest of its node draws. Not (Vsrc - Vout)/Out_Imped:
// at 1 nOhm the rounding of the node voltages (µV) made that kiloamps. On ground (no node
// equation) from the source, exact there since Vout is 0
double OpAmp::loadCurrent( double ideal )
{
    eNode* oN = m_output->getEnode();
    if( !oN ) return 0;
    if( oN->getNodeNumber() != 0 ) return oN->currentExcept( m_elmId+"-" );
    if( m_mode == ILIM ) return m_stampI;
    double src = ( m_mode == LIN ) ? ideal
               : ( m_mode == SAT_P ) ? m_voltPos : ( m_mode == SAT_N ) ? m_voltNeg : m_slewV;
    return src/m_outImp;
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
    Simulator* sim = Simulator::self();
    if( m_mode != SLEW ){                      // GBW: move the pole one reactive step
        if( m_gbw <= 0 ) return;
        if( m_mode == LIN ) stampMode();
        sim->addEvent( sim->reactStep(), this );
        return;
    }
    if( m_slewRate <= 0 ) return;
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
        if( m_gbw > 0 ) sim->addEvent( sim->reactStep(), this );   // Back to the pole steps
        return;
    }
    m_slewV += dir*dv;
    m_slewDir = dir;
    stampMode();
    sim->addEvent( step, this );
}

double OpAmp::gbwAlpha()
{
    if( m_gbw <= 0 ) return 0;
    uint64_t now = Simulator::self()->circTime();
    double dt = ( now > m_gbwT ) ? ( now-m_gbwT )*1e-12 : 0;
    return qExp(-2*M_PI*m_gbw/m_gain*dt );
}

void OpAmp::setGbw( double gbw )   // Hz
{
    if( gbw < 0 ) gbw = 0;
    m_gbw = gbw;
    if( Simulator::self()->isRunning() ) CircuitWidget::self()->powerCircOff();
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
    s.io( m_gbw );
    s.io( m_gbwPrev );
    s.io( m_gbwT );
    s.io( m_stampT );
    m_bias.simState( s );
}
