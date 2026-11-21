/***************************************************************************
 *   SimulIDE command line control interface                              *
 *   Application, circuit and simulation settings (the Settings dialog)    *
 ***( see copyright.txt file at root folder )*******************************/

#include <QJsonArray>
#include <QMetaEnum>

#include "instrumentview.h"
#include "uitheme.h"
#include "cliapi.h"
#include "simskip.h"
#include "circuitwidget.h"
#include "mainwindow.h"
#include "circuitwidget.h"
#include "circuit.h"
#include "circuitview.h"
#include "simulator.h"
#include "currentflow.h"
#include "chargeview.h"
#include "statusview.h"
#include "eventlog.h"
#include "theme.h"

namespace
{
// A setter's remark for the reply (e.g. the speed lowered to keep a step): BH-050
QString s_note;

struct Setting
{
    QString name;
    QString group;
    QString help;
    std::function<QJsonValue()> get;
    std::function<void(QString)> set;   // Empty: read only
};

const QStringList langNames = { "English","Azerbaijani","Chinese","Czech","Dutch","French","German",
                                "Italian","Russian","Spanish","Portuguese","Pt_Brasil","Slovak","Turkish"};

bool parseBool( QString v )
{
    v = v.toLower();
    if( v == "true" || v == "1" || v == "on"  || v == "yes" ) return true;
    if( v == "false"|| v == "0" || v == "off" || v == "no"  ) return false;
    throw CliError("expected true/false: "+v );
}

int parseInt( QString v, QString what, int min, int max )
{
    double d = Cli::parseNum( v, what );
    if( d < min || d > max ) throw CliError( QString("%1 must be %2..%3").arg( what ).arg( min ).arg( max ) );
    return (int)d;
}

// "10MHz", "10M", "1e7", "2.5 kHz" -> number (SI prefixes p n u µ m k M G T)
double parseSi( QString v, QString what, QString unit )
{
    bool ok = false;
    double d = Simulator::parseSi( v, unit, &ok );
    if( !ok ) throw CliError("invalid number for "+what+": "+v );
    return d;
}

// Noise settings apply at the next start (docs/NOISE_SPEC.md)
void noiseChanged()
{
    Circuit::self()->setChanged();
    CircuitWidget::self()->updateNoise();
    if( Simulator::self()->isRunning() ) s_note = "noise settings apply at the next sim start";
}

QList<Setting>& settings()
{
    static QList<Setting> list;
    if( !list.isEmpty() ) return list;

    // App -------------------------------------------------------------------
    list.append({"language", "app", "User interface language (applies after restart): "+langNames.join(", "),
        [](){ return QJsonValue( langNames.value( (int)MainWindow::self()->lang() ) ); },
        []( QString v ){
            int i = -1;
            for( int n=0; n<langNames.size(); ++n ) if( langNames[n].compare( v, Qt::CaseInsensitive ) == 0 ) i = n;
            if( i < 0 ) throw CliError("unknown language: "+v+" ("+langNames.join(", ")+")");
            MainWindow::self()->setLang( (Langs)i );
            CliApi::self()->addMessage("language applies after restart");
        }});
    list.append({"fontScale", "app", "Font scale (applies after restart).",
        [](){ return QJsonValue( MainWindow::self()->fontScale() ); },
        []( QString v ){
            double s = Cli::parseNum( v, "fontScale");
            if( s < 0.5 || s > 3 ) throw CliError("fontScale must be 0.5..3");
            MainWindow::self()->setFontScale( s );
            CliApi::self()->addMessage("font scale applies after restart");
        }});
    list.append({"fontName", "app", "Default font family (applies after restart).",
        [](){ return QJsonValue( MainWindow::self()->defaultFontName() ); },
        []( QString v ){
            MainWindow::self()->setDefaultFontName( v );
            CliApi::self()->addMessage("font applies after restart");
        }});
    list.append({"reopenLast", "app", "On start, reopen the circuit that was open when the app was closed (default true).",
        [](){ return QJsonValue( MainWindow::self()->reopenLast() ); },
        []( QString v ){ MainWindow::self()->setReopenLast( parseBool( v ) ); }});
    list.append({"uiTheme", "app", "Look of the windows and panels: system (follow the OS light/dark mode, default), light or dark.",
        [](){ return QJsonValue( UiTheme::modeName( UiTheme::self()->mode() ) ); },
        []( QString v ){
            UiTheme::mode_t m;
            if( !UiTheme::parseMode( v, m ) ) throw CliError("uiTheme must be system, light or dark");
            UiTheme::self()->setMode( m );
        }});
    list.append({"userPath", "app", "User data folder (extra components, examples).",
        [](){ return QJsonValue( MainWindow::self()->userPath() ); },
        []( QString v ){
            QString p = CliApi::self()->resolvePath( v );
            if( !QFileInfo::exists( p ) ) throw CliError("folder not found: "+p );
            MainWindow::self()->setUserPath( p );
        }});

    // Circuit ---------------------------------------------------------------
    list.append({"drawGrid", "circuit", "Draw the grid.",
        [](){ return QJsonValue( Circuit::self()->drawGrid() ); },
        []( QString v ){ Circuit::self()->setDrawGrid( parseBool( v ) ); }});
    list.append({"showScroll", "circuit", "Show scroll bars.",
        [](){ return QJsonValue( CircuitView::self()->showScroll() ); },
        []( QString v ){ CircuitView::self()->setShowScroll( parseBool( v ) ); }});
    list.append({"animate", "circuit", "Animate (show pin states and wire voltages).",
        [](){ return QJsonValue( Circuit::self()->animate() ); },
        []( QString v ){ Circuit::self()->setAnimate( parseBool( v ) ); }});
    list.append({"currentFlow", "circuit", "Show current flow: moving dots on the wires (see also wire current).",
        [](){ return QJsonValue( CurrentFlow::self()->enabled() ); },
        []( QString v ){ CurrentFlow::self()->setEnabled( parseBool( v ) ); }});
    list.append({"currentMin", "circuit", "Smallest current the flow dots show (e.g. 1u, 100n).",
        [](){ return QJsonValue( CurrentFlow::currentStr( CurrentFlow::self()->threshold() ) ); },
        []( QString v ){
            double amps;
            if( !CurrentFlow::parseCurrent( v, amps ) || amps <= 0 ) throw CliError("invalid current: "+v+" (e.g. 1u, 100n)" );
            CurrentFlow::self()->setThreshold( amps );
        }});
    list.append({"currentRange", "circuit", "Flow dots: decades of current shown below the largest one, 1..6 (default 2). Fewer = more contrast.",
        [](){ return QJsonValue( CurrentFlow::range() ); },
        []( QString v ){
            bool ok; double d = v.toDouble( &ok );
            if( !ok || d < 1 || d > 6 ) throw CliError("invalid range: "+v+" (1..6 decades)" );
            CurrentFlow::self()->setRange( d );
        }});
    list.append({"capCharge", "circuit", "Show capacitor charge and inductor current: a gauge beside each capacitor, inductor and transformer coil; plates coloured by voltage, coils by current (see also read Cx / Lx / Tx).",
        [](){ return QJsonValue( ChargeView::self()->enabled() ); },
        []( QString v ){ ChargeView::self()->setEnabled( parseBool( v ) ); }});
    list.append({"partStatus", "circuit", "Show part states: badges above each part that reports its state (scripted cores: component.setStatus) and its outline in the most severe state's colour (see also: status).",
        [](){ return QJsonValue( StatusView::self()->enabled() ); },
        []( QString v ){ StatusView::self()->setEnabled( parseBool( v ) ); }});
    list.append({"instruments", "circuit", "Show instruments: scopes, logic analyzers, voltmeters, frequency meters and probes with their leads, lead tunnels and junctions. Off: the circuit alone; they keep measuring (scope data, read).",
        [](){ return QJsonValue( InstrumentView::self()->shown() ); },
        []( QString v ){ InstrumentView::self()->setShown( parseBool( v ) ); }});
    list.append({"logParts", "circuit", "Messages: write part state changes with their simulated time (see also: log).",
        [](){ return QJsonValue( EventLog::self()->logParts() ); },
        []( QString v ){ EventLog::self()->setLogParts( parseBool( v ) ); }});
    list.append({"logCaps", "circuit", "Messages: write charge events of capacitors whose LogCharge property is on (see also: log).",
        [](){ return QJsonValue( EventLog::self()->logCaps() ); },
        []( QString v ){ EventLog::self()->setLogCaps( parseBool( v ) ); }});
    list.append({"logVerbose", "circuit", "Messages: also show debug logs (qDebug lines starting with a [tag]); off shows only information, warnings and errors. Debug logs always go to stderr.",
        [](){ return QJsonValue( CircuitWidget::verboseLog() ); },
        []( QString v ){ CircuitWidget::self()->setVerboseLog( parseBool( v ) ); }});
    list.append({"capLabel", "circuit", "Live label beside each capacitor, inductor and transformer coil while the display is on: off, volt (the value: V or A), percent or both.",
        [](){ return QJsonValue( ChargeView::labelModes().at( ChargeView::self()->labelMode() ) ); },
        []( QString v ){
            int i = ChargeView::labelModes().indexOf( v.toLower() );
            if( i < 0 ) throw CliError("invalid label mode: "+v+" ("+ChargeView::labelModes().join(", ")+")" );
            ChargeView::self()->setLabelMode( i );
        }});
    list.append({"chargeScale", "circuit", "Charge display 100 % in volts, saved in the circuit; 0 = auto (largest source voltage).",
        [](){ return QJsonValue( Circuit::self()->chargeScale() ); },
        []( QString v ){
            bool ok; double d = v.toDouble( &ok );
            if( !ok || d < 0 ) throw CliError("invalid voltage: "+v+" (0 = auto)" );
            Circuit::self()->setChargeScale( d );
        }});
    list.append({"currentScale", "circuit", "Inductor and transformer current gauges 100 % in amps, saved in the circuit; 0 = auto (peak current since the start).",
        [](){ return QJsonValue( Circuit::self()->currentScale() ); },
        []( QString v ){
            bool ok; double d = v.toDouble( &ok );
            if( !ok || d < 0 ) throw CliError("invalid current: "+v+" (0 = auto)" );
            Circuit::self()->setCurrentScale( d );
        }});
    list.append({"colorTheme", "circuit", "Color preset: System (Light or Dark, following the OS appearance), Classic, Light or Dark (EveryCircuit style). Reads Custom when colors were changed.",
        [](){ return QJsonValue( Theme::currentPreset() ); },
        []( QString v ){
            if( !Theme::applyPreset( v ) ) throw CliError("unknown color theme: "+v+" ("+Theme::choices().join(", ")+")");
        }});
    for( int i=0; i<Theme::numRoles; ++i )
    {
        Theme::role_t role = (Theme::role_t)i;
        static const char* help[] = {
            "Canvas (background) color: #rrggbb, a color name, or default.",
            "Wire color: #rrggbb, a color name, or default.",
            "Component outline color (lines, pins, arrowheads): #rrggbb, a color name, or default.",
            "Text inside components (pin names): #rrggbb, a color name, or default.",
            "Component body (fill) color, for parts drawn white by default: #rrggbb, a color name, or default.",
            "Current flow dot color: #rrggbb, a color name, or default.",
            "Component name label color (R1, U1): #rrggbb, a color name, or default.",
            "Component value label color (470 Ω): #rrggbb, a color name, or default.",
            "Highlight color (keyboard cursor cross, selected pins): #rrggbb, a color name, or default.",
            "IC body color (chips, subcircuits, MCUs without their own background): #rrggbb, a color name, or default (each kind keeps its classic shade).",
            "Text inside ICs (pin names) color; the pin-1 marker is derived from it and the IC body: #rrggbb, a color name, or default." };
        QString desc = ( i < Theme::firstAnimRole ) ? QString( help[i] )
                     : "Animate color, "+Theme::caption( role ).toLower()+": #rrggbb, a color name, or default.";
        list.append({ Theme::key( role ), "circuit", desc,
            [role](){ return QJsonValue( Theme::get( role ).name() ); },
            [role]( QString v ){
                if( v.toLower() == "default" ) { Theme::set( role, Theme::defaultColor( role ) ); return; }
                QColor c( v );
                if( !c.isValid() ) throw CliError("invalid color: "+v+" (e.g. #202830, white, default)");
                Theme::set( role, c );
            }});
    }
    list.append({"fps", "circuit", "Screen refresh rate, frames per second.",
        [](){ return QJsonValue( (int)Simulator::self()->fps() ); },
        []( QString v ){ Simulator::self()->setFps( parseInt( v, "fps", 1, 100 ) ); }});
    list.append({"autoBackup", "circuit", "Auto backup interval in seconds (0 = off).",
        [](){ return QJsonValue( Circuit::self()->autoBck() ); },
        []( QString v ){ Circuit::self()->setAutoBck( parseInt( v, "autoBackup", 0, 86400 ) ); }});

    // Simulation (saved in the circuit file) --------------------------------
    list.append({"speed", "simulation", "Target speed in % of real time (also: sim speed).",
        [](){ return QJsonValue( 100.0*(double)Simulator::self()->psPerSec()/1e12 ); },
        []( QString v ){
            double pct = Cli::parseNum( v, "speed");
            if( pct <= 0 ) throw CliError("speed must be > 0");
            s_note = Cli::setSimSpeed( (uint64_t)( pct*1e10 ) );
            Circuit::self()->setChanged();
        }});
    list.append({"stepSize", "simulation", "Simulation step size (e.g. 1us).",
        [](){ return QJsonValue( Cli::timeStr( Simulator::self()->stepSize() ) ); },
        []( QString v ){
            uint64_t step = Cli::parseTime( v );
            if( step == 0 ) throw CliError("stepSize must be > 0");
            s_note = Cli::setSimSpeed( Simulator::self()->psPerSec(), step, true );
            Circuit::self()->setChanged();
        }});
    list.append({"stepsPerSec", "simulation", "Simulation steps per real second (follows from speed and stepSize).",
        [](){ return QJsonValue( QString::number( Simulator::self()->stepsPerSec() ) ); },
        nullptr });
    list.append({"reactStep", "simulation", "Reactive components step (e.g. 1us).",
        [](){ return QJsonValue( Cli::timeStr( Simulator::self()->reactStep() ) ); },
        []( QString v ){
            uint64_t step = Cli::parseTime( v );
            if( step == 0 ) throw CliError("reactStep must be > 0");
            Simulator::self()->setreactStep( step );
            Circuit::self()->setChanged();
        }});
    list.append({"nlSteps", "simulation", "Maximum non-linear iterations per step.",
        [](){ return QJsonValue( (int)Simulator::self()->maxNlSteps() ); },
        []( QString v ){
            Simulator::self()->setMaxNlSteps( parseInt( v, "nlSteps", 1, 1000000000 ) );
            Circuit::self()->setChanged();
        }});
    list.append({"supplyRamp", "simulation", "DC sources (Battery, Rail, Fixed Voltage) rise from 0 over this time at each start (0 = a step; e.g. 1ms). Saved with the circuit.",
        [](){ return QJsonValue( Cli::timeStr( Simulator::self()->supplyRamp() ) ); },
        []( QString v ){ Simulator::self()->setSupplyRamp( Cli::parseTime( v ) ); }});
    list.append({"skipInterval", "simulation", "Skip interval of the arrow keys and the toolbar (step = one solver step, or 1ps … 1min). Saved with the circuit.",
        [](){ return QJsonValue( SimSkip::intervalText( Simulator::self()->skipInterval() ) ); },
        []( QString v ){
            uint64_t ps = 0;
            if( !SimSkip::parseInterval( v, &ps ) ) throw CliError("skipInterval: step, or 1ps … 1min (e.g. 5us)");
            CircuitWidget::self()->setSkipInterval( ps );
        }});
    list.append({"slopeSteps", "simulation", "Steps used for signal slopes (0 = instant edges).",
        [](){ return QJsonValue( Simulator::self()->slopeSteps() ); },
        []( QString v ){
            Simulator::self()->setSlopeSteps( parseInt( v, "slopeSteps", 0, 1000000 ) );
            Circuit::self()->setChanged();
        }});

    // Transient noise (docs/NOISE_SPEC.md) ------------------------------------
    list.append({"noise", "simulation", "Transient noise of the parts (thermal, shot, channel) on or off.",
        [](){ return QJsonValue( Simulator::self()->noise().on ); },
        []( QString v ){ Simulator::self()->noise().on = parseBool( v ); noiseChanged(); }});
    list.append({"noiseBW", "simulation", "Noise bandwidth fmax (e.g. 10MHz): sources redrawn every 1/(2·fmax).",
        [](){ return QJsonValue( Simulator::siStr( Simulator::self()->noise().bw, "Hz" ).remove(' ') ); },
        []( QString v ){
            double f = parseSi( v, "noiseBW", "Hz" );
            if( !(f > 0) || f > 5e11 ) throw CliError("noiseBW must be > 0 and <= 500 GHz");
            Simulator::self()->noise().bw = f; noiseChanged();
        }});
    list.append({"noiseScale", "simulation", "Multiplies every noise current (1 = physical).",
        [](){ return QJsonValue( Simulator::self()->noise().scale ); },
        []( QString v ){
            double k = Cli::parseNum( v, "noiseScale" );
            if( !(k >= 0 && k <= 1e12) ) throw CliError("noiseScale must be 0..1e12");
            Simulator::self()->noise().scale = k; noiseChanged();
        }});
    list.append({"noiseSeed", "simulation", "Noise seed: 0 = random at every start (the seed used is in sim status).",
        [](){ return QJsonValue( QString::number( Simulator::self()->noise().seed ) ); },
        []( QString v ){
            bool ok = false;
            qulonglong s = v.trimmed().toULongLong( &ok );
            if( !ok ) throw CliError("noiseSeed must be an integer >= 0: "+v );
            Simulator::self()->noise().seed = s; noiseChanged();
        }});
    list.append({"noiseStart", "simulation", "Noise on from this simulated time (e.g. 0, 100us).",
        [](){ return QJsonValue( Cli::timeStr( Simulator::self()->noise().start ) ); },
        []( QString v ){ Simulator::self()->noise().start = Cli::parseTime( v ); noiseChanged(); }});
    list.append({"noiseStop", "simulation", "Noise off from this simulated time (0 = until the end; 1ms = start-up only).",
        [](){ return QJsonValue( Cli::timeStr( Simulator::self()->noise().stop ) ); },
        []( QString v ){ Simulator::self()->noise().stop = Cli::parseTime( v ); noiseChanged(); }});
    list.append({"noiseOff", "simulation", "Parts without noise: labels and type:NAME, comma-separated (none = empty).",
        [](){ return QJsonValue( Simulator::self()->noise().off.join(",") ); },
        []( QString v ){
            QStringList l;
            if( v.trimmed().compare("none", Qt::CaseInsensitive ) != 0 )
                for( QString e : v.split(",", QString::SkipEmptyParts ) ) if( !e.trimmed().isEmpty() ) l.append( e.trimmed() );
            Simulator::self()->noise().off = l; noiseChanged();
        }});
    return list;
}

Setting& findSetting( QString name )
{
    for( Setting& s : settings() ) if( s.name.compare( name, Qt::CaseInsensitive ) == 0 ) return s;
    QStringList names;
    for( Setting& s : settings() ) names.append( s.name );
    throw CliError("unknown setting: "+name+" ("+names.join(", ")+")");
}

QJsonObject settingJson( Setting& s )
{
    QJsonObject o{
        {"name",  s.name },
        {"group", s.group },
        {"value", s.get() },
        {"help",  s.help } };
    if( !s.set ) o["readOnly"] = true;
    return o;
}
}

