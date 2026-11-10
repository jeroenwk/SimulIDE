/***************************************************************************
 *   Copyright (C) 2018 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QGraphicsProxyWidget>

#include <QDebug>

#include "oscope.h"
#include "connector.h"
#include "circuitwidget.h"
#include "circuit.h"
#include "simulator.h"
#include "itemlibrary.h"
#include "oscopechannel.h"
#include "oscwidget.h"
#include "datawidget.h"
#include "tunnel.h"
#include "e-node.h"
#include "iopin.h"

#include "stringprop.h"
#include "doubleprop.h"
#include "intprop.h"
#include "boolprop.h"

#define tr(str) simulideTr("Oscope",str)

Component* Oscope::construct( QString type, QString id )
{ return new Oscope( type, id ); }

LibraryItem* Oscope::libraryItem()
{
    return new LibraryItem(
        tr( "Oscope" ),
        "Meters",
        "oscope.png",
        "Oscope",
        Oscope::construct );
}

Oscope::Oscope( QString type, QString id )
      : PlotBase( type, id )
{
    m_numChannels = 4;
    m_trigger = 4;
    m_auto    = 4;
    m_trigAuto  = true;
    m_trigLevel = 0;
    m_trigState = -1;

    m_oscWidget  = new OscWidget( CircuitWidget::self(), this );
    m_dataWidget = new DataWidget( NULL, this );
    m_proxy = Circuit::self()->addWidget( m_dataWidget );
    m_proxy->setParentItem( this );
    m_dataWidget->show();
    m_display = m_oscWidget->display();
    m_display->setPlotBase( this );
    m_display->setFixedSize( m_baSizeX+6*8, m_baSizeY+2*8 );

    m_inPin.resize(5);
    m_pin.resize(5);
    m_pin[4] = m_inPin[4] = new IoPin( 180, QPoint(-80-8, 64 ), id+"-PinG", 0, this, input );
    m_pin[4]->setWireColor( QColor( 128, 128, 128 ) );   // Ground lead: grey

    for( int i=0; i<4; i++ )
    {
        m_pin[i] = m_inPin[i] = new IoPin( 180, QPoint(-80-8,-48+32*i ), id+"-Pin"+QString::number(i), 0, this, undef_mode );
        double admit = m_connectGnd ? m_inputAdmit : 0;
        m_inPin[i]->setInputAdmit( admit );
        m_channel[i] = new OscopeChannel( this, id+"Chan"+QString::number(i) );
        m_channel[i]->m_channel = i;
        m_channel[i]->m_ePin[0] = m_pin[i];
        m_channel[i]->m_ePin[1] = m_pin[4]; // Ref Pin
        m_channel[i]->m_buffer.resize( m_bufferSize );
        m_channel[i]->m_time.resize( m_bufferSize );

        m_hideCh[i] = false;

        m_display->setChannel( i, m_channel[i] );
        m_display->setColor( i, m_color[i] );
        m_pin[i]->setWireColor( m_color[i] );            // Probe lead in the channel colour
        m_dataWidget->setColor( i, m_color[i] );

        setTimePos( i, 0 );
        setVoltDiv( i, 1 );
        setVoltPos( i, 0 );
    }
    setFilter( 0.1 );
    setTimeDiv( 1e9 ); // 1 ms
    setLabelPos(-90,-100, 0);
    expand( false );

    addPropGroup( { "Hidden1", {
        new DoubProp<Oscope>("Filter", "", "V"
                            , this, &Oscope::filter, &Oscope::setFilter ),

        new IntProp <Oscope>("AutoSC" ,"", ""
                            , this, &Oscope::autoSC, &Oscope::setAutoSC ),

        new IntProp <Oscope>("Tracks" ,"", ""
                            , this, &Oscope::tracks, &Oscope::setTracks ),

        new StrProp <Oscope>("HideCh" ,"", ""
                            , this, &Oscope::hideCh, &Oscope::setHideCh ),

        new StrProp <Oscope>("VolPos" ,"", ""
                            , this, &Oscope::volPos, &Oscope::setVolPos ),

        new BoolProp<Oscope>("TrigAuto" ,"", ""
                            , this, &Oscope::trigAuto, &Oscope::setTrigAuto ),

        new DoubProp<Oscope>("TrigLevel", "", "V"
                            , this, &Oscope::trigLevel, &Oscope::setTrigLevel )
    }, groupHidden } );
}
Oscope::~Oscope()
{
    m_proxy->setWidget( NULL );
    delete m_dataWidget;

    m_oscWidget->setParent( NULL );
    m_oscWidget->close();
    delete m_oscWidget;
}

void Oscope::updateStep()
{
    uint64_t period = 0;
    uint64_t timeFrame = m_timeDiv*10;
    uint64_t simTime;

    if( screenDue() )   // Running, or paused with new data (BH-051)
    {
        bool fixedLevel = m_trigger < 4 && !m_trigAuto;
        if( !fixedLevel && m_trigger < 4 ) period = m_channel[m_trigger]->m_period; // We want a trigger

        uint64_t now = Simulator::self()->circTime();
        if( fixedLevel ) // Trigger at a fixed level: the crossings the channel found
            levelEnd( now, timeFrame, &simTime );
        else if( m_trigger < 4 && levelEnd( now, timeFrame, &simTime ) ) {} // Auto level edges (BH-052)
        else if( period > 10 ) // We have a Trigger
        {
            uint64_t risEdge = m_channel[m_trigger]->m_risEdge;

            uint64_t nCycles = timeFrame/period;
            if( timeFrame%period ) nCycles++;
            if( nCycles%2 )        nCycles++;

            uint64_t delta = nCycles*period/2-timeFrame/2;
            if( delta > risEdge ) delta = risEdge;
            simTime = risEdge-delta;
        }
        else simTime = Simulator::self()->circTime(); // free running

        m_display->setTimeEnd( simTime );

        for( int i=0; i<4; i++ )
        {
            bool connected = m_pin[i]->connector();
            if( !connected )
            {
                QString chTunnel = m_channel[i]->m_chTunnel;

                eNode* enode = Tunnel::getEnode( chTunnel );
                m_pin[i]->setEnode( enode );
                if( enode )
                {
                    enode->voltChangedCallback( m_channel[i] );
                    connected = true;
                }
                display()->connectChannel( i, connected );
            }
            m_channel[i]->m_connected = connected;
            if( connected ) m_channel[i]->updateStep();
            m_channel[i]->m_trigIndex = m_channel[i]->m_bufferCounter;
        }
    }
    m_display->update();

    if( m_changed ){
        m_changed = false;
        double admit = m_connectGnd ? m_inputAdmit : 0;
        for( IoPin* pin : m_inPin ){
            pin->setInputAdmit( admit );
            pin->stampAdmitance( admit );
        }
    }
}

void Oscope::expand( bool e )
{
    m_expand = e;
    m_screenSizeY = m_baSizeY+2*4;
    if( e ){
        m_display->setMaximumSize( 9999, 9999 );
        m_oscWidget->getLayout()->addWidget( m_display );
        m_oscWidget->setWindowTitle( idLabel() );
        m_oscWidget->show();
        m_screenSizeX = 0;
    }else{
        m_screenSizeX = m_baSizeX+2*4;
        m_oscWidget->hide();
        m_dataWidget->getLayout()->addWidget( m_display );
        m_display->setMaximumSize( m_screenSizeX, m_screenSizeY );
    }
    m_display->setMinimumSize( m_screenSizeX, m_screenSizeY );

    int widgetSizeX = m_screenSizeX+68+4;
    int widgetSizeY = m_screenSizeY+4;
    int centerY = widgetSizeY/2;
    m_dataWidget->setFixedSize( widgetSizeX, widgetSizeY );
    m_proxy->setPos( -80+2, -centerY-2+4 );
    m_area = QRectF( -80, -centerY, widgetSizeX+4, widgetSizeY+4+2 );

    m_display->setExpand( e );
    m_display->updateValues();
    Circuit::self()->update();
}

void Oscope::setFilter( double filter )
{
    m_filter = filter;
    m_oscWidget->setFilter( filter );
    for( int i=0; i<4; i++ ) m_channel[i]->setFilter( filter );
}

void Oscope::setTrigger( int ch )
{
    if( ch < 0 || ch > 4 )  // BH-023: same null button() crash as setAutoSC
    {
        qDebug() << "[BH-023] Oscope::setTrigger: invalid trigger channel" << ch << "ignored (valid: 0..4)";
        return;
    }
    m_trigger = ch;
    m_oscWidget->setTrigger( ch );
    m_trigState = -1;
    for( int i=0; i<4; ++i ) ((OscopeChannel*)m_channel[i])->resetTrig(); // Edges were of the old channel

    if( ch > 3 ) return;
    for( int i=0; i<4; ++i ){
        if( ch == i ) m_channel[i]->m_trigger = true;
        else          m_channel[i]->m_trigger = false;
}   }

bool Oscope::levelEnd( uint64_t now, uint64_t timeFrame, uint64_t* end )
{
    // Like a real scope in Auto sweep: the trigger point (a crossing of the level) goes in the
    // middle of the screen, so the latest edge with half a screen of data after it is shown.
    // No such edge in the last 10 screens (the buffer holds about 12): free running (*end = now,
    // returns false). With the level on Auto the crossings are of the channel's auto level
    // (OscopeChannel::findEdges), and the caller falls back to the measured period.
    OscopeChannel* ch = (OscopeChannel*)m_channel[m_trigger];
    uint64_t half = timeFrame/2;
    uint64_t edge = 0;
    bool found = ( now > half ) && ch->trigEdgeBefore( now-half, &edge )
              && ( now-edge <= timeFrame*10 );
    int state = found ? 1 : 0;
    if( state != m_trigState ){
        m_trigState = state;
        qDebug() << "[ScopeTrig]" << getUid() << ( found ? "triggered"
                   : m_trigAuto ? "no edge at the auto level: measured period, or free running"
                                : "free running (no edge at the level)" )
                 << "ch" << m_trigger << "level" << ( m_trigAuto ? QString("auto") : QString::number( m_trigLevel )+" V" )
                 << "t" << now*1e-12 << "s";
    }
    *end = found ? edge+half : now;
    return found;
}

void Oscope::setTrigAuto( bool a )
{
    if( a != m_trigAuto ) qDebug() << "[ScopeTrig]" << getUid() << "level mode" << ( a ? "Auto" : "fixed" ) << m_trigLevel << "V";
    m_trigAuto = a;
    m_trigState = -1;
    for( int i=0; i<4; ++i ) ((OscopeChannel*)m_channel[i])->resetTrig();
    m_oscWidget->setTrigAuto( a );
    if( m_display ) m_display->update();
}

void Oscope::setTrigLevel( double l )
{
    if( l != m_trigLevel ) qDebug() << "[ScopeTrig]" << getUid() << "level" << m_trigLevel << "->" << l << "V";
    m_trigLevel = l;
    m_trigState = -1;
    for( int i=0; i<4; ++i ) ((OscopeChannel*)m_channel[i])->resetTrig();
    m_oscWidget->setTrigLevel( l );
    if( m_display ) m_display->update();
}

bool Oscope::trigLevelOn( double* level )
{
    if( m_trigAuto || m_trigger > 3 ) return false;
    *level = m_trigLevel;
    return true;
}

void Oscope::setTunnels( QString tunnels )
{
    QStringList list = tunnels.split(",");
    for( int i=0; i<list.size(); i++ ){
        if( i >= m_numChannels ) break;
        m_channel[i]->m_chTunnel = list.at(i);
        m_dataWidget->setTunnel( i, list.at(i) );
}   }

void Oscope::setAutoSC( int ch )
{
    // BH-023: the auto-scale selector only has the ids 0..4; anything else made autoGroup->button() null and crash.
    if( ch < 0 || ch > 4 )
    {
        qDebug() << "[BH-023] Oscope::setAutoSC: invalid AutoSC" << ch << "ignored (valid: 0..4)";
        return;
    }
    m_auto = ch;
    m_oscWidget->setAuto( ch );
}

QString Oscope::hideCh()
{
    QString list;
    QString hide;
    for( int i=0; i<4; ++i ){
        hide = m_hideCh[i]? "true":"false";
        list.append( hide ).append(",");
    }
    return list;
}

void Oscope::setHideCh( QString hc )
{
    QStringList list = hc.split(",");
    for( int i=0; i<4; ++i ){
        if( i == list.size() ) break;
        bool hide = (list.at(i) == "true")? true:false;
        hideChannel( i, hide );
}   }

void Oscope::hideChannel( int ch, bool hide )
{
    if( ch > 3 ) return;
    m_hideCh[ch] = hide;
    m_display->hideChannel( ch , hide );
    m_oscWidget->hideChannel( ch, hide );
}

int Oscope::tracks() { return m_display->tracks(); }
void Oscope::setTracks( int tracks )
{
    // BH-022: the track selector only has the ids 1, 2 and 4; anything else
    // (hand-edited file, CLI) made trackGroup->button() return nullptr and crash.
    if( tracks != 1 && tracks != 2 && tracks != 4 )
    {
        qDebug() << "[BH-022] Oscope::setTracks: invalid Tracks" << tracks << "ignored (valid: 1, 2, 4)";
        return;
    }
    m_display->setTracks( tracks );
    m_oscWidget->setTracks( tracks );
}

void Oscope::setTimeDiv( uint64_t td )
{
    if( td < 1 ) td = 1;
    PlotBase::setTimeDiv( td );
    m_oscWidget->updateTimeDivBox( td );
}

QString Oscope::timPos()
{
    QString list;
    for( int i=0; i<4; ++i ) list.append( QString::number( m_timePos[i] )).append(",");
    return list;
}

void Oscope::setTimPos( QString tp )
{
    QStringList list = tp.split(",");
    for( int i=0; i<4; ++i ){
        if( i == list.size() ) break;
        setTimePos( i, list.at(i).toLongLong() );
}   }

QString Oscope::volDiv()
{
    QString list;
    for( int i=0; i<4; ++i ) list.append( QString::number( m_voltDiv[i] )).append(",");
    return list;
}

void Oscope::setVolDiv( QString vd )
{
    QStringList list = vd.split(",");
    for( int i=0; i<4; ++i ){
        if( i == list.size() ) break;
        setVoltDiv( i, list.at(i).toDouble() );
}   }

QString Oscope::volPos()
{
    QString list;
    for( int i=0; i<4; ++i ) list.append( QString::number( m_voltPos[i] )).append(",");
    return list;
}

void Oscope::setVolPos( QString vp )
{
    QStringList list = vp.split(",");
    for( int i=0; i<4; ++i ){
        if( i == list.size() ) break;
        setVoltPos( i, list.at(i).toDouble() );
}   }

void Oscope::setTimePos( int ch, int64_t tp )
{
    m_timePos[ch] = tp;
    m_display->setHPos( ch, tp );
    m_oscWidget->updateTimePosBox( ch, tp );
}

void Oscope::moveTimePos( int64_t delta )
{
    if( m_trigger < 4  )
    {
        uint64_t period = m_channel[m_trigger]->m_period; // check wave period
        if( period > 10 ) return;                // We have a Trigger: Don't Zoom at mouse pos.
    }
    for( int i=0; i<4; ++i )
    {
        m_timePos[i] = m_timePos[i]+delta;
        m_display->setHPos( i, m_timePos[i] );
        m_oscWidget->updateTimePosBox( i, m_timePos[i] );
    }
}

void Oscope::setVoltDiv( int ch, double vd )
{
    m_voltDiv[ch] = vd;
    m_display->setVTick( ch, vd );
    m_oscWidget->updateVoltDivBox( ch, vd );
}

void Oscope::setVoltPos( int ch, double vp )
{
    m_voltPos[ch] = vp;
    m_display->setVPos( ch, vp );
    m_oscWidget->updateVoltPosBox( ch, vp );
}

