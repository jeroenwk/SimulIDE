/***************************************************************************
 *   SimulIDE command line control interface                              *
 *   Measurement and interaction commands                                  *
 ***( see copyright.txt file at root folder )*******************************/

#include "capacitorbase.h"
#include "chargeview.h"
#include <QGraphicsProxyWidget>
#include <QAbstractButton>
#include <QAbstractSlider>
#include <cmath>

#include "cliapi.h"
#include "clirefs.h"
#include "circuit.h"
#include "component.h"
#include "pin.h"
#include "simulator.h"
#include "meter.h"
#include "probe.h"
#include "freqmeter.h"
#include "e-led.h"
#include "e-resistor.h"
#include "plotbase.h"
#include "datachannel.h"
#include "oscope.h"
#include "logicanalizer.h"

namespace
{
void requireSim()
{
    if( !Simulator::self()->isRunning() )
        throw CliError("simulation is not running: use sim start or sim run --for <time> first");
}

QJsonValue num( double v ) { return std::isfinite( v ) ? QJsonValue( v ) : QJsonValue(); }

QJsonObject readComp( Component* comp )
{
    QJsonObject o{ {"uid", comp->getUid() }, {"type", comp->itemType() } };

    if( Meter* m = dynamic_cast<Meter*>( comp ) )
    {
        o["value"] = num( m->value() );
        o["unit"]  = m->unit();
    }
    else if( Probe* p = dynamic_cast<Probe*>( comp ) )
    {
        o["value"] = num( p->voltage() );
        o["unit"]  = "V";
        o["high"]  = p->voltage() >= p->threshold();
    }
    else if( FreqMeter* f = dynamic_cast<FreqMeter*>( comp ) )
    {
        o["value"] = num( f->frequency() );
        o["unit"]  = "Hz";
    }
    else if( eLed* led = dynamic_cast<eLed*>( comp ) )
    {
        o["brightness"] = num( led->brightness() );
        o["current"]    = num( led->overCurrent()*led->maxCurrent() );
        o["overloaded"] = led->overCurrent() > 1.5;
    }
    else if( CapacitorBase* cap = dynamic_cast<CapacitorBase*>( comp ) )
    {
        ChargeView* cv = ChargeView::self();
        o["voltage"]     = num( cap->capVolt() );
        o["level"]       = num( cap->chargeLevel() );
        o["vref"]        = num( cv->vref() );
        o["vref_source"] = ChargeView::refSourceStr( cv->refSource() );
        if( cap->maxVolt() > 0 ) o["rating"] = num( cap->maxVolt() );
        QStringList flags = cap->chargeFlags();
        if( !flags.isEmpty() ) o["flag"] = flags.join(",");
    }
    else if( comp->itemType() == "Resistor" )
    {
        eResistor* r = dynamic_cast<eResistor*>( comp );
        std::vector<Pin*> pins = comp->getPins();
        if( r && pins.size() == 2 && pins[0]->isConnected() && pins[1]->isConnected() && r->res() > 0 )
        {
            double v = pins[0]->getVoltage() - pins[1]->getVoltage();
            o["voltage"] = num( v );
            o["current"] = num( v/r->res() );
            o["power"]   = num( v*v/r->res() );
    }   }

    QJsonObject volts;
    std::vector<Pin*> pins = comp->getPins();
    for( Pin* pin : pins ) if( pin && pin->isConnected() )
    {
        QString ref = CliRefs::pinRef( pin );
        volts[ref.mid( ref.lastIndexOf('.')+1 )] = num( pin->getVoltage() );
    }
    if( !volts.isEmpty() ) o["pins"] = volts;
    return o;
}

//--- Interactive widgets embedded in components ---------------------------

struct Control { QWidget* widget; QAbstractButton* button; QAbstractSlider* slider; };

void collectControls( QGraphicsItem* item, QList<Control>& out )
{
    for( QGraphicsItem* child : item->childItems() )
    {
        if( QGraphicsProxyWidget* proxy = dynamic_cast<QGraphicsProxyWidget*>( child ) )
        {
            QWidget* w = proxy->widget();
            if( !w ) continue;
            QList<QWidget*> widgets{ w };
            widgets += w->findChildren<QWidget*>();
            for( QWidget* x : widgets )
            {
                if( QAbstractButton* b = qobject_cast<QAbstractButton*>( x ) ) out.append( { x, b, nullptr } );
                else if( QAbstractSlider* s = qobject_cast<QAbstractSlider*>( x ) ) out.append( { x, nullptr, s } );
        }   }
        collectControls( child, out );   // Child components (e.g. keypad buttons)
}   }

QList<Control> controls( Component* comp )
{
    QList<Control> list;
    collectControls( comp, list );
    return list;
}

QJsonObject controlJson( const Control& c, int i )
{
    QJsonObject o{ {"index", i } };
    if( c.button ){
        o["kind"] = c.button->isCheckable() ? "toggle" : "button";
        if( !c.button->text().isEmpty() ) o["text"] = c.button->text();
        if( c.button->isCheckable() ) o["checked"] = c.button->isChecked();
        o["down"] = c.button->isDown();
    }else{
        o["kind"]  = "dial";
        o["value"] = c.slider->value();
        o["min"]   = c.slider->minimum();
        o["max"]   = c.slider->maximum();
    }
    return o;
}

Control control( Component* comp, QString idx, bool wantButton )
{
    QList<Control> list = controls( comp );
    QList<Control> ofKind;
    for( Control& c : list ) if( wantButton ? c.button != nullptr : c.slider != nullptr ) ofKind.append( c );
    if( ofKind.isEmpty() )
        throw CliError( comp->getUid()+" has no "+( wantButton ? "button/switch" : "dial/slider" )+" control (see: ui controls "+comp->getUid()+")" );

    int i = 0;
    if( !idx.isEmpty() ){
        bool ok;
        i = idx.toInt( &ok );
        if( !ok || i < 0 || i >= ofKind.size() )
            throw CliError( QString("control index must be 0..%1").arg( ofKind.size()-1 ) );
    }
    return ofKind.at( i );
}

QJsonObject compControls( Component* comp )
{
    QJsonArray arr;
    QList<Control> list = controls( comp );
    for( int i=0; i<list.size(); ++i ) arr.append( controlJson( list.at( i ), i ) );
    return QJsonObject{ {"uid", comp->getUid() }, {"controls", arr } };
}
}