void CliApi::registerSettings()
{
    add("settings list", "settings list [--group app|circuit|simulation]", "All settings with their current values.",
        []( CliCtx& c ) -> QJsonValue
    {
        QString group = c.opt("group");
        QJsonArray rows;
        for( Setting& s : settings() )
            if( group.isEmpty() || s.group == group ) rows.append( settingJson( s ) );
        return rows;
    });

    add("settings get", "settings get <name>", "Value of one setting.",
        []( CliCtx& c ) -> QJsonValue { return findSetting( c.arg( 0, "name") ).get(); });

    add("settings set", "settings set <name> <value> | <name>=<value>...", "Change settings.",
        []( CliCtx& c ) -> QJsonValue
    {
        QList<QPair<QString,QString>> changes;
        if( c.pos.size() >= 2 ) changes.append({ c.pos.at(0), c.pos.mid(1).join(" ") });
        else if( c.pos.size() == 1 ) throw CliError("missing value for "+c.pos.at(0) );
        for( QString k : c.sets.keys() ) changes.append({ k, c.sets.value( k ).toString() });
        if( changes.isEmpty() ) throw CliError("nothing to set (usage: settings set <name> <value>)");

        // Validate names first so nothing is half applied on a typo
        for( auto& ch : changes )
            if( !findSetting( ch.first ).set ) throw CliError( ch.first+" is read only");

        QJsonArray rows;
        for( auto& ch : changes )
        {
            Setting& s = findSetting( ch.first );
            s_note.clear();
            s.set( ch.second );
            QJsonObject row = settingJson( s );
            if( !s_note.isEmpty() ) row["note"] = s_note;
            rows.append( row );
        }
        return rows;
    });
}
