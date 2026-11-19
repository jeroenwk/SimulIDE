// MT7833A block "Feedback Detection" (datasheet p.4 block diagram, p.5 "Auxiliary Sensing")
//
// Samples DSEN (relative to GND) 2 us after each turn-off of the switch (DRV falling):
//   OVP  DSEN > 3.2 V three times within 1 ms        -> FAULT
//   SCP  DSEN < 0.4 V for 7.5 ms of switching        -> FAULT
//   ZCD  DSEN below 0.1 V after the sample: the inductor is demagnetised, next cycle may start
// FAULT stays high 1 ms (the supply block latches the hiccup), then clears.
// Every constant names its source: [DOC p.N] datasheet, [INFERRED] not in the datasheet.
//
// Links: 0 VZCD (voltage source ZCD -> GND), 1 VFAULT (voltage source FAULT -> GND)

const double V_OVP     = 3.2;          // DSEN over-voltage threshold      [DOC p.3/p.5 OVP]
const double V_SCP     = 0.400;        // DSEN short-circuit threshold     [DOC p.3/p.5 SCP]
const double V_ZCD     = 0.1;          // DSEN level taken as demagnetised [INFERRED]
const uint64 T_LEB     = 2000000;      // sample 2 us after turn-off       [DOC p.3 LEB1, p.5 Fig.2]
const uint64 T_SCP     = 7500000000;   // SCP after 5-10 ms: 7.5 ms        [DOC p.5]
const uint64 T_OVP_WIN = 1000000000;   // OVP: three times within 1 ms     [DOC p.5]
const uint64 T_IDLE    = 1000000000;   // no switching for 1 ms: new start, counters cleared [INFERRED]
const double V_DRV_ON  = 5.0;          // DRV (gate) level seen as on      [INFERRED]
const double V_OUT     = 5.0;          // output level of ZCD and FAULT    [INFERRED]

IoPin@ dsenPin = component.getPin("DSEN");
IoPin@ drvPin  = component.getPin("DRV");
IoPin@ gndPin  = component.getPin("GND");

bool   drvOn, armed, fault;
uint64 lastSample, scpStart, ovp0, ovp1;
int    ovpCount;
int    pending;              // 1 = sample, 2 = clear fault
string shown;

double rel( IoPin@ p ) { return p.getVoltage() - gndPin.getVoltage(); }

void setup() {}

void reset()
{
    dsenPin.setPinMode( 1 ); drvPin.setPinMode( 1 ); gndPin.setPinMode( 1 );
    dsenPin.changeCallBack( element, true );
    drvPin.changeCallBack( element, true );
    drvOn = false; armed = false; fault = false;
    lastSample = 0; scpStart = 0; ovp0 = 0; ovp1 = 0; ovpCount = 0;
    pending = 0; shown = "";
    show("OK");
    schedule( 1000, 2 );                    // outputs low once the linked sources are stamped
}

void show( string s )
{
    if( s == shown ) return;
    shown = s;
    component.showValue( s );
    component.setStatus( s, s == "OK" ? ST_ON : ST_FAULT );          // Part state on the canvas
}
void schedule( uint64 d, int what ) { component.cancelEvents(); pending = what; component.addEvent( d ); }
void setZcd( bool on )   { component.setLinkedValue( 0, on ? V_OUT : 0.0, 0 ); }
void setFault( bool on ) { fault = on; component.setLinkedValue( 1, on ? V_OUT : 0.0, 0 ); }

void voltChanged()
{
    bool on = rel( drvPin ) > V_DRV_ON;
    if( on != drvOn )
    {
        drvOn = on;
        if( on ) { armed = false; setZcd( false ); }          // new cycle
        else if( !fault ) schedule( T_LEB, 1 );                // turn-off: sample after 2 us
    }
    if( armed && rel( dsenPin ) < V_ZCD ) { armed = false; setZcd( true ); }
}

void runEvent()
{
    int what = pending; pending = 0;
    if( what == 2 ) { setFault( false ); setZcd( false ); return; }
    if( what != 1 ) return;

    uint64 now = component.circTime();
    if( now - lastSample > T_IDLE ) { scpStart = 0; ovpCount = 0; show("OK"); }   // new start
    lastSample = now;
    double v = rel( dsenPin );

    if( v > V_OVP )                                            // OVP [DOC p.5]
    {
        if( ovpCount >= 2 && now - ovp0 < T_OVP_WIN ) { trip("OVP"); return; }
        ovp0 = ovp1; ovp1 = now;
        if( ovpCount < 2 ) ovpCount++;
    }
    if( v < V_SCP )                                            // SCP [DOC p.5]
    {
        if( scpStart == 0 ) scpStart = now;
        else if( now - scpStart > T_SCP ) { trip("SCP"); return; }
    }
    else scpStart = 0;

    if( v < V_ZCD ) setZcd( true );                            // already demagnetised
    else            armed = true;
}

void trip( string what )
{
    show( what );
    setFault( true );
    schedule( T_IDLE, 2 );                                     // clear after 1 ms
}
