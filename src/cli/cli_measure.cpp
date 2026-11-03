/***************************************************************************
 *   SimulIDE command line control interface                              *
 *   Measurement and interaction commands                                  *
 ***( see copyright.txt file at root folder )*******************************/

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
            if( pin->isConnected() ) o["voltage"] = num( pin->getVoltage() );
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
