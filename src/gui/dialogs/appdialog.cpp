/***************************************************************************
 *   Copyright (C) 2021 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <math.h>

#include "uitheme.h"
#include "currentflow.h"
#include "chargeview.h"
#include <QColorDialog>
#include <QGridLayout>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QSignalBlocker>
#include "theme.h"
#include "appdialog.h"
#include "mainwindow.h"
#include "simulator.h"
#include "circuit.h"
#include "circuitwidget.h"

AppDialog::AppDialog( QWidget* parent )
         : QDialog( parent )
{
    setupUi( this );

    setWindowFlags( Qt::Window | Qt::WindowTitleHint | Qt::Tool
                   | Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint );

    // App Settings
    m_scale = MainWindow::self()->fontScale();
    language->setCurrentIndex( (int)MainWindow::self()->lang() );
    language->setFixedWidth( 80*m_scale );
    fontName->setCurrentText( MainWindow::self()->defaultFontName() );
    fontName->setFixedWidth( 80*m_scale );
    fontScale->setValue( m_scale );
    fontScale->setFixedWidth( 80*m_scale );
    userPath->setText( MainWindow::self()->userPath() );

    // Circuit Settings
    drawGrid->setChecked( Circuit::self()->drawGrid() );
    showScroll->setChecked( CircuitView::self()->showScroll() );
    animate->setChecked( Circuit::self()->animate() );
    currentFlow->setChecked( CurrentFlow::self()->enabled() );
    currentMin->setText( CurrentFlow::currentStr( CurrentFlow::self()->threshold() ) );
    currentRange->setValue( qRound( CurrentFlow::range() ) );
    {   // Filling the empty combo selects item 0: without the blocker that would save "Off"
        QSignalBlocker b( capLabel );
        capLabel->addItems({ tr("Off"), tr("Volts"), tr("Percent"), tr("Both") });
    }
    syncCharge();
    buildColors();
    fps->setValue( Simulator::self()->fps() );
    backup->setValue( Circuit::self()->autoBck() );

    // Simulation Settings
    m_blocked = true;

    m_psPerSec = Simulator::self()->psPerSec();
    m_speedP   = 100*(double)m_psPerSec/1e12;
    m_stepsPS  = Simulator::self()->stepsPerSec();
    m_stepSize = Simulator::self()->stepSize();
    m_stepUnit = log10(m_stepSize)/3;

    uint64_t reactStep = Simulator::self()->reactStep();
    int unit = 0; // ps
    uint64_t mult = 1;
    uint64_t step = reactStep / mult;
    while( remainder( reactStep, mult*1e3 ) == 0 )
    {
        unit += 1;
        mult *= 1e3;
        step = reactStep / mult;
    }
    reactStepBox->setValue( step );
    reactStepUnitBox->setCurrentIndex( unit );

    nlStepsBox->setValue( Simulator::self()->maxNlSteps() );
    slopeStepsBox->setValue( Simulator::self()->slopeSteps() );
    m_blocked = false;

    updtSpeedPer();

    m_showHelp = false;
    helpText->setVisible( false );
    mainLayout->removeWidget( helpText );
    //helpText->setText( help );

    this->adjustSize();
}

void AppDialog::on_tabList_currentChanged( int tab )
{
    if( helpText->isVisible() ) updtHelp();

    switch( tab ) {
        case 0: setMinimumHeight( 250*m_scale ); setMaximumHeight( 350*m_scale ); break;
        case 1:                                    // Sized by its contents (current flow, colors)
            setMinimumHeight( 0 ); setMaximumHeight( QWIDGETSIZE_MAX );
            setMinimumHeight( minimumSizeHint().height() );
            break;
        case 2: setMinimumHeight( 350*m_scale ); setMaximumHeight( 450*m_scale ); break;
    }
    adjustSize();
}
void AppDialog::updtHelp()
{
    if( m_showHelp ){
        if( !helpText->isVisible() ) mainLayout->addWidget( helpText );

        QString tabStr;
        switch( tabList->currentIndex() ) {
            case 0: tabStr = "app";        break;
            case 1: tabStr = "circuit";    break;
            case 2: tabStr = "simulation"; break;
        }
        helpText->setText( MainWindow::self()->getHelp( tabStr) );
    }
    else mainLayout->removeWidget( helpText );

    helpText->setVisible( m_showHelp );
    adjustSize();
}

void AppDialog::updtValues(){}

// App Settings -------------------------------

void AppDialog::on_language_currentIndexChanged( int index )
{
    MainWindow::self()->setLang( (Langs)index );
}

void AppDialog::on_fontScale_valueChanged( double scale )
{
    MainWindow::self()->setFontScale( scale );
}

void AppDialog::on_setPathButton_clicked()
{
    MainWindow::self()->getUserPath();
    userPath->setText( MainWindow::self()->userPath() );
}

void AppDialog::on_userPath_editingFinished()
{
    QString path = userPath->text();
    MainWindow::self()->setUserPath( path );
}

// Circuit Settings ----------------------------

void AppDialog::on_drawGrid_toggled( bool draw )
{
    Circuit::self()->setDrawGrid( draw );
}
void AppDialog::on_showScroll_toggled( bool show )
{
    CircuitView::self()->setShowScroll( show );
}

void AppDialog::on_animate_toggled( bool ani )
{
    Circuit::self()->setAnimate( ani );
}

// Circuit colours: a swatch button per colour (opens a colour picker) and a reset
void AppDialog::buildColors()
{
    QGridLayout* grid = new QGridLayout( colorsBox );
    grid->setContentsMargins( 6, 4, 6, 4 );

    m_themeBox = new QComboBox( colorsBox );                // Presets
    m_themeBox->addItems( Theme::presets() );
    m_themeBox->setToolTip( tr("Color preset. Dark is inspired by EveryCircuit.") );
    connect( m_themeBox, QOverload<int>::of( &QComboBox::activated ), [this]( int )
    {
        if( Theme::applyPreset( m_themeBox->currentText() ) ) updateColorButtons();
    });
    grid->addWidget( new QLabel( tr("Theme"), colorsBox ), 0, 0 );
    grid->addWidget( m_themeBox, 0, 1, 1, 3 );

    const int animRow = 1+(Theme::firstAnimRole+1)/2;           // After the base colours
    for( int i=0; i<Theme::numRoles; ++i )
    {
        Theme::role_t role = (Theme::role_t)i;
        QPushButton* b = new QPushButton( colorsBox );
        b->setFixedSize( 44*m_scale, 20*m_scale );
        b->setToolTip( tr("Click to choose the %1 color").arg( Theme::caption( role ).toLower() ) );
        b->setAutoDefault( false );
        b->setProperty("uiThemeKeep", true );   // Colour swatch
        m_colorButtons[i] = b;
        connect( b, &QPushButton::clicked, [this, role]()
        {
            QColor c = QColorDialog::getColor( Theme::get( role ), this, tr("%1 color").arg( Theme::caption( role ) ) );
            if( !c.isValid() ) return;                       // Cancelled
            Theme::set( role, c );
            updateColorButtons();
        });
        int k   = ( i < Theme::firstAnimRole ) ? i : i-Theme::firstAnimRole;
        int row = ( i < Theme::firstAnimRole ) ? 1+k/2 : animRow+1+k/2;
        grid->addWidget( new QLabel( Theme::caption( role ), colorsBox ), row, (k%2)*2 );
        grid->addWidget( b, row, (k%2)*2+1 );
    }
    QLabel* animLabel = new QLabel( tr("Animate"), colorsBox );   // Sub-heading
    QFont f = animLabel->font(); f.setBold( true ); animLabel->setFont( f );
    grid->addWidget( animLabel, animRow, 0, 1, 4 );
    QPushButton* reset = new QPushButton( tr("Reset colors"), colorsBox );
    reset->setAutoDefault( false );
    connect( reset, &QPushButton::clicked, [this](){ Theme::reset(); updateColorButtons(); } );
    grid->addWidget( reset, animRow+1+(Theme::numRoles-Theme::firstAnimRole+1)/2, 0, 1, 4 );
    updateColorButtons();
}

void AppDialog::showEvent( QShowEvent* e )
{
    // The dialog is reused: values may have changed (CLI, toolbar) since it was built
    QSignalBlocker b1( currentFlow );
    currentFlow->setChecked( CurrentFlow::self()->enabled() );
    currentMin->setText( CurrentFlow::currentStr( CurrentFlow::self()->threshold() ) );
    QSignalBlocker b3( currentRange );
    currentRange->setValue( qRound( CurrentFlow::range() ) );
    syncCharge();
    { QSignalBlocker b( reopenLast ); reopenLast->setChecked( MainWindow::self()->reopenLast() ); }
    { QSignalBlocker b( uiTheme ); uiTheme->setCurrentIndex( UiTheme::self()->mode() ); }
    QSignalBlocker b2( animate );
    animate->setChecked( Circuit::self()->animate() );
    refreshColors();
    QDialog::showEvent( e );
}

void AppDialog::updateColorButtons()
{
    for( int i=0; i<Theme::numRoles; ++i )
    {
        QColor c = Theme::get( (Theme::role_t)i );
        m_colorButtons[i]->setStyleSheet( QString("background-color: %1; border: 1px solid #777; border-radius: 3px;").arg( c.name() ) );
    }
    // Show the matching preset, or "Custom" (an extra entry, only while it applies)
    QString cur = Theme::currentPreset();
    int custom = m_themeBox->findText("Custom");
    if( cur == "Custom" && custom < 0 ) m_themeBox->addItem("Custom");
    if( cur != "Custom" && custom >= 0 ) m_themeBox->removeItem( custom );
    m_themeBox->setCurrentText( cur );
}

void AppDialog::on_currentFlow_toggled( bool on )
{
    CurrentFlow::self()->setEnabled( on );
}

void AppDialog::syncCharge()
{
    QSignalBlocker b1( capCharge ), b2( capLabel ), b3( chargeScale );
    capCharge->setChecked( ChargeView::self()->enabled() );
    capLabel->setCurrentIndex( ChargeView::self()->labelMode() );
    chargeScale->setValue( Circuit::self()->chargeScale() );
}

void AppDialog::on_reopenLast_toggled( bool on )
{
    MainWindow::self()->setReopenLast( on );
}

void AppDialog::on_uiTheme_activated( int i )
{
    if( i >= 0 ) UiTheme::self()->setMode( (UiTheme::mode_t)i );
}

void AppDialog::on_capCharge_toggled( bool on )
{
    ChargeView::self()->setEnabled( on );
}

void AppDialog::on_capLabel_currentIndexChanged( int i )
{
    if( i >= 0 ) ChargeView::self()->setLabelMode( i );
}

void AppDialog::on_chargeScale_valueChanged( double v )
{
    if( v != Circuit::self()->chargeScale() ) Circuit::self()->setChargeScale( v );
}

void AppDialog::on_currentRange_valueChanged( int decades )
{
    CurrentFlow::self()->setRange( decades );
}

void AppDialog::on_currentMin_editingFinished()
{
    double amps;
    if( !CurrentFlow::parseCurrent( currentMin->text(), amps ) || amps <= 0 ){
        currentMin->setText( CurrentFlow::currentStr( CurrentFlow::self()->threshold() ) );
        return;
    }
    CurrentFlow::self()->setThreshold( amps );
    currentMin->setText( CurrentFlow::currentStr( amps ) );
}

void AppDialog::on_fps_valueChanged( int fps )
{
    Simulator::self()->setFps( fps );
}

void AppDialog::on_backup_valueChanged( int secs )
{
    Circuit::self()->setAutoBck( secs );
}

// Simulation Settings ----------------------
void AppDialog::on_simSpeedPerSlider_valueChanged( int speed )
{
    if( m_blocked ) return;
    if( speed == 0 ) speed = 1;
    m_speedP = speed;
    m_psPerSec = m_speedP*1e12/100;
    updtSpeed();
}

void AppDialog::on_simStepBox_editingFinished()
{
    m_stepsPS = simStepBox->value();
    m_psPerSec = m_stepsPS * m_stepSize;
    updtSpeedPer();
}

void AppDialog::on_simStepUnitBox_currentIndexChanged( int index )
{
    if( m_blocked ) return;
    m_stepUnit = index;
    m_stepSize = pow( 1000, index );
    m_psPerSec = m_stepsPS * m_stepSize;
    updtSpeedPer();
}

void AppDialog::updtSpeed()
{
    if( m_blocked ) return;
    m_blocked = true;

    simSpeedPerSlider->setValue( m_speedP );
    speedLabel->setText( " "+QString::number( m_speedP,'f', 2 )+"%");

    m_stepsPS = m_psPerSec/m_stepSize;
    while( m_stepsPS >= 1000000000 )
    {
        m_stepsPS /= 1e3;
        m_stepSize *= 1e3;
        m_stepUnit += 1;
    }
    while( m_stepsPS == 0 )
    {
        m_stepSize /= 1e3;
        m_stepUnit -= 1;
        m_stepsPS = m_psPerSec/m_stepSize;
    }
    simStepBox->setValue( m_stepsPS );
    simStepUnitBox->setCurrentIndex( m_stepUnit );

    Simulator::self()->setStepSize( m_stepSize );
    Simulator::self()->setStepsPerSec( m_stepsPS );
    Simulator::self()->setPsPerSec( m_psPerSec );
    m_blocked = false;
}

void AppDialog::updtSpeedPer()
{
    m_speedP = (double)(100*m_psPerSec)/1e12;
    if( m_speedP > 100 )
    {
        m_speedP = 100;
        m_psPerSec = 1e12;
    }
    updtSpeed();
}

void AppDialog::on_nlStepsBox_editingFinished()
{
    Simulator::self()->setMaxNlSteps( nlStepsBox->value() );
}

void AppDialog::on_reactStepUnitBox_currentIndexChanged( int index )
{
    updtReactStep();
}

void AppDialog::on_reactStepBox_editingFinished()
{
    updtReactStep();
}

void AppDialog::updtReactStep()
{
    if( m_blocked ) return;
    uint64_t mult = pow( 1000, reactStepUnitBox->currentIndex() );
    uint64_t reactStep = mult*reactStepBox->value();
    Simulator::self()->setreactStep( reactStep );
}

void AppDialog::on_slopeStepsBox_editingFinished()
{
    Simulator::self()->setSlopeSteps( slopeStepsBox->value() );
}

void AppDialog::on_fontName_currentFontChanged( const QFont &f )
{
    MainWindow::self()->setDefaultFontName( f.family() );
}

