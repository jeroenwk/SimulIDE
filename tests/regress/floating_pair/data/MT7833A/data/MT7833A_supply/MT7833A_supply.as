// MT7833A block "Supply & VDD detection" + "Start-up" (datasheet p.4 block diagram, p.4-5 text)
//
// Watches VDD (relative to GND), sets the IC's supply current and the EN line that powers the
// error amplifier and lets the PWM controller run. A FAULT from the feedback detection (OVP,
// SCP) starts a hiccup: EN off and VDD discharged until UVLO, then a new start-up.
// Every constant names its source: [DOC p.N] datasheet, [INFERRED] not in the datasheet.
//
// Links: 0 IVDD (current source VDD -> GND), 1 VEN (voltage source EN -> GND)

const double V_STARTUP = 18.0;    // VDD start-up voltage                 [DOC p.3 VSTP]
const double V_UVLO    = 9.0;     // VDD under-voltage lockout            [DOC p.3 UVLO]
const double I_STARTUP = 25e-6;   // supply current before start-up       [DOC p.3 ISTP]
const double I_Q       = 1.0e-3;  // quiescent current, running           [DOC p.3 IQ]
const double V_EN      = 5.0;     // EN level (internal logic supply)     [INFERRED]

double hiccupI = 3.0;             // VDD discharge current in hiccup (mA)

IoPin@ vddPin   = component.getPin("VDD");
IoPin@ faultPin = component.getPin("FAULT");
IoPin@ gndPin   = component.getPin("GND");

enum state_t { OFF=0, ON, HICCUP };
state_t state;
string  why;

double rel( IoPin@ p ) { return p.getVoltage() - gndPin.getVoltage(); }

void setup() {}

void reset()
{
    vddPin.setPinMode( 1 ); faultPin.setPinMode( 1 ); gndPin.setPinMode( 1 );
    vddPin.changeCallBack( element, true );
    faultPin.changeCallBack( element, true );
    why = "";
    component.addEvent( 1000 );            // outputs once the linked sources are stamped
}

void runEvent() { enterOff(); voltChanged(); }

void enterOff()
{
    state = OFF;
    component.setLinkedValue( 0, I_STARTUP, 0 );
    component.setLinkedValue( 1, 0.0, 0 );
    if( why == "" ) component.showValue("OFF");
    else            component.showValue("OFF after "+why);
    component.setStatus( "UVLO", ST_OFF );                       // Part state on the canvas
}

void voltChanged()
{
    double vdd = rel( vddPin );
    if( state == OFF )
    {
        if( vdd >= V_STARTUP )                                   // start-up [DOC p.4 Fig.1]
        {
            state = ON;
            component.setLinkedValue( 0, I_Q, 0 );
            component.setLinkedValue( 1, V_EN, 0 );
            component.showValue("ON");
            component.setStatus( "ON", ST_ON );
        }
        return;
    }
    if( vdd < V_UVLO ) { enterOff(); return; }                  // UVLO [DOC p.5]

    if( state == ON && rel( faultPin ) > V_EN/2 )               // hiccup [DOC p.5]
    {
        state = HICCUP;
        why = "fault";
        component.setLinkedValue( 0, hiccupI*1e-3, 0 );
        component.setLinkedValue( 1, 0.0, 0 );
        component.showValue("HICCUP");
        component.setStatus( "HICCUP", ST_FAULT );
    }
}

void   setHiccup_mA( double v ) { if( v > 0 ) hiccupI = v; }
double getHiccup_mA()           { return hiccupI; }
