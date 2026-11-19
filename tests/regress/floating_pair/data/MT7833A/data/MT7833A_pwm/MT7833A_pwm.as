// MT7833A block "PWM Controller" (datasheet p.4 block diagram)
//
// Runs while EN is high and COMP has reached 1.4 V (start-up, p.4). Each cycle: switch on for
// an on-time set by COMP; off at the end of it, or at once when the OCP comparator trips
// (after the 300 ns blanking); on again when the feedback detection reports the inductor
// demagnetised (ZCD), no sooner than 10 us after the last turn-on, or after the maximum period.
// At each turn-on it holds the average of CS over the cycle that just ended on ISENSE (the
// datasheet draws an RC there, see README) and updates the error amplifier, a transconductance
// stage: net current into COMP = gm x (0.2 V - ISENSE), clamped to +-59 uA, so COMP integrates
// the average error (not the share of time above 0.2 V, as a comparator would). Drawn as a
// current source VEA into COMP with R_EA from COMP to GND: VEA supplies COMP/R_EA on top, so
// R_EA only acts when the EA is off (pre-charge and discharge, as before).
// CS carries the coil current in both phases (the freewheel diode returns through R_CS), a
// triangle: average = (peak x on-time + start of the decay x demagnetisation time) / 2 / period.
// The decay is a straight line: two CS samples, 0.5 and 1.5 us after turn-off, give its start
// and when it reaches zero.
// While waiting for COMP (start-up): 5 V behind R_EA, the COMP pre-charge. EN off: VEA 0, COMP
// discharged through R_EA.
// Soft start: from the first switching cycle, COMP is raised by a fixed soft-start current instead
// of the EA, until a cycle that ended with the coil empty (CS back near 0 at the next turn-on:
// boundary mode) has a CS average
// of 0.2 V or more: the loop is reached ("LoopOK", Fig. 1). The datasheet lists a power-on soft
// start (p.1) but gives no numbers. The first cycles, with the output still near 0 V, never empty
// the coil and average well above 0.2 V: they do not end it (their DSEN plateau is also below the
// 0.1 V ZCD level, so ZCD cannot tell). Without a soft start COMP climbs at
// gm x 0.2 V / C_COMP only (~0.1 V in 7.5 ms with 2.2 uF), the output stays below the SCP level
// for the whole SCP time and the board hiccups at power-on (led_driver
// datasheets/MT7833A_notes.md section 3.3). SoftStart_uA = 0 turns it off.
// A FAULT stops it until EN drops.
// Every constant names its source: [DOC p.N] datasheet, [INFERRED] not in the datasheet.
//
// Links: 0 VGATE (driver: voltage source GATE -> CS), 1 VISENSE (voltage source ISENSE -> GND),
//        2 VEA (error amplifier: current source GND -> COMP)

const double V_RUN    = 1.4;         // COMP level where switching starts   [DOC p.4 Start Up]
const uint64 T_MIN    = 10000000;    // minimum switching period 10 us      [DOC p.3 MinT]
const uint64 T_LEB_CS = 300000;      // OCP blanking 300 ns                 [DOC p.3 LEB2]
const double V_GATE   = 10.0;        // gate drive (Ron given at VGS=10V)   [DOC p.3]
const double V_LOGIC  = 2.5;         // input threshold of EN, ZCD, FAULT, OCP [INFERRED]
const double V_FB     = 0.2;         // feedback reference                  [DOC p.3 VFB]
const double V_PRE    = 5.0;         // COMP pre-charge: 5 V (EN) behind R_EA [INFERRED]
const double R_EA     = 85e3;        // R_EA in the .sim1                   [INFERRED]
const double I_EA_MAX = 59e-6;       // EA output current limit (= 5 V / R_EA) [INFERRED]
const double T_ON_MIN = 0.3;         // shortest on-time (us): the CS blanking [INFERRED]
const uint64 T_S1     = 500000;      // first CS sample in the off-time, 0.5 us [INFERRED]
const uint64 T_S2     = 1500000;     // second CS sample, 1.5 us [INFERRED]

double tonPerV   = 5.0;    // on-time per volt of COMP above 1.4 V (us/V)
double tonMax    = 25.0;   // longest on-time (us)
double periodMax = 60.0;   // "selectable maximum period": switch on anyway after this (us)
double eaGm      = 200.0;  // EA transconductance (uS): linear up to +-0.3 V of error
double iSoftStart = 150.0; // soft-start current into COMP until the loop is reached (uA); 0 = off

