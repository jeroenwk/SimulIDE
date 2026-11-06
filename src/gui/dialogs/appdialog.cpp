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
#include <QScrollArea>
#include <QDebug>
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

    for( QLabel* l : findChildren<QLabel*>() )   // Section headings: the app font and scale
        if( l->styleSheet().contains("bold 13px") ) l->setStyleSheet( UiTheme::headingSheet() );

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

    tabList->setMinimumHeight( 0 );   // The .ui's fixed 300 px hid the pages' real minimum: widgets got squeezed
    on_tabList_currentChanged( tabList->currentIndex() );   // Size by the first tab shown
}

void AppDialog::on_tabList_currentChanged( int tab )
{
    if( helpText->isVisible() ) updtHelp();

    // Each tab is sized by its own contents, with no height caps: a cap squeezes the
    // widgets when the font's line height changes (Inter is taller than Ubuntu).
    // Hidden tabs are ignored vertically, so a short tab gives a short dialog
    for( int i=0; i<tabList->count(); ++i )
        tabList->widget( i )->setSizePolicy( QSizePolicy::Preferred,
                                             ( i == tab ) ? QSizePolicy::Preferred : QSizePolicy::Ignored );
    setMinimumHeight( 0 );
    setMaximumHeight( QWIDGETSIZE_MAX );
    tabList->updateGeometry();               // The policies changed: recompute the hints now
    layout()->invalidate();
    layout()->activate();
    setMinimumHeight( minimumSizeHint().height() );
    // The tab's preferred height (its page's preferred extra over its minimum). Not
    // adjustSize(): it caps at 2/3 of the screen; nor sizeHint(): it counts every page
    QWidget* cur = tabList->widget( tab );
    int extra = qMax( 0, cur->sizeHint().height()-cur->minimumSizeHint().height() );
    resize( width(), minimumHeight()+extra );
    QWidget* page = cur;
    qDebug() << "[Settings] tab" << tab << "min height" << minimumHeight() << "size" << size()
             << "page min" << page->minimumSizeHint() << "page layout min" << ( page->layout() ? page->layout()->minimumSize() : QSize() )
             << "page hint" << page->sizeHint() << "tabs min" << tabList->minimumSizeHint()
             << "main layout min" << layout()->minimumSize() << "constraint" << layout()->sizeConstraint();
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
    QVBoxLayout* boxLayout = new QVBoxLayout( colorsBox );
    boxLayout->setContentsMargins( 6, 4, 6, 4 );

    QHBoxLayout* top = new QHBoxLayout;                       // Preset and reset: always visible
    m_themeBox = new QComboBox( colorsBox );
    m_themeBox->addItems( Theme::choices() );
    m_themeBox->setToolTip( tr("Color preset. System: Light or Dark, following the OS appearance. Dark is inspired by EveryCircuit.") );
    connect( m_themeBox, QOverload<int>::of( &QComboBox::activated ), [this]( int )
    {
        if( Theme::applyPreset( m_themeBox->currentText() ) ) updateColorButtons();
    });
    QPushButton* reset = new QPushButton( tr("Reset colors"), colorsBox );
    reset->setAutoDefault( false );
    connect( reset, &QPushButton::clicked, [this](){ Theme::reset(); updateColorButtons(); } );
    top->addWidget( new QLabel( tr("Theme"), colorsBox ) );
    top->addWidget( m_themeBox, 1 );
    top->addWidget( reset );
    boxLayout->addLayout( top );

    // The swatches scroll in their own area, so the tab keeps a sensible height
    QScrollArea* scroll = new QScrollArea( colorsBox );
    scroll->setWidgetResizable( true );
    scroll->setFrameShape( QFrame::NoFrame );
    scroll->setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
    scroll->viewport()->setAutoFillBackground( false );
    QWidget* swatches = new QWidget( scroll );
    swatches->setAutoFillBackground( false );
    QGridLayout* grid = new QGridLayout( swatches );
    grid->setContentsMargins( 0, 2, 6, 2 );                   // Right: room for the scroll bar

    const int animRow = (Theme::firstAnimRole+1)/2;           // After the base colours
    for( int i=0; i<Theme::numRoles; ++i )
    {
        Theme::role_t role = (Theme::role_t)i;
        QPushButton* b = new QPushButton( swatches );
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
        int row = ( i < Theme::firstAnimRole ) ? k/2 : animRow+1+k/2;
        grid->addWidget( new QLabel( Theme::caption( role ), swatches ), row, (k%2)*2 );
        grid->addWidget( b, row, (k%2)*2+1 );
    }
    QLabel* animLabel = new QLabel( tr("Animate"), swatches );   // Sub-heading
    QFont f = animLabel->font(); f.setBold( true ); animLabel->setFont( f );
    grid->addWidget( animLabel, animRow, 0, 1, 4 );
    scroll->setWidget( swatches );

    int content = swatches->sizeHint().height();
    int rowH = qMax( (int)(20*m_scale), animLabel->sizeHint().height() )+grid->verticalSpacing();
    int shown = qMin( content, 6*rowH );                     // About 6 rows, then scroll
    scroll->setFixedHeight( shown );
    qDebug() << "[Settings] colors: content" << content << "px, row" << rowH << "px, shown" << shown << "px";
    boxLayout->addWidget( scroll );
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
    QSignalBlocker b1( capCharge ), b2( capLabel ), b3( chargeScale ), b4( currentScale );
    capCharge->setChecked( ChargeView::self()->enabled() );
    capLabel->setCurrentIndex( ChargeView::self()->labelMode() );
    chargeScale->setValue( Circuit::self()->chargeScale() );
    currentScale->setValue( Circuit::self()->currentScale() );
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

void AppDialog::on_currentScale_valueChanged( double a )
{
    if( a != Circuit::self()->currentScale() ) Circuit::self()->setCurrentScale( a );
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