namespace
{
QJsonObject scopeSettings( PlotBase* plot )
{
    QJsonObject o{
        {"uid",      dynamic_cast<Component*>( plot )->getUid() },
        {"timeDiv",  Cli::timeStr( plot->timeDiv() ) },
        {"trigger",  plot->trigger() > 3 ? QJsonValue("none") : QJsonValue( plot->trigger() ) } };

    if( Oscope* osc = dynamic_cast<Oscope*>( plot ) )
    {
        QStringList timePos = osc->timPos().split(',');
        QJsonArray chans;
        for( int i=0; i<4; ++i )
            chans.append( QJsonObject{
                {"ch",      i },
                {"voltDiv", osc->voltDiv( i ) },
                {"voltPos", osc->voltPos( i ) },
                {"timePos", Cli::timeStr( (uint64_t)qAbs( timePos.value( i ).toLongLong() ) ).prepend( timePos.value( i ).startsWith('-') ? "-":"" ) } } );
        o["channels"] = chans;
        o["autoScale"] = osc->autoSC() > 3 ? QJsonValue("none") : QJsonValue( osc->autoSC() );
    }
    else if( LAnalizer* la = dynamic_cast<LAnalizer*>( plot ) )
    {
        o["voltDiv"] = la->volDiv().toDouble();
        o["timePos"] = Cli::timeStr( (uint64_t)qAbs( la->timePos() ) ).prepend( la->timePos() < 0 ? "-":"" );
    }
    return o;
}

// "none" or a channel number 0..3
int parseChannel( QString v, QString what )
{
    if( v.toLower() == "none" || v.toLower() == "off" ) return 4;
    bool ok;
    int ch = v.toInt( &ok );
    if( !ok || ch < 0 || ch > 3 ) throw CliError( what+" must be a channel 0..3 (Pin0..Pin3) or none");
    return ch;
}

int64_t parseSignedTime( QString v )
{
    v = v.trimmed();
    bool neg = v.startsWith('-');
    int64_t t = (int64_t)Cli::parseTime( neg ? v.mid( 1 ) : v );
    return neg ? -t : t;
}

// Per channel values: "5" (the --ch channel, or all) or "5,5,1,1"
QList<QPair<int,QString>> perChannel( CliCtx& c, QString key )
{
    QList<QPair<int,QString>> out;
    QStringList vals = c.opt( key ).split(',', QString::SkipEmptyParts );
    if( vals.size() > 1 ){
        for( int i=0; i<vals.size() && i<4; ++i ) out.append({ i, vals[i] });
    }else if( c.has("ch") ) out.append({ parseChannel( c.opt("ch"), "--ch"), vals.value( 0 ) });
    else for( int i=0; i<4; ++i ) out.append({ i, vals.value( 0 ) });
    return out;
}
}

