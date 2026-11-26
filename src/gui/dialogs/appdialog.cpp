/***************************************************************************
 *   Copyright (C) 2021 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QTimer>
#include <math.h>

#include "uitheme.h"
#include "appfonts.h"
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
#include <QGroupBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include "theme.h"
#include "appdialog.h"
#include "mainwindow.h"
#include "simulator.h"
#include "circuit.h"
#include "circuitwidget.h"
#include "shortcuts.h"
#include <QTableWidget>
#include <QHeaderView>

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
    {
        QPushButton* fontDefault = new QPushButton( tr("Default"), this );   // Back to the bundled font
        fontDefault->setAutoDefault( false );
        fontDefault->setSizePolicy( QSizePolicy::Fixed, QSizePolicy::Fixed );
        fontDefault->setToolTip( tr("Use the default font (%1). Restart to apply.").arg( AppFonts::sans() ) );
        auto sync = [fontDefault](){ fontDefault->setEnabled( MainWindow::self()->defaultFontName() != AppFonts::sans() ); };
        sync();
        connect( fontDefault, &QPushButton::clicked, [this, sync]()
        {
            fontName->setCurrentFont( QFont( AppFonts::sans() ) );
            MainWindow::self()->setDefaultFontName( AppFonts::sans() );
            sync();
        });
        connect( fontName, &QFontComboBox::currentFontChanged, [sync](){ sync(); } );
        horizontalLayout_15->addWidget( fontDefault );
    }
    fontScale->setValue( m_scale );
    fontScale->setFixedWidth( 80*m_scale );
    userPath->setText( MainWindow::self()->userPath() );

    // Circuit Settings
    drawGrid->setChecked( Circuit::self()->drawGrid() );
    showScroll->setChecked( CircuitView::self()->showScroll() );
    { QSignalBlocker b( flashSelect ); flashSelect->setChecked( Circuit::flashEnabled() ); }
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
    buildNoise();
    buildLock();

    updtSpeedPer();

    m_showHelp = false;
    helpText->setVisible( false );
    mainLayout->removeWidget( helpText );
    //helpText->setText( help );

    buildShortcuts();

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
    if( cur == m_keysPage )                  // A long list: room for it, the rest scrolls
         resize( qMax( width(), int( 720*m_scale ) ), qMax( minimumHeight(), int( 560*m_scale ) ) );
    else resize( width(), minimumHeight()+extra );
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
        if( tabStr.isEmpty() ){ m_showHelp = false; helpText->setVisible( false ); mainLayout->removeWidget( helpText ); return; }
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
void AppDialog::on_flashSelect_toggled( bool on ) { Circuit::setFlashEnabled( on ); }

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

void AppDialog::updtRunNote()
{
    if( !m_runNote ) return;
    bool on = Simulator::self() && Simulator::self()->isRunning();
    if( m_runNote->isVisible() != on ) m_runNote->setVisible( on );
}

void AppDialog::showEvent( QShowEvent* e )
{
    updtRunNote();
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
    syncNoise();
    syncLock();
    fillShortcuts();                         // Part letters may have changed
    QDialog::showEvent( e );
}

// K10: every keyboard shortcut, read-only, from the registry (Shortcuts::table) and
// the part letters in effect. Keys as this platform writes them (⌘ on macOS)
void AppDialog::buildShortcuts()
{
    m_keysPage = new QWidget();
    QVBoxLayout* l = new QVBoxLayout( m_keysPage );
    QLabel* intro = new QLabel( tr("The keys of the circuit editor (click on the canvas first). "
        "A part's letter is set in its properties, Keyboard tab. "
        "Capital letters are kept for the Key of switches and push buttons (Shift+letter)."), m_keysPage );
    intro->setWordWrap( true );
    l->addWidget( intro );

    m_keysTree = new QTableWidget( m_keysPage );
    m_keysTree->setObjectName("shortcutList");
    m_keysTree->setColumnCount( 3 );
    m_keysTree->setHorizontalHeaderLabels({ tr("Action"), tr("Keys"), tr("When") });
    m_keysTree->verticalHeader()->setVisible( false );
    m_keysTree->setShowGrid( false );
    m_keysTree->setSelectionMode( QAbstractItemView::NoSelection );
    m_keysTree->setEditTriggers( QAbstractItemView::NoEditTriggers );
    m_keysTree->setFocusPolicy( Qt::NoFocus );
    m_keysTree->setWordWrap( true );
    m_keysTree->setTextElideMode( Qt::ElideNone );
    m_keysTree->setVerticalScrollMode( QAbstractItemView::ScrollPerPixel );
    QHeaderView* h = m_keysTree->horizontalHeader();
    h->setDefaultAlignment( Qt::AlignLeft );
    h->setSectionResizeMode( 0, QHeaderView::Stretch );
    h->setSectionResizeMode( 1, QHeaderView::ResizeToContents );
    h->setSectionResizeMode( 2, QHeaderView::Interactive );
    h->resizeSection( 2, 170*m_scale );
    m_keysTree->setMinimumHeight( 200*m_scale );
    // Wrapped rows get their height from the column width: again whenever it changes
    connect( h, &QHeaderView::sectionResized, m_keysTree, [this](){
        QMetaObject::invokeMethod( m_keysTree, "resizeRowsToContents", Qt::QueuedConnection ); } );
    l->addWidget( m_keysTree, 1 );

    tabList->addTab( m_keysPage, tr("Shortcuts") );
    fillShortcuts();
}

void AppDialog::fillShortcuts()
{
    if( !m_keysTree ) return;
    QList<Shortcut> rows = Shortcuts::table();
    QFont bold = m_keysTree->font();
    bold.setBold( true );
    m_keysTree->clearSpans();
    m_keysTree->setRowCount( 0 );
    QString cat;
    auto cell = [this]( int r, int c, QString text ){
        QTableWidgetItem* it = new QTableWidgetItem( text );
        it->setFlags( Qt::ItemIsEnabled );
        it->setTextAlignment( Qt::AlignLeft | Qt::AlignTop );
        m_keysTree->setItem( r, c, it );
        return it;
    };
    for( const Shortcut& s : rows )
    {
        int r = m_keysTree->rowCount();
        if( s.category != cat )                          // The category's heading row
        {
            cat = s.category;
            m_keysTree->insertRow( r );
            QTableWidgetItem* it = cell( r, 0, cat );
            it->setFont( bold );
            it->setBackground( m_keysTree->palette().color( QPalette::Button ) );  // Light and dark themes
            m_keysTree->setSpan( r, 0, 1, 3 );
            r++;
        }
        m_keysTree->insertRow( r );
        cell( r, 0, s.action );
        cell( r, 1, s.keys );
        cell( r, 2, s.context );
    }
    m_keysTree->resizeRowsToContents();
}

void AppDialog::buildNoise()
{
    m_noiseBox = new QGroupBox( tr("Noise (thermal and shot noise of the parts)"), simTab );
    m_noiseBox->setCheckable( true );
    m_noiseBox->setToolTip( tr("Adds the physical noise of resistors, diodes and transistors. "
                               "Shown in the toolbar while on. Applies at the next start.") );
    QFormLayout* f = new QFormLayout( m_noiseBox );
    m_noiseBW = new QLineEdit( m_noiseBox );
    m_noiseBW->setToolTip( tr("Noise bandwidth, e.g. 10MHz: every source draws a new value every 1/(2·bandwidth). "
                              "Keep the reactive step at or below that.") );
    f->addRow( tr("Bandwidth"), m_noiseBW );
    m_noiseScale = new QDoubleSpinBox( m_noiseBox );
    m_noiseScale->setDecimals( 3 );
    m_noiseScale->setRange( 0, 1e9 );
    m_noiseScale->setToolTip( tr("Multiplies every noise current: 1 is the physical level") );
    f->addRow( tr("Scale"), m_noiseScale );
    m_noiseSeed = new QLineEdit( m_noiseBox );
    m_noiseSeed->setToolTip( tr("0: a new random seed at every start (shown in the toolbar). Any other number repeats the same noise.") );
    f->addRow( tr("Seed"), m_noiseSeed );
    m_noiseWindow = new QComboBox( m_noiseBox );
    m_noiseWindow->addItems({ tr("Start-up only (0 to 1 ms)"), tr("Whole run"), tr("Custom") });
    f->addRow( tr("When"), m_noiseWindow );
    m_noiseStart = new QLineEdit( m_noiseBox );
    f->addRow( tr("From"), m_noiseStart );
    m_noiseStop = new QLineEdit( m_noiseBox );
    m_noiseStop->setToolTip( tr("0: until the end of the run") );
    f->addRow( tr("Until"), m_noiseStop );
    m_noiseOff = new QLineEdit( m_noiseBox );
    m_noiseOff->setToolTip( tr("Parts without noise: labels (R3, U1-R2) or types (type:Resistor), comma-separated") );
    f->addRow( tr("Parts off"), m_noiseOff );
    m_noiseErr = new QLabel( m_noiseBox );
    m_noiseErr->setStyleSheet("QLabel { color: #c00000; }");
    f->addRow( "", m_noiseErr );
    QFrame* line = new QFrame( simTab );   // Same separation as the other sections
    line->setFrameShape( QFrame::HLine );
    line->setFrameShadow( QFrame::Sunken );
    verticalLayout_2->insertSpacing( verticalLayout_2->count()-1, 9 );
    verticalLayout_2->insertWidget( verticalLayout_2->count()-1, line );
    verticalLayout_2->insertSpacing( verticalLayout_2->count()-1, 9 );
    verticalLayout_2->insertWidget( verticalLayout_2->count()-1, m_noiseBox );   // Above the bottom spacer

    // specs/sim-control.md §3.8: shown while the simulation runs
    m_runNote = new QLabel( tr("The simulation is running: Speed, FPS, Max. Iterations and the speed lock apply at once; "
                               "Reactive Step, Slope Steps and Noise apply at the next start. "
                               "Changes are logged in Messages with the simulated time."), simTab );
    m_runNote->setWordWrap( true );
    m_runNote->setStyleSheet("QLabel { color: #e67800; }");
    verticalLayout_2->insertWidget( 0, m_runNote );
    QTimer* runTimer = new QTimer( this );
    connect( runTimer, &QTimer::timeout, this, [this](){ if( isVisible() ) updtRunNote(); } );
    runTimer->start( 500 );
    updtRunNote();

    connect( m_noiseBox, &QGroupBox::toggled, this, [this](bool){ applyNoise(); } );
    for( QLineEdit* e : { m_noiseBW, m_noiseSeed, m_noiseStart, m_noiseStop, m_noiseOff } )
        connect( e, &QLineEdit::editingFinished, this, [this](){ applyNoise(); } );
    connect( m_noiseScale, &QDoubleSpinBox::editingFinished, this, [this](){ applyNoise(); } );
    connect( m_noiseWindow, QOverload<int>::of( &QComboBox::activated ), this, [this]( int i )
    {
        if( i == 0 ){ m_noiseStart->setText("0 s"); m_noiseStop->setText("1 ms"); }
        if( i == 1 ){ m_noiseStart->setText("0 s"); m_noiseStop->setText("0 s"); }
        applyNoise();
    });
    syncNoise();
}

// Speed lock group (specs/sim-control.md §3.9), above the noise group
void AppDialog::buildLock()
{
    m_lockBox = new QGroupBox( tr("Speed lock (hold the set speed)"), simTab );
    m_lockBox->setCheckable( true );
    m_lockBox->setToolTip( tr("While the computer can't keep the set speed, capacitors and inductors integrate "
                              "with a coarser step (doubled until it keeps up, up to the max. reactive step). "
                              "Less accurate; skips and sim run --for keep the normal step. Saved with the circuit.") );
    QFormLayout* f = new QFormLayout( m_lockBox );
    m_lockMax = new QLineEdit( m_lockBox );
    m_lockMax->setToolTip( tr("Largest reactive step the lock may use, e.g. 100us. Empty or 0: 100 × the reactive step") );
    f->addRow( tr("Max. reactive step"), m_lockMax );
    m_lockErr = new QLabel( m_lockBox );
    m_lockErr->setStyleSheet("QLabel { color: #c00000; }");
    f->addRow( "", m_lockErr );
    int at = verticalLayout_2->indexOf( m_noiseBox );
    QFrame* line = new QFrame( simTab );
    line->setFrameShape( QFrame::HLine );
    line->setFrameShadow( QFrame::Sunken );
    verticalLayout_2->insertWidget( at, m_lockBox );
    verticalLayout_2->insertSpacing( at+1, 9 );
    verticalLayout_2->insertWidget( at+2, line );
    verticalLayout_2->insertSpacing( at+3, 9 );

    connect( m_lockBox, &QGroupBox::toggled, this, [this]( bool on ){ CircuitWidget::self()->setSpeedLock( on ); } );
    connect( m_lockMax, &QLineEdit::editingFinished, this, [this]()
    {
        QString t = m_lockMax->text().trimmed();
        uint64_t ps = 0;
        if( !t.isEmpty() && t != "0" )
        {
            bool ok = false;
            double s = Simulator::parseSi( t, "s", &ok );
            if( !ok || s <= 0 ){ m_lockErr->setText( tr("Not a valid time (e.g. 100us)") ); return; }
            ps = (uint64_t)llround( s*1e12 );
        }
        Simulator::self()->setLockMaxStep( ps );
        Circuit::self()->setChanged();
        syncLock();
    });
    syncLock();
}

void AppDialog::syncLock()
{
    if( !m_lockBox ) return;
    Simulator* sim = Simulator::self();
    QSignalBlocker b( m_lockBox );
    m_lockBox->setChecked( sim->speedLock() );
    m_lockMax->setText( sim->lockMaxStepSet() ? Simulator::siStr( sim->lockMaxStepSet()*1e-12, "s" ) : "" );
    m_lockMax->setPlaceholderText( Simulator::siStr( sim->lockMaxStep()*1e-12, "s" )+tr(" (100 × reactive step)") );
    m_lockErr->clear();
}

void AppDialog::syncNoise()
{
    if( !m_noiseBox ) return;
    const NoiseSettings& n = Simulator::self()->noise();
    QSignalBlocker b( m_noiseBox );
    m_noiseBox->setChecked( n.on );
    m_noiseBW->setText( Simulator::siStr( n.bw, "Hz" ) );
    m_noiseScale->setValue( n.scale );
    m_noiseSeed->setText( QString::number( n.seed ) );
    m_noiseStart->setText( Simulator::siStr( n.start*1e-12, "s" ) );
    m_noiseStop->setText( Simulator::siStr( n.stop*1e-12, "s" ) );
    m_noiseOff->setText( n.off.join(", ") );
    int w = 2;
    if( n.start == 0 && n.stop == 1000000000ULL ) w = 0;
    else if( n.start == 0 && n.stop == 0 ) w = 1;
    m_noiseWindow->setCurrentIndex( w );
    m_noiseErr->clear();
}

void AppDialog::applyNoise()
{
    NoiseSettings n = Simulator::self()->noise();
    QStringList err;
    bool ok = false;
    n.on = m_noiseBox->isChecked();
    double bw = Simulator::parseSi( m_noiseBW->text(), "Hz", &ok );
    if( ok && bw > 0 ) n.bw = bw; else err << tr("bandwidth");
    n.scale = m_noiseScale->value();
    qulonglong seed = m_noiseSeed->text().trimmed().toULongLong( &ok );
    if( ok ) n.seed = seed; else err << tr("seed");
    double t = Simulator::parseSi( m_noiseStart->text(), "s", &ok );
    if( ok && t >= 0 ) n.start = (uint64_t)llround( t*1e12 ); else err << tr("from");
    t = Simulator::parseSi( m_noiseStop->text(), "s", &ok );
    if( ok && t >= 0 ) n.stop = (uint64_t)llround( t*1e12 ); else err << tr("until");
    n.off.clear();
    for( QString e : m_noiseOff->text().split(",", QString::SkipEmptyParts ) )
        if( !e.trimmed().isEmpty() ) n.off.append( e.trimmed() );
    Simulator::self()->setNoise( n );
    Circuit::self()->setChanged();
    CircuitWidget::self()->updateNoise();
    m_noiseErr->setText( err.isEmpty() ? "" : tr("Not a valid value: ")+err.join(", ") );
    if( err.isEmpty() ) syncNoise();   // Show the values as understood (units, presets)
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

