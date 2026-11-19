/***************************************************************************
 *   Copyright (C) 2026 by Jeroen de Zwart                                 *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QPainter>
#include <QMenu>
#include <QtMath>
#include <algorithm>
#include <cmath>

#include "fuse.h"
#include "itemlibrary.h"
#include "circuitwidget.h"
#include "statusview.h"
#include "eventlog.h"
#include "simulator.h"
#include "e-pin.h"
#include "pin.h"

#include "doubleprop.h"
#include "boolprop.h"
#include "stringprop.h"

#define tr(str) simulideTr("Fuse",str)

// Rated current (A), typical voltage drop at In (V), typical melting I²t at 10 In (A²s)
// [DOC] Schurter FSF 5x20 (quick-acting F) and FST 5x20 (time-lag T) datasheets, every rating
struct FuseRow { double in, vdrop, i2t; };

static const FuseRow fsfTable[] = {
    { 0.032, 1.500, 0.000082 }, { 0.04, 1.100, 0.000128 }, { 0.05, 3.000, 0.00025 },
    { 0.063, 0.940, 0.000278 }, { 0.08, 0.750, 0.00102  }, { 0.1,  0.840, 0.0014  },
    { 0.125, 0.610, 0.00484  }, { 0.16, 0.550, 0.0113   }, { 0.2,  0.540, 0.0252  },
    { 0.25,  0.240, 0.0159   }, { 0.315,0.210, 0.0367   }, { 0.4,  0.200, 0.085   },
    { 0.5,   0.150, 0.151    }, { 0.63, 0.140, 0.303    }, { 0.8,  0.110, 0.508   },
    { 1,     0.110, 1.13     }, { 1.25, 0.100, 1.81     }, { 1.6,  0.100, 2.94    },
    { 2,     0.090, 5.28     }, { 2.5,  0.090, 9.19     }, { 3.15, 0.090, 16.1    },
    { 4,     0.090, 25.6     }, { 5,    0.080, 33.8     }, { 6.3,  0.080, 53.2    },
    { 8,     0.080, 93.4     }, { 10,   0.070, 183      } };

static const FuseRow fstTable[] = {
    { 0.05,  0.950, 0.0363 }, { 0.063, 1.300, 0.0401 }, { 0.08,  1.100, 0.057  },
    { 0.1,   0.565, 0.107  }, { 0.125, 0.400, 0.064  }, { 0.16,  0.415, 0.23   },
    { 0.2,   0.270, 0.256  }, { 0.25,  0.210, 0.238  }, { 0.315, 0.170, 0.544  },
    { 0.4,   0.150, 0.768  }, { 0.5,   0.160, 3      }, { 0.63,  0.160, 4.35   },
    { 0.8,   0.120, 3.85   }, { 1,     0.060, 3.3    }, { 1.25,  0.060, 5.5    },
    { 1.6,   0.060, 10.5   }, { 2,     0.060, 16     }, { 2.5,   0.060, 21.9   },
    { 3.15,  0.060, 47     }, { 4,     0.060, 68.3   }, { 5,     0.060, 102    },
    { 6.3,   0.060, 190    }, { 8,     0.060, 275    }, { 10,    0.060, 520    },
    { 12.5,  0.060, 750    }, { 16,    0.060, 1638   }, { 20,    0.060, 3057   } };

// Log-log interpolation in the rating; outside the table, the value per In² (I²t) or the
// value (voltage drop) of the nearest end
static double interp( const FuseRow* t, int n, double in, bool i2t )
{
    auto val = [&]( int k ){ return i2t ? t[k].i2t : t[k].vdrop; };
    if( in <= t[0].in   ) return i2t ? val(0)*in*in/( t[0].in*t[0].in ) : val(0);
    if( in >= t[n-1].in ) return i2t ? val(n-1)*in*in/( t[n-1].in*t[n-1].in ) : val(n-1);
    int k = 0;
    while( t[k+1].in < in ) k++;
    double f = std::log( in/t[k].in )/std::log( t[k+1].in/t[k].in );
    return std::exp( std::log( val(k) ) + f*( std::log( val(k+1) )-std::log( val(k) ) ) );
}

// Heat-model presets per speed class [INFERRED specs/fuse/fuse_fit.py: two stages fitted to
// the 1 A pre-arcing limits and melting I²t of the datasheets above]
//                         C1/I²t   I_mf/In  G2/G1    τ2 (s)
static const double presetF[4] = { 0.9284, 1.600, 0.6874, 1735.5 };
static const double presetT[4] = { 1.0362, 2.000, 0.9706,   22.0 };

Component* Fuse::construct( QString type, QString id )
{ return new Fuse( type, id ); }

LibraryItem* Fuse::libraryItem()
{
    return new LibraryItem(
        tr("Fuse"),
        "Switches",
        "fuse.png",
        "Fuse",
        Fuse::construct );
}

Fuse::Fuse( QString type, QString id )
    : Comp2Pin( type, id )
    , eResistor( id )
{
    m_ePin[0] = m_pin[0];
    m_ePin[1] = m_pin[1];
    m_pin[0]->setLength( 5 );
    m_pin[1]->setLength( 5 );
    setThermalNoise( true );

    m_in        = 1;
    m_speed     = spT;
    m_speedStr  = "T";
    m_i2tSet    = 0;
    m_vdropSet  = 0;
    m_ratedVolt = 250;
    m_breakSet  = 0;
    m_cMinFuse  = presetF[1];
    m_cShare    = presetF[2];
    m_cTau2     = presetF[3];
    m_resMode   = false;
    m_keepBlown = false;
    m_blown     = false;
    m_blownBySim = false;
    m_blownAt   = -1;
    m_blowCurrent = 0;
    m_broke     = false;
    m_overVolt  = false;
    m_overVoltAt = 0;
    m_overVoltMax = 0;
    m_level     = -1;
    m_msgPending = m_voltMsgPending = false;
    m_repaint   = false;
    m_tFlush = m_tPrev = 0;
    m_u = m_S = m_sumI2t = m_predU = 0;
    m_h = 1;

    setupHeat();
    m_admit = 1/resistance();

    Simulator::self()->addToUpdateList( this );

    addPropGroup( { tr("Main"), {
        new DoubProp<Fuse>("Current", tr("Rated Current"), "A"
                          , this, &Fuse::ratedCurrent, &Fuse::setRatedCurrent ),
        new StrProp <Fuse>("Speed", tr("Speed"), "F,T,Custom;"+tr("F (quick-acting)")+","+tr("T (time-lag)")+","+tr("Custom")
                          , this, &Fuse::speed, &Fuse::setSpeed, 0, "enum" ),
        new DoubProp<Fuse>("I2t", tr("Melting I²t (0 = preset)"), "A²s"
                          , this, &Fuse::i2t, &Fuse::setI2t ),
        new DoubProp<Fuse>("Vdrop", tr("Voltage Drop at In (0 = preset)"), "mV"
                          , this, &Fuse::vdrop, &Fuse::setVdrop ),
        new StrProp <Fuse>("Model", tr("Model"), "Fuse,Resistor;"+tr("Fuse")+","+tr("Behave as resistor")
                          , this, &Fuse::model, &Fuse::setModel, 0, "enum" ),
        showStateProp()
    }, 0 } );
    enableStates( true );

    addPropGroup( { tr("Ratings"), {
        new DoubProp<Fuse>("Voltage", tr("Rated Voltage"), "V"
                          , this, &Fuse::ratedVolt, &Fuse::setRatedVolt ),
        new DoubProp<Fuse>("Breaking", tr("Breaking Capacity (0 = preset)"), "A"
                          , this, &Fuse::breaking, &Fuse::setBreaking ),
    }, 0 } );

    addPropGroup( { tr("State"), {
        new BoolProp<Fuse>("Blown", tr("Blown"), ""
                          , this, &Fuse::blownProp, &Fuse::setBlownProp, propNoCopy ),
        new BoolProp<Fuse>("KeepBlown", tr("Stays blown after a restart"), ""
                          , this, &Fuse::keepBlown, &Fuse::setKeepBlown ),
    }, 0 } );

    addPropGroup( { tr("Custom"), {
        new DoubProp<Fuse>("MinFuse", tr("Minimum Fusing Current (Speed = Custom)"), "×In"
                          , this, &Fuse::minFuse, &Fuse::setMinFuse ),
        new DoubProp<Fuse>("Share", tr("Body/Element Conductance (Speed = Custom)"), ""
                          , this, &Fuse::share, &Fuse::setShare ),
        new DoubProp<Fuse>("Tau2", tr("Body Time Constant (Speed = Custom)"), "s"
                          , this, &Fuse::tau2, &Fuse::setTau2 ),
    }, 0 } );

    setShowProp("Current");
    setPropStr("Current", "1");
}
Fuse::~Fuse(){}

// ---- Parameters ------------------------------------------------------------------------------

double Fuse::i2tUsed()
{
    if( m_i2tSet > 0 ) return m_i2tSet;
    if( m_speed == spT ) return interp( fstTable, sizeof(fstTable)/sizeof(FuseRow), m_in, true );
    return interp( fsfTable, sizeof(fsfTable)/sizeof(FuseRow), m_in, true );
}

double Fuse::vdropUsed()
{
    if( m_vdropSet > 0 ) return m_vdropSet;
    if( m_speed == spT ) return interp( fstTable, sizeof(fstTable)/sizeof(FuseRow), m_in, false );
    return interp( fsfTable, sizeof(fsfTable)/sizeof(FuseRow), m_in, false );
}

double Fuse::breakingUsed()     // [DOC] 35 A up to 3.15 A (glass, L), 10 x In above
{
    if( m_breakSet > 0 ) return m_breakSet;
    return std::max( 35.0, 10*m_in );
}

void Fuse::setupHeat()
{
    double c1r, mf, share, tau2;
    if( m_speed == spCustom ){
        c1r = 1; mf = m_cMinFuse; share = m_cShare; tau2 = m_cTau2;
    }else{
        const double* p = ( m_speed == spT ) ? presetT : presetF;
        c1r = p[0]; mf = p[1]; share = p[2]; tau2 = p[3];
    }
    double gt = ( mf*m_in )*( mf*m_in );      // 1/(1/G1 + 1/G2) = I_mf²
    double g1 = gt*( 1+1/share );
    double g2 = share*g1;
    double c1 = c1r*i2tUsed();
    m_heat.setParams( c1, g1, tau2*g2, g2 );

    double h = m_heat.tau1()/50*1e12;         // ps
    m_h = h < 1 ? 1 : (uint64_t)h;
}

// A property changed during a run: hold the simulation, unless it is already paused (then
// resumeSim() would also undo the user's pause)
static void applyLocked( Simulator* sim, bool& paused )
{
    paused = sim->simState() > SIM_PAUSED;
    if( paused ) sim->pauseSim();
}

void Fuse::setRatedCurrent( double i )
{
    if( i <= 0 ) return;
    Simulator* sim = Simulator::self(); bool paused;
    applyLocked( sim, paused );
    m_in = i;
    setupHeat();
    if( !m_blown ) eResistor::setRes( resistance() );
    if( paused ) sim->resumeSim();
    updateLabel();
}

void Fuse::setSpeed( QString s )
{
    QString l = s.trimmed().toUpper();
    Simulator* sim = Simulator::self(); bool paused;
    applyLocked( sim, paused );
    if     ( l.startsWith("F") ) { m_speed = spF; m_speedStr = "F"; }
    else if( l.startsWith("C") ) { m_speed = spCustom; m_speedStr = "Custom"; }
    else                         { m_speed = spT; m_speedStr = "T"; }
    setupHeat();
    if( !m_blown ) eResistor::setRes( resistance() );
    if( paused ) sim->resumeSim();
    updateLabel();
}

void Fuse::setI2t( double v )
{
    Simulator* sim = Simulator::self(); bool paused;
    applyLocked( sim, paused );
    m_i2tSet = v > 0 ? v : 0;
    setupHeat();
    if( paused ) sim->resumeSim();
}

void Fuse::setVdrop( double v )
{
    Simulator* sim = Simulator::self(); bool paused;
    applyLocked( sim, paused );
    m_vdropSet = v > 0 ? v : 0;
    if( !m_blown || m_resMode ) eResistor::setRes( resistance() );
    if( paused ) sim->resumeSim();
}

void Fuse::setMinFuse( double m ){ if( m > 1 ){ m_cMinFuse = m; if( m_speed == spCustom ) setupHeat(); } }
void Fuse::setShare( double s )  { if( s > 0 ){ m_cShare   = s; if( m_speed == spCustom ) setupHeat(); } }
void Fuse::setTau2( double t )   { if( t > 0 ){ m_cTau2    = t; if( m_speed == spCustom ) setupHeat(); } }

void Fuse::setModel( QString m )
{
    bool res = m.trimmed().toLower().startsWith("r");
    if( res == m_resMode ) return;

    Simulator* sim = Simulator::self();
    if( !sim->isRunning() ){ m_resMode = res; update(); return; }

    bool paused;                          // Changed during a run: from now, with a cold fuse
    applyLocked( sim, paused );
    m_resMode = res;
    if( eventTime ) sim->cancelEvents( this );
    m_predU = 0;
    bool conn = m_ePin[0]->isConnected() && m_ePin[1]->isConnected();
    if( res ){
        if( conn ){ m_ePin[0]->changeCallBack( this, false ); m_ePin[1]->changeCallBack( this, false ); }
        eResistor::setRes( resistance() );
        clearStates();
        m_level = -1;
    }else{
        m_heat.reset();
        m_tFlush = m_tPrev = sim->circTime();
        m_u = m_S = 0;
        if( conn ){ m_ePin[0]->changeCallBack( this ); m_ePin[1]->changeCallBack( this ); }
        if( m_blown ) eResistor::setAdmit( 0 );
        else          eResistor::setRes( resistance() );
    }
    if( paused ) sim->resumeSim();
    update();
}

QString Fuse::marking()
{
    QString sp = ( m_speed == spCustom ) ? "" : m_speedStr;
    if( m_in < 1 ) return sp+QString::number( m_in*1e3, 'g', 4 )+"mA";
    return sp+QString::number( m_in, 'g', 4 )+"A";
}

void Fuse::updateLabel()
{
    if( showProp() == "Current" ) setValLabelText( marking() );
}

// ---- Simulation ------------------------------------------------------------------------------

void Fuse::initialize()
{
    if( m_blown && m_blownBySim && !m_keepBlown ) m_blown = false;   // Restart: a fresh fuse

    setupHeat();
    m_heat.reset();
    m_tFlush = m_tPrev = 0;
    m_u = m_S = m_sumI2t = m_predU = 0;
    if( !m_blown ){
        m_blownAt = -1;
        m_broke   = false;
    }
    m_overVolt = false;
    m_msgPending = m_voltMsgPending = false;
    m_level = -1;
    clearStates();
    if( m_blown && !m_resMode ){
        m_level = 2;
        reportState( m_broke ? "BLOWN, BROKE" : "BLOWN", stFault );
    }
    m_repaint = true;
}

void Fuse::stamp()
{
    if( m_resMode ){
        m_admit = 1/resistance();
        eResistor::stamp();
        return;
    }
    m_admit = m_blown ? 0 : 1/resistance();
    if( m_ePin[0]->isConnected() && m_ePin[1]->isConnected() ){
        m_ePin[0]->changeCallBack( this );
        m_ePin[1]->changeCallBack( this );
    }
    eResistor::stamp();
}

void Fuse::integrate( uint64_t t )      // ∫I²dt with the current of the last change
{
    if( t <= m_tPrev ) return;
    m_S += m_u*( t-m_tPrev )*1e-12;
    m_tPrev = t;
}

void Fuse::flush( uint64_t t )          // Exact heat update, the interval's mean I² over it
{
    integrate( t );
    if( t > m_tFlush ){
        double dt = ( t-m_tFlush )*1e-12;
        m_heat.advance( dt, m_S/dt );
    }
    m_sumI2t += m_S;
    m_S = 0;
    m_tFlush = t;
}

void Fuse::schedule( uint64_t t, uint64_t at )
{
    Simulator* sim = Simulator::self();
    if( eventTime ) sim->cancelEvents( this );
    sim->addEvent( at > t ? at-t : 1, this );
}

// Once per converged solution that moved a pin (linear element: after the non-linear loop).
// Cheap unless the element can be near melting or the current is above the minimum fusing
// current (specs/fuse.md § 6.2)
void Fuse::voltChanged()
{
    if( m_resMode ) return;
    double volt = m_ePin[0]->getVoltage()-m_ePin[1]->getVoltage();

    if( m_blown ){
        // The rating is AC rms (250 VAC): its peak, √2 × 250 = 354 V, so 230 V mains (325 V) is fine
        double v = qFabs( volt );
        if( v > M_SQRT2*m_ratedVolt ){
            if( !m_overVolt ){
                m_overVolt = true;
                m_voltMsgPending = true;
                m_overVoltAt  = Simulator::self()->circTime();
                m_overVoltMax = v;
            }
            else if( m_voltMsgPending && v > m_overVoltMax ) m_overVoltMax = v;
        }
        return;
    }
    uint64_t t = Simulator::self()->circTime();
    integrate( t );
    double i = volt*m_admit;
    m_u = i*i;

    double c1 = m_heat.c1();
    double room = 1-std::max( m_heat.theta(), m_heat.phi() )-m_S/c1;   // Upper bound of θ now
    if( room < 0.05 || t-m_tFlush >= m_h ){
        flush( t );
        room = 1-std::max( m_heat.theta(), m_heat.phi() );
    }
    if( m_heat.theta() >= 1-1e-9 ){ blow( t, true ); return; }

    if( m_u > m_heat.meltU() && ( m_predU == 0 || m_u > m_predU ) )
    {                                     // Overload: an event no later than the earliest melt
        double ts = std::max( room, 0.0 )*c1/m_u;
        uint64_t at = t+std::max( (uint64_t)1, (uint64_t)( ts*1e12 ) );
        if( !eventTime || at < eventTime ) schedule( t, at );
        m_predU = m_u;
    }
    updateState();
}

void Fuse::runEvent()
{
    m_predU = 0;
    if( m_blown || m_resMode ) return;

    uint64_t t = Simulator::self()->circTime();
    flush( t );
    if( m_heat.theta() >= 1-1e-9 ){ blow( t, true ); return; }

    if( m_u > m_heat.meltU() )            // Still overloaded: the exact melting time at this current
    {
        double tm = m_heat.timeToMelt( m_u );
        if( tm >= 0 ){
            schedule( t, t+std::max( (uint64_t)1, (uint64_t)std::ceil( tm*1e12 ) ) );
            m_predU = m_u;
        }
    }
    updateState();
}

void Fuse::blow( uint64_t t, bool bySim )
{
    Simulator* sim = Simulator::self();
    m_blown      = true;
    m_blownBySim = bySim;
    m_blownAt    = t*1e-12;
    m_blowCurrent = std::sqrt( m_u );
    m_broke      = bySim && m_blowCurrent > breakingUsed();
    if( eventTime ) sim->cancelEvents( this );
    m_predU = 0;
    m_u = 0;
    eResistor::setAdmit( 0 );

    if( !m_resMode ){
        m_level = 2;
        reportState( m_broke ? "BLOWN, BROKE" : "BLOWN", stFault );
    }
    m_msgPending = bySim;
    m_repaint = true;
}

// OK (green) -> HOT (amber: above In, or the element at 80 % of melting) -> back below 95 % of In
// and 75 %
void Fuse::updateState()
{
    double in2 = m_in*m_in;
    double th  = m_heat.theta();
    int lvl = m_level < 0 ? 0 : m_level;
    if( lvl == 0 ){ if( m_u > in2 || th >= 0.8 ) lvl = 1; }
    else if( m_u < 0.9025*in2 && th < 0.75 ) lvl = 0;

    if( lvl == m_level ) return;
    m_level = lvl;
    reportState( lvl ? "HOT" : "OK", lvl ? stWait : stOn );
    m_repaint = true;
}

double Fuse::heatNow()
{
    if( m_blown ) return std::max( 1.0, m_heat.theta() );
    if( !Simulator::self()->isRunning() && !Simulator::self()->isPaused() ) return m_heat.theta();
    uint64_t t = Simulator::self()->circTime();
    if( t <= m_tFlush ) return m_heat.theta();
    double s = m_S + ( t > m_tPrev ? m_u*( t-m_tPrev )*1e-12 : 0 );
    double dt = ( t-m_tFlush )*1e-12;
    return m_heat.thetaAfter( dt, s/dt );
}

double Fuse::i2tSum()
{
    double s = m_sumI2t + m_S;
    uint64_t t = Simulator::self()->circTime();
    if( !m_blown && t > m_tPrev ) s += m_u*( t-m_tPrev )*1e-12;
    return s;
}

void Fuse::updateStep()
{
    if( m_msgPending ){
        m_msgPending = false;
        uint64_t ps = (uint64_t)( m_blownAt*1e12+0.5 );
        QString text = idLabel()+": "+tr("blew")+QString(", I = %1 A, I²t %2 A²s")
                       .arg( m_blowCurrent, 0, 'g', 4 ).arg( m_sumI2t, 0, 'g', 4 );
        if( m_broke ) text += ", "+tr("above its breaking capacity")
                             +QString(" (%1 A)").arg( breakingUsed(), 0, 'g', 4 );
        CircuitWidget::self()->eventMessage("[ "+EventLog::timeStr( ps )+"] "+text
                                            , stFault, getUid(), (int64_t)ps );
    }
    if( m_voltMsgPending ){
        m_voltMsgPending = false;
        uint64_t ps = m_overVoltAt;
        QString text = idLabel()+": "+QString("%1 V ").arg( m_overVoltMax, 0, 'g', 4 )
             +tr("across the blown fuse, above its rating")
             +QString(" (%1 V rms, %2 V peak)").arg( m_ratedVolt ).arg( M_SQRT2*m_ratedVolt, 0, 'f', 0 );
        if( m_blownBySim && m_blownAt >= 0 && ps*1e-12-m_blownAt < 1e-4 )   // Within 100 µs of blowing
            text += ", "+tr("when the current was cut: an inductive spike, a real fuse arcs instead");
        CircuitWidget::self()->eventMessage("[ "+EventLog::timeStr( ps )+"] "+text
                                            , stWait, getUid(), (int64_t)ps );
    }
    if( m_repaint || m_level == 1 ){ m_repaint = false; update(); }
}

// ---- Replacing --------------------------------------------------------------------------------

void Fuse::replace()
{
    Simulator* sim = Simulator::self();
    bool running = sim->isRunning();
    bool paused;
    applyLocked( sim, paused );

    m_blown = false;
    m_blownBySim = false;
    m_blownAt = -1;
    m_broke = false;
    m_overVolt = false;
    m_heat.reset();
    m_tFlush = m_tPrev = running ? sim->circTime() : 0;
    m_u = m_S = m_predU = 0;
    if( running ){
        if( eventTime ) sim->cancelEvents( this );
        eResistor::setRes( resistance() );
        m_level = -1;
        if( !m_resMode ) updateState();
    }
    if( paused ) sim->resumeSim();
    m_repaint = true;
    update();
}

void Fuse::setBlownProp( bool b )
{
    if( b == m_blown ) return;
    Simulator* sim = Simulator::self();
    if( !b ){ replace(); return; }

    if( sim->isRunning() && !m_resMode ){     // Blown by hand
        bool paused;
        applyLocked( sim, paused );
        flush( sim->circTime() );
        blow( sim->circTime(), false );
        if( paused ) sim->resumeSim();
    }else{
        m_blown = true;
        m_blownBySim = false;
    }
    update();
}

void Fuse::contextMenu( QGraphicsSceneContextMenuEvent* event, QMenu* menu )
{
    if( m_blown ){
        QAction* a = menu->addAction( tr("Replace fuse") );
        QObject::connect( a, &QAction::triggered, [=](){ setPropStr("Blown", "false"); } );
    }else{
        QAction* a = menu->addAction( tr("Blow fuse") );
        QObject::connect( a, &QAction::triggered, [=](){ setPropStr("Blown", "true"); } );
    }
    menu->addSeparator();
    Component::contextMenu( event, menu );
}

// ---- Drawing --------------------------------------------------------------------------------

void Fuse::paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w )
{
    Component::paint( p, o, w );
    p->drawRect( m_area );

    QPen pen = p->pen();
    if( m_resMode ) pen.setStyle( Qt::DashLine );     // Can't blow

    if( m_blown && !m_resMode ){
        pen.setColor( StatusView::levelColor( stFault ) );
        p->setPen( pen );
        p->drawLine( QPointF(-11, 0 ), QPointF(-3, 0 ) );     // Broken element
        p->drawLine( QPointF(-3, 0 ),  QPointF(-1,-2.5 ) );
        p->drawLine( QPointF( 11, 0 ), QPointF( 3, 0 ) );
        p->drawLine( QPointF( 3, 0 ),  QPointF( 1, 2.5 ) );
    }else{
        if( m_level == 1 && !m_resMode && StatusView::self()->enabled() ){
            double th = std::min( 1.0, std::max( 0.0, heatNow() ) );
            pen.setColor( StatusView::levelColor( stWait ) );
            pen.setWidthF( pen.widthF()+1.5*th );
        }
        p->setPen( pen );
        p->drawLine( QPointF(-11, 0 ), QPointF( 11, 0 ) );
    }
    Component::paintSelected( p );
}
