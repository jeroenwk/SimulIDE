/***************************************************************************
 *   Instruments on the canvas: show or hide them with their leads         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QSettings>

#include "instrumentview.h"
#include "mainwindow.h"
#include "circuitwidget.h"
#include "circuit.h"
#include "component.h"
#include "connector.h"
#include "connectorline.h"
#include "tunnel.h"
#include "node.h"
#include "pin.h"

InstrumentView* InstrumentView::self()
{
    static InstrumentView* view = new InstrumentView();
    return view;
}

InstrumentView::InstrumentView()
{
    m_shown = MainWindow::self()->settings()->value("Circuit/showInstruments", true ).toBool();
}

void InstrumentView::setShown( bool show )
{
    if( show == m_shown ) return;
    m_shown = show;
    CircuitWidget::self()->syncInstruments( show );    // Toolbar button
    MainWindow::self()->settings()->setValue("Circuit/showInstruments", show );
    apply();
}

bool InstrumentView::isInstrument( Component* comp )
{
    if( !comp ) return false;
    QString t = comp->itemType();
    return t == "Oscope" || t == "LAnalizer" || t == "Voltimeter" || t == "FreqMeter" || t == "Probe";
}

bool InstrumentView::onlyLeads( Component* comp )
{
    // A ground or rail that only an instrument uses (the scope's ground, a meter's COM)
    QString t = comp->itemType();
    if( t != "Ground" && t != "Rail" ) return false;
    bool any = false;
    for( Pin* pin : comp->getPins() )
    {
        Connector* con = pin ? pin->connector() : nullptr;
        if( !con ) continue;
        if( !con->leadPin() ) return false;
        any = true;
    }
    return any;
}

void InstrumentView::apply()
{
    Circuit* circ = Circuit::self();
    if( !circ ) return;

    for( Component* comp : *circ->compList() )
    {
        bool lead = isInstrument( comp );
        if( !lead ){
            Tunnel* tunnel = dynamic_cast<Tunnel*>( comp );
            lead = tunnel && tunnel->leadPin();
        }
        if( !lead ) lead = onlyLeads( comp );
        if( lead ) comp->setVisible( m_shown );
    }
    for( Connector* con : *circ->conList() )
    {
        if( !con->leadPin() ) continue;
        for( ConnectorLine* line : *con->lineList() ) line->setVisible( m_shown );
    }
    circ->update();        // Junction dots check their wires when painted
}
