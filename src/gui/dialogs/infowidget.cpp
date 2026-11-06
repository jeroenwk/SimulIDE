/***************************************************************************
 *   Copyright (C) 2023 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include "infowidget.h"
#include "mainwindow.h"
#include "mcu.h"
#include "uitheme.h"
#include "simulator.h"
#include "circuitwidget.h"

InfoWidget* InfoWidget::m_pSelf = nullptr;

InfoWidget::InfoWidget( QWidget* parent )
          : QWidget( parent )
{
    setupUi( this );
    m_pSelf = this;

    QFont font( MainWindow::self()->defaultFontName() , 10, QFont::Bold );
    double fontScale = MainWindow::self()->fontScale();
    font.setPixelSize( 12*fontScale );

    simTimeLabel->setFont( font );
    simTime->setFont( font );
    targetSpeedLabel->setFont( font );
    targetSpeed->setFont( font );
    realSpeedLabel->setFont( font );
    realSpeed->setFont( font );
    simLoadLabel->setFont( font );
    simLoadVal->setFont( font );
    guiLoadLabel->setFont( font );
    guiLoadVal->setFont( font );
    overLoadLabel->setFont( font );
    overLoadVal->setFont( font );
    fpsLabel->setFont( font );
    fpsVal->setFont( font );
    mainMcuLabel->setFont( font );
    mainMcu->setFont( font );
    mainMcuName->setFont( font );

    overLoadLabel->setVisible( false );
    m_timePalette = simTime->palette();         // The time in the headings purple (lightened in dark mode)
    m_timePalette.setColor( QPalette::Active,   QPalette::WindowText, UiTheme::headingColor() );
    m_timePalette.setColor( QPalette::Inactive, QPalette::WindowText, UiTheme::headingColor() );
    simTime->setPalette( m_timePalette );
    m_speedPalette = realSpeed->palette();
    overLoadVal->setVisible( false );
}

void InfoWidget::showBarTime()   // Toolbar copy of the time: headings purple, grey while stopped
{
    if( !m_barTime ) return;
    QColor c = ( m_shown == shownStopped ) ? QColor( 120, 120, 120 ) : UiTheme::headingColor();
    QString sheet = QString("font-weight: bold; color: rgb(%1, %2, %3)").arg( c.red() ).arg( c.green() ).arg( c.blue() );
    if( m_barTime->property("_sheetBase").toString() != sheet )   // Only on a change (UiTheme adapts it for dark mode)
    {
        m_barTime->setProperty("_sheetBase", sheet );
        m_barTime->setStyleSheet( sheet );
    }
    QString state = ( m_shown == shownStopped ) ? tr("stopped") : ( m_shown == shownPaused ) ? tr("paused") : "";
    m_barTime->setText( simTime->text().trimmed() );
    m_barTime->setToolTip( tr("Simulation time")+( state.isEmpty() ? "" : " ("+state+")" )
                          +( simTime->toolTip().isEmpty() ? "" : ". "+simTime->toolTip() ) );
}

QString InfoWidget::speedText( double psPerSec )
{
    double s = psPerSec/1e12;
    if( s <= 0    ) return "0 s/s";
    if( s >= 1    ) return QString::number( s, 'g', 3 )+" s/s";
    if( s >= 1e-3 ) return QString::number( s*1e3, 'g', 3 )+" ms/s";
    if( s >= 1e-6 ) return QString::number( s*1e6, 'g', 3 )+" µs/s";
    return QString::number( s*1e9, 'g', 3 )+" ns/s";
}

static QString pctText( double pct )
{
    QString t = QString::number( pct,'f', 2 );
    while( t.size() < 6 ) t = "0"+t;
    return t+" %";
}

void InfoWidget::setTargetSpeed( double s )
{
    m_targetPct = s;
    targetSpeed->setText("  "+speedText( s*1e10 )+"   "+pctText( s ) );
    updtMcu();
}

// Below the set speed (5 % margin) while running: real speed in orange, here and in the toolbar
void InfoWidget::setSlow( bool slow, double realPsPerSec )
{
    if( slow != m_slow )
    {
        m_slow = slow;
        QPalette pal = m_speedPalette;
        if( slow )
            for( QPalette::ColorGroup g : { QPalette::Active, QPalette::Inactive } )
                pal.setColor( g, QPalette::WindowText, slowColor() );
        realSpeed->setPalette( pal );
    }
    if( CircuitWidget::self() ) CircuitWidget::self()->showRealSpeed( slow ? realPsPerSec : 0 );
}

void InfoWidget::updtMcu()
{
    if( Mcu::self() )
    {
        QString device = Mcu::self()->device();
        QString freq = QString::number( Mcu::self()->freq()*1e-6 );
        mainMcu->setText( "  "+device+" at "+freq+" MHz" );

        QString name;
        Mcu* mcu = Mcu::self();
        if( mcu->isMainComp() && mcu->parentItem() )
        {
            Component* comp = static_cast<Component*>( mcu->parentItem() );
            name = comp->idLabel();
        }
        else name = mcu->idLabel();
        mainMcuName->setText( "  "+name );
    }else{
        mainMcu->setText( "  ---" );
        mainMcuName->setText( "  ---" );
    }
    mainMcuLabel->setVisible( Mcu::self() );
    mainMcu->setVisible( Mcu::self() );
    mainMcuName->setVisible( Mcu::self() );
}

void InfoWidget::setRate( double rate, double simLoad, double guiLoad, int fps )
{
    updtMcu();
    if( rate < 0 )
    {
        if( rate == -1 ) realSpeed->setText( "  "+tr("Speed: Debugger") );
        else             realSpeed->setText( "  "+tr("Circuit ERROR!!!") );
        setSlow( false );
    }else{
        //if( (load > 150) || (load < 0) ) load = 0;
        double speed = rate/100;
        QString Srate = QString::number( speed,'f', 2 );
        while( Srate.size() < 6 ) Srate = "0"+Srate;

        if( simLoad > 101 )
        {
            int overLoad = simLoad-100;
            simLoad = 100;

            QString Oload = QString::number( overLoad,'f', 2 );
            while( Oload.size() < 6 ) Oload = "0"+Oload;

            overLoadVal->setText( "  "+Oload+" %    ");
            overLoadLabel->setVisible( true );
            overLoadVal->setVisible( true );
        }else{
            overLoadLabel->setVisible( false );
            overLoadVal->setVisible( false );
        }
        QString Sload = QString::number( simLoad,'f', 2 );
        while( Sload.size() < 6 ) Sload = "0"+Sload;

        QString Gload = QString::number( guiLoad,'f', 2 );
        while( Gload.size() < 6 ) Gload = "0"+Gload;

        QString FPS = QString::number( fps );

        double realPs = rate*1e8;                  // rate: 100 x % of real time
        bool running = Simulator::self() && Simulator::self()->isRunning() && m_shown == shownRunning;
        realSpeed->setText( "  "+speedText( realPs )+"   "+Srate+" %" );
        setSlow( running && rate > 0 && speed < m_targetPct*0.95, realPs );
        simLoadVal->setText( "  "+Sload+" %    ");
        guiLoadVal->setText( "  "+Gload+" %    ");
        fpsVal->setText( "  "+FPS );
}   }

// Leading units that are zero are left out ("2 ms 500 µs", not "00:00:00 s 002 ms 500 µs");
// ns only at speeds of 1 µs/s or slower, ps never
QString InfoWidget::timeText( uint64_t ps )
{
    uint64_t speed = Simulator::self() ? Simulator::self()->psPerSec() : (uint64_t)1e12;
    bool showNs = speed <= 1000000;           // <= 1 µs per second

    uint64_t ns = ps/1000;
    uint64_t us = ns/1000;   ns %= 1000;
    uint64_t ms = us/1000;   us %= 1000;
    uint64_t s  = ms/1000;   ms %= 1000;
    uint64_t h  = s/3600, m = ( s/60 )%60;   s %= 60;

    QString t;
    auto pad = []( uint64_t v, int w ){ return QString("%1").arg( v, w, 10, QChar('0') ); };
    if     ( h ) t = QString::number( h )+":"+pad( m, 2 )+":"+pad( s, 2 )+" s";
    else if( m ) t = QString::number( m )+":"+pad( s, 2 )+" s";
    else if( s ) t = QString::number( s )+" s";

    auto unit = [&]( uint64_t v, QString name ){
        if( t.isEmpty() ){ if( v ) t = QString::number( v )+" "+name; }
        else t += "  "+pad( v, 3 )+" "+name;
    };
    unit( ms, "ms" );
    unit( us, "µs" );
    if( showNs ) unit( ns, "ns" );
    if( t.isEmpty() ) t = showNs ? "0 ns" : "0 µs";
    return t;
}

void InfoWidget::setCircTime( uint64_t tStep )
{
    if( tStep ) m_lastTime = tStep;
    if( m_shown == shownStopped && tStep ) return;   // Stopped: keep showing the reset state
    simTime->setText( timeText( tStep ) );
    showBarTime();
}

// Make the simulation state obvious next to the time: after a stop the circuit is
// reset, so the time shows 0 (greyed) with the time the last run reached
void InfoWidget::setSimState( simShown state )
{
    m_shown = state;
    if( state != shownRunning ) setSlow( false );
    if( state == shownStopped )
    {
        QPalette pal = m_timePalette;
        pal.setColor( QPalette::WindowText, QColor( 120, 120, 120 ) );
        simTime->setPalette( pal );
        simTimeLabel->setText( tr("Simulation Time: stopped") );
        QString last = m_lastTime ? "   (last run: "+timeText( m_lastTime )+")" : "";
        simTime->setText( "0 s  -  next start from 0"+last );
        simTime->setToolTip( tr("The circuit is reset. Power on starts again from 0 s.") );
    }else{
        simTime->setPalette( m_timePalette );
        simTimeLabel->setText( state == shownPaused ? tr("Simulation Time: paused") : tr("Simulation Time:") );
        simTime->setToolTip( state == shownPaused ? tr("Paused: Resume continues from here. Power off resets to 0 s.") : "" );
    }
    showBarTime();
}
