/***************************************************************************
 *   Copyright (C) 2018 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/
// Based on Falstad Circuit Simulator BJT model: https://falstad.com

#include <QtMath>
#include <QDebug>
#include <QDomDocument>
#include <limits>

#include "e-bjt.h"
#include "e-pin.h"
#include "e-node.h"
#include "simulator.h"
#include "mainwindow.h"
#include "utils.h"
#include "noise.h"

#define COLL m_ePin[0]
#define EMIT m_ePin[1]
#define BASE m_ePin[2]

QHash<QString, bjtData_t> eBJT::m_bjts;
QStringList eBJT::m_names;

eBJT::eBJT( QString id )
    : eElement( id )
    , m_BEjunction( id+"BEjunct")
    , m_BCjunction( id+"BCjunct")
    , m_cje( id+"-Cje")
    , m_cjc( id+"-Cjc")
{
    m_cje.setPins( [this](){ return m_PNP ? std::make_pair( EMIT, BASE ) : std::make_pair( BASE, EMIT ); } );
    m_cjc.setPins( [this](){ return m_PNP ? std::make_pair( COLL, BASE ) : std::make_pair( BASE, COLL ); } );
    m_cje.setNoiseDefault( 4.5e-12 );
    m_cjc.setNoiseDefault( 3.6e-12 );

    m_ePin.resize(3);

    m_model = "Custom";      // Circuits saved before models existed load as Custom, unchanged
    m_applying = false;
    m_PNP = false;
    m_baseCurr = 0;

    m_gain = 100;
    m_rgain = .5;
    m_fgain = m_gain/(m_gain+1);

    m_vt = 0.025865;
    m_satCur = 1e-13;
    m_vCrit = m_vt*qLn( m_vt/(qSqrt(2)*m_satCur) );
    m_va = 0;

    m_gmin   = 0;
    m_gminDropped = false;
    m_bvceo  = 0;
    m_voltCE = 0;
    m_bdCurr = 0;
    setBvceo( 0 );
    m_voltEB = 0;
    m_ebCurr = 0;
    setBvebo( 0 );

    m_BEjunction.setNumEpins( 2 );
    m_BE = m_BEjunction.getEpin( 0 );
    m_EB = m_BEjunction.getEpin( 1 );

    m_BCjunction.setNumEpins( 2 );
    m_BC = m_BCjunction.getEpin( 0 );
    m_CB = m_BCjunction.getEpin( 1 );

}
eBJT::~eBJT(){}

void eBJT::initialize()
{
    m_changed = true;
    m_step = 0;
    m_voltBE = 0;
    m_voltBC = 0;
    m_baseCurr = 0;
    m_gmin = 0;
    m_gminDropped = false;
    m_voltCE = 0;
    m_bdCurr = 0;
    m_voltEB = 0;
    m_ebCurr = 0;
    BASE->setTrueCurrent( std::numeric_limits<double>::quiet_NaN() );
    COLL->setTrueCurrent( std::numeric_limits<double>::quiet_NaN() );
    EMIT->setTrueCurrent( std::numeric_limits<double>::quiet_NaN() );
}

void eBJT::stamp()
{
    m_EC = EMIT;
    m_CE = COLL;

    eNode* collNod = COLL->getEnode();// Collector
    eNode* emitNod = EMIT->getEnode();// Emitter
    eNode* baseNod = BASE->getEnode();// Base

    if( collNod ) collNod->addToNoLinList( this );
    if( emitNod ) emitNod->addToNoLinList( this );
    if( baseNod ) baseNod->addToNoLinList( this );

    m_BE->setEnode( baseNod );
    m_BE->setEnodeComp( emitNod );
    m_EB->setEnode( emitNod );
    m_EB->setEnodeComp( baseNod );

    m_BC->setEnode( baseNod );
    m_BC->setEnodeComp( collNod );
    m_CB->setEnode( collNod );
    m_CB->setEnodeComp( baseNod );

    m_CE->setEnodeComp( emitNod );
    m_EC->setEnodeComp( collNod );

    BASE->createCurrent();
    COLL->createCurrent();
    EMIT->createCurrent();
}

void eBJT::voltChanged()
{
    double pnp = m_PNP ? -1 : 1;
    double voltC = COLL->getVoltage();
    double voltE = EMIT->getVoltage();
    double voltB = BASE->getVoltage();
    double voltBC = voltB-voltC;
    double voltBE = voltB-voltE;

    // Converged when the junctions moved less than 1 mV since the stamp: the node voltages
    // were solved with the model linearized at the stamp point, which is off by about
    // (dV/Vt)^2/2 in current: 0.07 % at 1 mV, but 7.5 % at the 10 mV used before, so
    // the circuit settled on wrong currents and the exact readouts broke KCL (BH-057)
    if( m_changed ) m_changed = false;
    else if( qFabs(voltBC-m_voltBC) < .001
          && qFabs(voltBE-m_voltBE) < .001
          && ( m_bvceo == 0 || qFabs( pnp*(voltC-voltE)-m_voltCE ) < .001 )
          && ( m_bvebo == 0 || qFabs( pnp*(voltE-voltB)-m_voltEB ) < .001 ) )
    {
        // Converged within 1 mV: the stamps stay, but the reported terminal currents
        // must come from the converged voltages, not from the last iteration (BH-034:
        // up to 47 % off, KCL violated in a current mirror).
        // (no qDebug here: this is the per-step hot path)
        double ie2, ic2;
        if( m_va <= 0 ){             // No Early effect: the original expressions, inline (hot path)
            double pc = pnp/m_vt;
            double eBE = qExp( voltBE*pc );
            double eBC = qExp( voltBC*pc );
            ie2 = pnp*m_satCur*(-(eBE-1)/m_fgain + (eBC-1) );
            ic2 = pnp*m_satCur*( (eBE-1) - (eBC-1)/m_rgain );
        }
        else currents( voltBE, voltBC, &ic2, &ie2 );
        double ib2 = 0;
        if( m_bvceo > 0 ) ib2 = pnp*breakdown( pnp*(voltC-voltE), nullptr );
        m_bdCurr = ib2;
        double ie3 = 0;
        if( m_bvebo > 0 ) ie3 = pnp*breakdown( pnp*(voltE-voltB), m_ebOfset, nullptr );
        m_ebCurr = ie3;
        BASE->setTrueCurrent( -(ie2+ic2) - ie3 );
        COLL->setTrueCurrent( ic2+ib2 );
        EMIT->setTrueCurrent( ie2-ib2 + ie3 );
        m_step = 0;
        // Converged with a large gmin still in the stamps (a long iteration at a switching
        // edge): restamp once without it, or it stays across the junctions (up to 0.1 S)
        // until the voltages move again, and a latch can sit in a false state for good
        if( m_gmin > 1e-9 && !m_gminDropped ){ m_gminDropped = true; m_step = -0.1; }
        else { m_gminDropped = false; return; }
    }
    Simulator::self()->notCorverged();

    m_step += .1;
    double gmin = m_satCur*1e-2*qExp( m_step );
    if( gmin > .1 ) gmin = .1;
    m_gmin = gmin;

    voltBC = pnp*limitStep( pnp*voltBC, pnp*m_voltBC );
    m_voltBC = voltBC;
    voltBE = pnp*limitStep( pnp*voltBE, pnp*m_voltBE );
    m_voltBE = voltBE;

    double ie, ic, Gce, Gcc, Gee, Gec;   // dIc/dVbe, dIc/dVbc, dIe/dVbe, dIe/dVbc
    if( m_va <= 0 ){                     // No Early effect: the original expressions, inline (hot path)
        double pcoef = pnp/m_vt;
        double expBC = qExp( voltBC*pcoef );
        double expBE = qExp( voltBE*pcoef );
        ie = pnp*m_satCur*(-(expBE-1)/m_fgain + (expBC-1) );
        ic = pnp*m_satCur*( (expBE-1) - (expBC-1)/m_rgain );
        Gee = -m_satCur/m_vt*expBE/m_fgain;
        Gcc = -m_satCur/m_vt*expBC/m_rgain;
        Gce = -Gee*m_fgain;
        Gec = -Gcc*m_rgain;
    }else{
        double g[4];
        currents( voltBE, voltBC, &ic, &ie, g );
        Gce = g[0]; Gcc = g[1]; Gee = g[2]; Gec = g[3];
    }
    m_baseCurr = -(ie+ic);

    Gcc -= gmin;
    Gee -= gmin;

    // Breakdown C-E: a conductance Gbd between C and E plus its Norton current
    double Gbd = 0, vce0 = 0;
    m_bdCurr = 0;
    if( m_bvceo > 0 )
    {
        double vce = limitBreak( pnp*(voltC-voltE), m_voltCE, m_bdOfset );
        m_voltCE = vce;
        m_bdCurr = pnp*breakdown( vce, &Gbd );
        vce0 = pnp*vce;                                    // Back to VC-VE
    }
    // Breakdown E-B: a conductance Geb between E and B plus its Norton current
    double Geb = 0, veb0 = 0;
    m_ebCurr = 0;
    if( m_bvebo > 0 )
    {
        double veb = limitBreak( pnp*(voltE-voltB), m_voltEB, m_ebOfset );
        m_voltEB = veb;
        m_ebCurr = pnp*breakdown( veb, m_ebOfset, &Geb );
        veb0 = pnp*veb;                                    // Back to VE-VB
    }

    // True terminal currents (into the device): the matrix stamps below are a
    // linearization whose terms nearly cancel, so they can't be used to recover
    // the currents (CurrentFlow, ePin::getCurrent)
    BASE->setTrueCurrent( m_baseCurr-m_ebCurr );
    COLL->setTrueCurrent( ic+m_bdCurr );
    EMIT->setTrueCurrent( ie-m_bdCurr+m_ebCurr );

    // Admitance Matrix OK
    m_BC->stampAdmitance(-Gec-Gcc );
    m_CB->stampAdmitance(-Gce-Gcc );
    m_BE->stampAdmitance(-Gee-Gce+Geb );
    m_EB->stampAdmitance(-Gee-Gec+Geb );
    m_CE->stampAdmitance( Gce+Gbd );
    m_EC->stampAdmitance( Gec+Gbd );

    double bdStamp = -m_bdCurr + Gbd*vce0;
    double ebStamp = -m_ebCurr + Geb*veb0;
    BASE->stampCurrent(-m_baseCurr - (Gec+Gcc)*voltBC - (Gee+Gce)*voltBE - ebStamp );
    COLL->stampCurrent(-ic + Gce*voltBE + Gcc*voltBC + bdStamp );
    EMIT->stampCurrent(-ie + Gee*voltBE + Gec*voltBC - bdStamp + ebStamp );
}

void eBJT::noiseSources( QList<NoiseSrc>& list )   // Shot noise of the collector and base currents
{
    if( !COLL || !EMIT || !BASE ) return;
    NoiseSrc c;
    c.a = COLL; c.b = EMIT;
    c.kind = NoiseSrc::Shot;
    c.psd = [this](){ double ic = COLL->trueCurrent(); return qIsNaN( ic ) ? 0 : Noise::twoQ*qFabs( ic ); };
    list.append( c );
    NoiseSrc b;
    b.a = BASE; b.b = EMIT;
    b.kind = NoiseSrc::Shot;
    b.psd = [this](){ return Noise::twoQ*qFabs( m_baseCurr ); };
    list.append( b );
}

// Ebers-Moll in transport form: Ict = IS (eBE - eBC) K, Ic = Ict - IS (eBC-1)/BR,
// Ie = -Ict - IS (eBE-1)/BF, with K = 1 - VBC/VA the Early factor (1 with no VA).
// Called only with VA set: with no VA, voltChanged() runs the original Falstad expressions
// inline, so existing circuits keep their results to the last bit and their speed.
void eBJT::currents( double vbe, double vbc, double* ic, double* ie, double* g )
{
    double pnp = m_PNP ? -1 : 1;
    double eBE = qExp( vbe*pnp/m_vt );
    double eBC = qExp( vbc*pnp/m_vt );
    double cr = 1/m_rgain-1;             // 1/BR
    double cf = 1/m_fgain-1;             // 1/BF
    double K = 1-pnp*vbc/m_va;           // dK: dK/d(pnp-signed VBC)
    double dK = -1/m_va;
    if( K < 0.05 ){ K = 0.05; dK = 0; }  // VBC forward past VA: not a real bias, keep it sane
    double ict = m_satCur*(eBE-eBC);
    *ic = pnp*( ict*K - m_satCur*(eBC-1)*cr );
    *ie = pnp*(-ict*K - m_satCur*(eBE-1)*cf );
    if( !g ) return;
    double sBE = m_satCur*eBE/m_vt;
    double sBC = m_satCur*eBC/m_vt;
    g[0] = sBE*K;                        // dIc/dVbe
    g[1] = -sBC*K + ict*dK - sBC*cr;     // dIc/dVbc
    g[2] = -sBE*K - sBE*cf;              // dIe/dVbe
    g[3] = sBC*K - ict*dK;               // dIe/dVbc
}

double eBJT::limitStep( double vnew, double vold )
{
    if( vnew > m_vCrit && qFabs(vnew-vold) > (2*m_vt) ){
        if( vold > 0 ){
            double arg = 1+(vnew-vold)/m_vt;
            if( arg > 0 )  vnew = vold + m_vt*qLn( arg );
            else           vnew = m_vCrit;
        }
        else vnew = m_vt*qLn( vnew/m_vt );
    }
    return vnew;
}

void eBJT::setGain( double gain )
{
    // Gain <= 0 made m_fgain 0 or infinite and every node voltage NaN (BH-026)
    if( gain < 0.01 ){
        qDebug() << "[eBJT] setGain: Gain" << gain << "is not positive, using 0.01";
        gain = 0.01;
    }
    if( differs( gain, m_gain ) ) toCustom("Gain");
    m_gain = gain;
    m_fgain = m_gain/(m_gain+1);
    m_changed = true;
}

void eBJT::setThreshold( double vCrit )
{
    if( qAbs( vCrit-m_vCrit ) > 1e-5 ) toCustom("Vcrit");   // 10 uV: IS within 0.04 %
    m_vCrit = vCrit;
    m_satCur = m_vt/(qExp( vCrit/m_vt )*qSqrt(2));
    setBvceo( m_bvceo );   // The breakdown knees depend on m_satCur
    setBvebo( m_bvebo );
    m_changed = true;
}

void eBJT::setSatCur( double is )
{
    if( is < 1e-20 ) is = 1e-20;
    if( is > 1e-6 )  is = 1e-6;
    if( differs( is, m_satCur ) ) toCustom("IS");
    m_satCur = is;
    m_vCrit = m_vt*qLn( m_vt/(qSqrt(2)*m_satCur) );
    setBvceo( m_bvceo );   // The breakdown knees depend on m_satCur
    setBvebo( m_bvebo );
    m_changed = true;
}

void eBJT::setEarly( double va )
{
    if( va < 0 ) va = -va;
    if( differs( va, m_va ) ) toCustom("VA");
    m_va = va;
    m_changed = true;
}

// Breakdown current (pnp-signed voltage in, NPN sense out: into the collector for C-E,
// into the emitter for E-B) and its conductance: m_satCur*exp((v-ofset)/vt), 5 mA at
// v = BV, as SimulIDE's zener.
double eBJT::breakdown( double v, double ofset, double* g )
{
    double e = qExp( qMin( (v-ofset)/m_vt, 80.0 ) );
    if( g ) *g = m_satCur*e/m_vt;
    return m_satCur*e;
}

void eBJT::setBvceo( double bv )
{
    if( bv < 0 ) bv = -bv;
    m_bvceo   = bv;
    m_bdOfset = bv - m_vt*qLn( 0.005/m_satCur );
    m_bdCrit  = m_vt*qLn( m_vt/(qSqrt(2)*m_satCur) );
    m_changed = true;
}

void eBJT::setBvebo( double bv )
{
    if( bv < 0 ) bv = -bv;
    m_bvebo   = bv;
    m_ebOfset = bv - m_vt*qLn( 0.005/m_satCur );
    m_changed = true;
}

// Newton step limit on a breakdown voltage (pnp-signed), the same damping as a zener's
double eBJT::limitBreak( double vnew, double vold, double ofset )
{
    double v  = vnew - ofset;
    double vo = vold - ofset;
    if( v > m_bdCrit && qFabs( v-vo ) > 2*m_vt )
    {
        if( vo > 0 ){
            double arg = 1+(v-vo)/m_vt;
            v = ( arg > 0 ) ? vo + m_vt*qLn( arg ) : m_bdCrit;
        }
        else v = m_vt*qLn( v/m_vt );
    }
    return v + ofset;
}

void eBJT::toCustom( const char* prop )
{
    if( m_applying || m_model == "Custom" ) return;
    qDebug() << "[eBJT]" << m_elmId << prop << "edited: model" << m_model << "-> Custom";
    m_model = "Custom";
}

void eBJT::setModel( QString model )
{
    if( !m_bjts.contains( model ) ){             // Custom (or a name this build doesn't have): keep the values
        if( model != "Custom" )
            qWarning() << "[eBJT]" << m_elmId << "unknown model" << model << ": kept as Custom";
        m_model = "Custom";
        return;
    }
    bjtData_t d = m_bjts.value( model );
    qDebug() << "[eBJT]" << m_elmId << "model" << model << ( d.pnp ? "PNP" : "NPN" ) << "gain" << d.gain
             << "IS" << d.satCur << "VA" << d.va << "Cje" << d.cje << "pF Cjc" << d.cjc << "pF";
    m_applying = true;
    setPnp( d.pnp );          // Virtual: the BJT component redraws its symbol
    setGain( d.gain );
    setSatCur( d.satCur );
    setEarly( d.va );
    setCje( d.cje*1e-12 ); setVje( d.vje ); setMje( d.mje );
    setCjc( d.cjc*1e-12 ); setVjc( d.vjc ); setMjc( d.mjc );
    m_applying = false;
    m_model = model;
    m_changed = true;
}

QStringList eBJT::modelNames() { return m_names; }

void eBJT::getModels() // Static: resources/data/bjts.model
{
    m_bjts.clear();
    m_names.clear();

    QString modelsFile = MainWindow::self()->getDataFilePath( "bjts.model" );
    QDomDocument domDoc = fileToDomDoc( modelsFile, "eBJT::getModels" );
    QDomNode node = domDoc.documentElement().firstChild();
    while( !node.isNull() )
    {
        QDomElement itemset = node.toElement();
        if( itemset.tagName() == "itemset" )
        {
            bool pnp = itemset.attribute("type").toUpper() == "PNP";
            QDomNode nodei = itemset.firstChild();
            while( !nodei.isNull() )
            {
                QDomElement item = nodei.toElement();
                if( item.tagName() == "item" )
                {
                    bjtData_t d;
                    d.pnp    = pnp;
                    d.gain   = item.attribute("gain").toDouble();
                    d.satCur = item.attribute("is").toDouble();
                    d.va     = item.attribute("va").toDouble();
                    d.cje = item.attribute("cje").toDouble();
                    d.vje = item.hasAttribute("vje") ? item.attribute("vje").toDouble() : 0.75;
                    d.mje = item.hasAttribute("mje") ? item.attribute("mje").toDouble() : 0.33;
                    d.cjc = item.attribute("cjc").toDouble();
                    d.vjc = item.hasAttribute("vjc") ? item.attribute("vjc").toDouble() : 0.75;
                    d.mjc = item.hasAttribute("mjc") ? item.attribute("mjc").toDouble() : 0.33;
                    QString name = item.attribute("name");
                    if( d.gain > 0 && d.satCur > 0 && !name.isEmpty() ){
                        m_bjts.insert( name, d );
                        m_names.append( name );
                    }
                    else qWarning() << "[eBJT] bjts.model: item" << name << "skipped (needs name, gain > 0, is > 0)";
                }
                nodei = nodei.nextSibling();
        }   }
        node = node.nextSibling();
    }
    m_names.append( "Custom" );
    qDebug() << "[eBJT] getModels:" << m_bjts.size() << "models from" << modelsFile;
}

void eBJT::simState( StateIO& s )
{
    eElement::simState( s );
    pinCurrents( s );
    s.io( m_applying );
    s.io( m_va );
    s.io( m_baseCurr );
    s.io( m_voltBE );
    s.io( m_voltBC );
    s.io( m_gain );
    s.io( m_vt );
    s.io( m_satCur );
    s.io( m_vCrit );
    s.io( m_rgain );
    s.io( m_fgain );
    s.io( m_PNP );
    s.io( m_gmin );
    s.io( m_gminDropped );
    s.io( m_bvceo );
    s.io( m_voltCE );
    s.io( m_bdOfset );
    s.io( m_bdCrit );
    s.io( m_bdCurr );
    s.io( m_bvebo );
    s.io( m_voltEB );
    s.io( m_ebOfset );
    s.io( m_ebCurr );
    m_cje.simState( s );
    m_cjc.simState( s );
    m_BEjunction.simState( s );
    m_BCjunction.simState( s );
}