IoPin@ zcdPin   = component.getPin("ZCD");
IoPin@ faultPin = component.getPin("FAULT");
IoPin@ enPin    = component.getPin("EN");
IoPin@ compPin  = component.getPin("COMP");
IoPin@ ocpPin   = component.getPin("OCP");
IoPin@ csPin    = component.getPin("CS");
IoPin@ gndPin   = component.getPin("GND");

enum state_t { IDLE=0, WAIT, RUN, STOP };
state_t state;
bool    gateOn, zcdWas;
bool    softStart;           // in the soft start: COMP raised by iSoftStart
bool    valley;              // the cycle just ended with the coil empty (CS ~ 0 at turn-on)
uint64  tOn;
int     pending;             // 1 = turn off, 2 = turn on, 3 = end of the CS blanking, 4/5 = CS samples
uint64  tonPs;               // on-time asked for the current cycle (ps)
uint64  tOff;                // turn-off time of the current cycle
double  vPeak;               // CS at turn-off (V)
double  vStart;              // CS where the decay starts (V)
double  v1;                  // first off-time sample (V)
double  tDemag;              // demagnetisation time of the current cycle (ps); < 0: not known yet

double rel( IoPin@ p ) { return p.getVoltage() - gndPin.getVoltage(); }
bool   high( IoPin@ p ) { return rel( p ) > V_LOGIC; }

void setup() {}

void reset()
{
    zcdPin.setPinMode( 1 ); faultPin.setPinMode( 1 ); enPin.setPinMode( 1 ); compPin.setPinMode( 1 );
    ocpPin.setPinMode( 1 ); csPin.setPinMode( 1 ); gndPin.setPinMode( 1 );
    zcdPin.changeCallBack( element, true );
    faultPin.changeCallBack( element, true );
    enPin.changeCallBack( element, true );
    compPin.changeCallBack( element, true );
    ocpPin.changeCallBack( element, true );
    state = IDLE; gateOn = false; zcdWas = false; tOn = 0; pending = 0; softStart = false; valley = false;
    tOff = 0; vPeak = 0; vStart = 0; v1 = 0; tDemag = -1;
    component.showValue("IDLE");
    component.setStatus( "IDLE", ST_OFF );
    schedule( 1000, 0 );                    // outputs low once the linked sources are stamped
}

void schedule( uint64 d, int what ) { component.cancelEvents(); pending = what; component.addEvent( d ); }
void setGate( bool on ) { gateOn = on; component.setLinkedValue( 0, on ? V_GATE : 0.0, 0 ); }
void setState( state_t s, string txt )
{
    state = s;
    component.showValue( txt );
    int level = ST_ON;                                               // Part state on the canvas
    if     ( s == IDLE ) level = ST_OFF;
    else if( s == WAIT ) level = ST_WAIT;
    else if( s == STOP ) level = ST_FAULT;
    component.setStatus( txt, level );
}
void setEaCurrent( double i ) { component.setLinkedValue( 2, i, 0 ); }   // into COMP
void setEaOff()     { setEaCurrent( 0 ); }                             // R_EA discharges COMP
void setEaPrecharge() { setEaCurrent( V_PRE/R_EA ); }
void setEaError( double err )                                          // COMP/R_EA cancels R_EA
{
    if( softStart )                                                    // until the loop is reached
    {
        if( iSoftStart > 0 && ( err > 0 || !valley ) )
        {
            setEaCurrent( rel( compPin )/R_EA + iSoftStart*1e-6 ); return;
        }
        softStart = false;                                             // loop reached at the valley
        setState( RUN, "RUN" );
    }
    double i = eaGm*1e-6*err;
    if( i >  I_EA_MAX ) i =  I_EA_MAX;
    if( i < -I_EA_MAX ) i = -I_EA_MAX;
    setEaCurrent( rel( compPin )/R_EA + i );
}

void holdAverage( uint64 now )                                 // the cycle from tOn to now ended
{
    if( tOn == 0 || tOff <= tOn || now <= tOn ) return;
    if( tDemag < 0 ) { tDemag = double( now - tOff ); vStart = vPeak; }   // ZCD before the samples
    double period = double( now - tOn );
    double ton = double( tOff - tOn );
    if( tDemag > period - ton ) tDemag = period - ton;
    double isense = ( vPeak*ton + vStart*tDemag )/2/period;
    component.setLinkedValue( 1, isense, 0 );
    setEaError( V_FB - isense );
}

void turnOn()
{
    valley = vPeak > 0 && rel( csPin ) < 0.05*vPeak;          // coil emptied in this cycle
    holdAverage( component.circTime() );
    double ton = tonPerV*( rel( compPin ) - V_RUN );
    if( ton > tonMax )   ton = tonMax;
    if( ton < T_ON_MIN ) ton = T_ON_MIN;
    tOn = component.circTime();
    tonPs = uint64( ton*1e6 );
    setGate( true );
    if( tonPs > T_LEB_CS ) schedule( T_LEB_CS, 3 );           // check OCP when the blanking ends
    else                   schedule( tonPs, 1 );
}