void CliApi::registerMeasure()
{
    //--- Measure -------------------------------------------------------------

    add("probe", "probe <pin> [pin ...]", "Voltage at pins (simulation must be running or paused).",
        []( CliCtx& c ) -> QJsonValue
    {
        if( c.pos.isEmpty() ) throw CliError("missing pin (e.g. R1.lPin)");
        requireSim();
        QJsonArray list;
        for( QString ref : c.pos )
        {
            Pin* pin = CliRefs::pin( ref );
            QJsonObject o{ {"ref", CliRefs::pinRef( pin ) } };
            if( pin->isConnected() )
            {
                o["voltage"] = num( pin->getVoltage() );
                o["current"] = num( pin->getCurrent() );   // Into the component (A)
            }
            else                     o["voltage"] = QJsonValue();   // Not connected to any node
            list.append( o );
        }
        return list;
    });

    add("read", "read <comp> [comp ...]", "Readout of components: meters, probes, LEDs, resistors (current, power) and pin voltages.",
        []( CliCtx& c ) -> QJsonValue
    {
        if( c.pos.isEmpty() ) throw CliError("missing component");
        requireSim();
        if( c.pos.size() == 1 ) return readComp( CliRefs::comp( c.pos.at(0) ) );
        QJsonArray list;
        for( QString ref : c.pos ) list.append( readComp( CliRefs::comp( ref ) ) );
        return list;
    });

    add("scope get", "scope get <comp>",
        "Display settings of an oscilloscope or logic analyzer: time/div, trigger, and per channel volts/div and positions.",
        []( CliCtx& c ) -> QJsonValue
    {
        Component* comp = CliRefs::comp( c.arg( 0, "oscilloscope or logic analyzer") );
        PlotBase* plot = dynamic_cast<PlotBase*>( comp );
        if( !plot ) throw CliError( comp->getUid()+" is not an oscilloscope or logic analyzer");
        return scopeSettings( plot );
    });

    add("scope set", "scope set <comp> [--timediv T] [--trigger CH|none] [--voltdiv V|a,b,c,d] [--voltpos V|a,b,c,d] "
        "[--timepos T|a,b,c,d] [--ch CH] [--autoscale CH|none]",
        "Change display settings, as in the Expand panel. Channels are 0..3 (Pin0..Pin3). A single --voltdiv/--voltpos/--timepos "
        "value applies to --ch, or to all channels; a comma list sets each channel. Example: scope set SCOPE --timediv 5ms --voltdiv 5 --trigger 0",
        []( CliCtx& c ) -> QJsonValue
    {
        Component* comp = CliRefs::comp( c.arg( 0, "oscilloscope or logic analyzer") );
        PlotBase* plot = dynamic_cast<PlotBase*>( comp );
        if( !plot ) throw CliError( comp->getUid()+" is not an oscilloscope or logic analyzer");
        Oscope*    osc = dynamic_cast<Oscope*>( plot );
        LAnalizer* la  = dynamic_cast<LAnalizer*>( plot );

        // Validate everything before changing anything
        uint64_t timeDiv = 0;
        if( c.has("timediv") ){
            timeDiv = Cli::parseTime( c.opt("timediv") );
            if( timeDiv == 0 ) throw CliError("--timediv must be > 0 (e.g. 5ms)");
        }
        int trigger = c.has("trigger") ? parseChannel( c.opt("trigger"), "--trigger") : -1;
        int autoSc  = c.has("autoscale") ? parseChannel( c.opt("autoscale"), "--autoscale") : -1;
        if( autoSc >= 0 && !osc ) throw CliError("--autoscale is only for the oscilloscope");

        QList<QPair<int,double>> voltDiv, voltPos;
        QList<QPair<int,int64_t>> timePos;
        if( c.has("voltdiv") ) for( auto& p : perChannel( c, "voltdiv") ){
            double v = Cli::parseNum( p.second, "voltdiv");
            if( v <= 0 ) throw CliError("--voltdiv must be > 0");
            voltDiv.append({ p.first, v });
        }
        if( c.has("voltpos") ){
            if( !osc ) throw CliError("--voltpos is only for the oscilloscope");
            for( auto& p : perChannel( c, "voltpos") ) voltPos.append({ p.first, Cli::parseNum( p.second, "voltpos") });
        }
        if( c.has("timepos") ) for( auto& p : perChannel( c, "timepos") ) timePos.append({ p.first, parseSignedTime( p.second ) });
        if( !c.has("timediv") && trigger < 0 && autoSc < 0 && voltDiv.isEmpty() && voltPos.isEmpty() && timePos.isEmpty() )
            throw CliError("nothing to set (see: help scope set)");

        if( timeDiv ) plot->setTimeDiv( timeDiv );
        if( trigger >= 0 ) plot->setTrigger( trigger );
        if( osc ){
            if( autoSc >= 0 ) osc->setAutoSC( autoSc );
            for( auto& p : voltDiv ) osc->setVoltDiv( p.first, p.second );
            for( auto& p : voltPos ) osc->setVoltPos( p.first, p.second );
            for( auto& p : timePos ) osc->setTimePos( p.first, p.second );
        }else if( la ){
            if( !voltDiv.isEmpty() ) la->setVolDiv( QString::number( voltDiv.first().second ) );
            if( !timePos.isEmpty() ) la->setTimePos( timePos.first().second );
        }
        Circuit::self()->setChanged();
        comp->update();
        return scopeSettings( plot );
    });

    add("scope data", "scope data <comp> [--ch N] [--last TIME] [--max N]",
        "Samples captured by an oscilloscope or logic analyzer: [time_s, value] per channel.",
        []( CliCtx& c ) -> QJsonValue
    {
        Component* comp = CliRefs::comp( c.arg( 0, "oscilloscope or logic analyzer") );
        PlotBase* plot = dynamic_cast<PlotBase*>( comp );
        if( !plot ) throw CliError( comp->getUid()+" is not an oscilloscope or logic analyzer");

        int onlyCh = c.has("ch") ? (int)Cli::parseNum( c.opt("ch"), "--ch") : -1;
        int maxPts = c.has("max") ? (int)Cli::parseNum( c.opt("max"), "--max") : 2000;
        uint64_t now = Simulator::self()->isRunning() ? Simulator::self()->circTime() : 0;
        uint64_t from = 0;
        if( c.has("last") ){
            uint64_t span = Cli::parseTime( c.opt("last") );
            from = ( now > span ) ? now-span : 0;
        }
        QJsonArray channels;
        for( int ch=0; ch<plot->numChannels(); ++ch )
        {
            if( onlyCh >= 0 && ch != onlyCh ) continue;
            DataChannel* dc = plot->channel( ch );
            if( !dc ) continue;

            QVector<double>   vals  = dc->buffer();     // Copies: snapshot of the ring buffer
            QVector<uint64_t> times = dc->times();
            int size = qMin( vals.size(), times.size() );
            int newest = dc->bufferIndex();

            QVector<int> order;                          // Oldest to newest
            for( int k=1; k<=size; ++k ){
                int i = ( newest+k ) % size;
                if( times[i] == 0 || times[i] < from || ( now && times[i] > now ) ) continue;
                order.append( i );
            }
            int step = ( maxPts > 0 && order.size() > maxPts ) ? ( order.size()+maxPts-1 )/maxPts : 1;
            QJsonArray samples;
            for( int k=0; k<order.size(); k+=step )
                samples.append( QJsonArray{ (double)times[order[k]]/1e12, vals[order[k]] } );

            channels.append( QJsonObject{
                {"channel",   ch },
                {"connected", dc->connected() },
                {"samples",   samples } } );
        }
        return QJsonObject{ {"uid", comp->getUid() }, {"time", (double)now/1e12 }, {"channels", channels } };
    });

    //--- Interact ------------------------------------------------------------

    add("ui controls", "ui controls <comp>", "Buttons, switches and dials of a component, with their state.",
        []( CliCtx& c ) -> QJsonValue
    {
        return compControls( CliRefs::comp( c.arg( 0, "component") ) );
    });

    add("ui press", "ui press <comp> [index]", "Press and hold a button (until ui release).",
        []( CliCtx& c ) -> QJsonValue
    {
        Component* comp = CliRefs::comp( c.arg( 0, "component") );
        QAbstractButton* b = control( comp, c.argOr( 1, ""), true ).button;
        b->setDown( true );
        emit b->pressed();
        return compControls( comp );
    });

    add("ui release", "ui release <comp> [index]", "Release a pressed button.",
        []( CliCtx& c ) -> QJsonValue
    {
        Component* comp = CliRefs::comp( c.arg( 0, "component") );
        QAbstractButton* b = control( comp, c.argOr( 1, ""), true ).button;
        b->setDown( false );
        emit b->released();
        return compControls( comp );
    });

    add("ui click", "ui click <comp> [index]", "Click a button or toggle a switch (press + release).",
        []( CliCtx& c ) -> QJsonValue
    {
        Component* comp = CliRefs::comp( c.arg( 0, "component") );
        control( comp, c.argOr( 1, ""), true ).button->click();
        return compControls( comp );
    });

    add("ui set", "ui set <comp> [index] <on|off>", "Set a switch/toggle to a state.",
        []( CliCtx& c ) -> QJsonValue
    {
        Component* comp = CliRefs::comp( c.arg( 0, "component") );
        QString state = c.pos.last().toLower();
        QString idx = c.pos.size() > 2 ? c.pos.at( 1 ) : "";
        if( c.pos.size() < 2 ) throw CliError("missing state: on or off");
        bool on;
        if     ( state == "on"  || state == "1" || state == "true"  || state == "closed" ) on = true;
        else if( state == "off" || state == "0" || state == "false" || state == "open"   ) on = false;
        else throw CliError("state must be on or off");

        QAbstractButton* b = control( comp, idx, true ).button;
        if( !b->isCheckable() ) throw CliError("control is a momentary button: use ui press / ui release");
        if( b->isChecked() != on ) b->click();
        return compControls( comp );
    });

    add("ui dial", "ui dial <comp> [index] <value|N%>", "Set a dial or slider (raw value or percentage of its range).",
        []( CliCtx& c ) -> QJsonValue
    {
        Component* comp = CliRefs::comp( c.arg( 0, "component") );
        if( c.pos.size() < 2 ) throw CliError("missing value");
        QString val = c.pos.last();
        QString idx = c.pos.size() > 2 ? c.pos.at( 1 ) : "";
        QAbstractSlider* s = control( comp, idx, false ).slider;

        int v;
        if( val.endsWith('%') ){
            double pct = Cli::parseNum( val.chopped( 1 ), "percentage");
            v = qRound( s->minimum() + ( s->maximum()-s->minimum() )*pct/100.0 );
        }
        else v = (int)Cli::parseNum( val, "value");
        if( v < s->minimum() || v > s->maximum() )
            throw CliError( QString("value out of range %1..%2").arg( s->minimum() ).arg( s->maximum() ) );
        s->setValue( v );
        return compControls( comp );
    });

    add("ui key", "ui key <char> [down|up|tap]", "Send a keyboard key to the circuit (for key-bound switches/buttons).",
        []( CliCtx& c ) -> QJsonValue
    {
        QString key = c.arg( 0, "key");
        if( key.size() != 1 ) throw CliError("key must be a single character");
        QString action = c.argOr( 1, "tap").toLower();
        Circuit* circ = Circuit::self();
        if     ( action == "down" ) emit circ->keyEvent( key, true );
        else if( action == "up"   ) emit circ->keyEvent( key, false );
        else if( action == "tap"  ) { emit circ->keyEvent( key, true ); emit circ->keyEvent( key, false ); }
        else throw CliError("action must be down, up or tap");
        return "ok";
    });
}