void turnOff()
{
    vPeak = rel( csPin );
    tOff = component.circTime();
    tDemag = -1;
    setGate( false );
    schedule( T_S1, 4 );                                       // CS samples, then the maximum period
}

void offSample( int which )                                    // straight-line decay to zero
{
    double v = rel( csPin );
    double low = 0.02*vPeak;
    if( which == 4 )
    {
        v1 = v;
        if( v <= low ) { tDemag = double( T_S1 ); vStart = vPeak; }   // already empty
        else { schedule( T_S2 - T_S1, 5 ); return; }
    }
    else if( v <= low || v >= v1 )                             // emptied between the samples
    {
        tDemag = double( T_S1 ) + double( T_S2 - T_S1 )*0.5;
        vStart = vPeak;
    }
    else
    {
        double slope = ( v1 - v )/double( T_S2 - T_S1 );       // V per ps
        tDemag = double( T_S2 ) + v/slope;
        vStart = v + slope*double( T_S2 );
    }
    schedule( uint64( periodMax*1e6 ) - T_S2, 2 );              // maximum period from turn-off
}

void nextOn()                                                  // valley found: >= 10 us period
{
    uint64 earliest = tOn + T_MIN;
    uint64 now = component.circTime();
    if( tDemag < 0 ) { tDemag = double( now - tOff ); vStart = vPeak; }  // emptied before the samples
    if( now >= earliest ) turnOn();
    else schedule( earliest - now, 2 );
}

void runEvent()
{
    int what = pending; pending = 0;
    if( what == 0 )                                            // outputs once the sources are stamped
    {
        setGate( false ); component.setLinkedValue( 1, 0.0, 0 );
        if( state == WAIT ) setEaPrecharge(); else setEaOff();  // EN may already be up
        voltChanged(); return;
    }
    if( ( what == 4 || what == 5 ) && state == RUN && !gateOn ) { offSample( what ); return; }
    if( state != RUN ) return;
    if( what == 3 && gateOn )
    {
        if( high( ocpPin ) ) turnOff();                        // OCP already over 1.4 V
        else schedule( tonPs - T_LEB_CS, 1 );
    }
    else if( what == 1 && gateOn )  turnOff();
    else if( what == 2 && !gateOn ) turnOn();
}

void voltChanged()
{
    if( !high( enPin ) )                                       // supply off or hiccup
    {
        if( state != IDLE )
        {
            component.cancelEvents(); pending = 0; setGate( false ); setState( IDLE, "IDLE" );
            tOn = 0; component.setLinkedValue( 1, 0.0, 0 );            // no current: ISENSE 0
            setEaOff();                                                // COMP discharged [DOC p.5]
        }
        return;
    }
    if( state == IDLE ) { setState( WAIT, "WAIT COMP" ); setEaPrecharge(); }
    if( state == STOP ) return;
    if( high( faultPin ) )                                     // OVP / SCP: stop until EN drops
    {
        component.cancelEvents(); pending = 0; setGate( false ); setState( STOP, "STOP" );
        return;
    }
    if( state == WAIT )
    {
        if( rel( compPin ) >= V_RUN )
        {
            softStart = iSoftStart > 0;
            setState( RUN, softStart ? "SOFT START" : "RUN" );
            turnOn();
        }
        return;
    }
    // RUN
    bool zcd = high( zcdPin );
    if( gateOn )
    {
        if( component.circTime() - tOn > T_LEB_CS && high( ocpPin ) ) turnOff();   // OCP [DOC p.5]
    }
    else if( zcd && !zcdWas ) nextOn();
    zcdWas = zcd;
}

void   setTon_us_per_V( double v )  { if( v > 0 ) tonPerV = v; }
double getTon_us_per_V()            { return tonPerV; }
void   setTon_max_us( double v )    { if( v > 0 ) tonMax = v; }
double getTon_max_us()              { return tonMax; }
void   setPeriod_max_us( double v ) { if( v >= 10 ) periodMax = v; }
double getPeriod_max_us()           { return periodMax; }
void   setEA_gm_uS( double v )      { if( v > 0 ) eaGm = v; }
double getEA_gm_uS()                { return eaGm; }
void   setSoftStart_uA( double v )  { if( v >= 0 ) iSoftStart = v; }
double getSoftStart_uA()            { return iSoftStart; }
