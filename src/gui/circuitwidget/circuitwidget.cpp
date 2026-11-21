/***************************************************************************
 *   Copyright (C) 2010 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QSignalBlocker>
#include <QSplitter>
#include <QTabWidget>
#include <QToolButton>
#include <QMessageBox>
#include <QFileDialog>
#include <QDesktopServices>
#include <QSettings>
#include <QTimer>
#include <QKeySequence>
#include <QStylePainter>
#include <QLineEdit>
#include <QKeyEvent>
#include <QApplication>
#include <QAbstractItemView>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QAbstractSpinBox>

#include "scriptpane.h"
#include "descriptionwidget.h"
#include "currentflow.h"
#include "chargeview.h"
#include "statusview.h"
#include "eventlog.h"
#include <QCheckBox>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <cmath>
#include "circuitwidget.h"
#include "keyboardcontroller.h"
#include "keytext.h"
#include "instrumentview.h"
#include "exportdialog.h"
#include "editorwindow.h"
#include "mainwindow.h"
#include "simulator.h"
#include "circuit.h"
#include "appdialog.h"
#include "filebrowser.h"
#include "infowidget.h"
#include "about.h"
#include "utils.h"
#include "simskip.h"
#include "simhistory.h"
#include "cliapi.h"

// The speed and skip boxes (specs/sim-control.md §3.5): editable combos, the list for presets,
// a typed value applied with Enter (Esc restores). The speed box shows the real speed in orange
// while the simulation can't keep up and the field is not being edited
class SpeedBox : public QComboBox
{
    public:
        SpeedBox( QWidget* parent ) : QComboBox( parent )
        {
            setEditable( true );
            setInsertPolicy( QComboBox::NoInsert );
            setCompleter( nullptr );
            lineEdit()->installEventFilter( this );
        }

        QString shownText() { return currentIndex() >= 0 ? itemText( currentIndex() ) : QString(); }

        void setOverride( QString text )
        {
            if( text == m_override ) return;
            m_override = text;
            refresh();
        }
        void setBad( bool bad ) { m_bad = bad; refresh( true ); }

        // The field shows the current item (or the override); not while the user types in it
        void refresh( bool force=false )
        {
            QLineEdit* e = lineEdit();
            if( !e || ( e->hasFocus() && !force ) ) return;
            QPalette p = palette();
            if( m_bad ) p.setColor( QPalette::Text, QColor( 0xc0, 0, 0 ) );
            else if( !m_override.isEmpty() && !e->hasFocus() ) p.setColor( QPalette::Text, InfoWidget::slowColor() );
            e->setPalette( p );
            if( m_bad ) return;
            e->setText( ( !m_override.isEmpty() && !e->hasFocus() ) ? m_override : shownText() );
        }

    protected:
        bool eventFilter( QObject* obj, QEvent* ev ) override
        {
            if( obj == lineEdit() )
            {
                if( ev->type() == QEvent::FocusIn ){ QLineEdit* e = lineEdit(); m_bad = false;
                    QPalette p = palette(); e->setPalette( p ); e->setText( shownText() );
                    QTimer::singleShot( 0, e, [e](){ e->selectAll(); } ); }
                else if( ev->type() == QEvent::FocusOut ){ m_bad = false; refresh( true ); }
                else if( ev->type() == QEvent::KeyPress
                      && static_cast<QKeyEvent*>( ev )->key() == Qt::Key_Escape )
                {
                    m_bad = false;
                    lineEdit()->setText( shownText() );
                    lineEdit()->clearFocus();      // Back to the simulation keys
                    return true;
                }
            }
            return QComboBox::eventFilter( obj, ev );
        }

    private:
        QString m_override;
        bool m_bad = false;
};

// "25%", "0.3 %", "3 ms/s", "3ms", "1 s/s (real time)", "2 s/s" -> simulated ps per real second
static bool parseSpeed( QString text, uint64_t* psPerSec )
{
    QString t = text.trimmed();
    int par = t.indexOf('(');
    if( par >= 0 ) t = t.left( par ).trimmed();
    double ps = 0;
    bool ok = false;
    if( t.endsWith('%') )
    {
        double pct = t.left( t.size()-1 ).trimmed().toDouble( &ok );
        if( !ok ) return false;
        ps = pct*1e10;
    }else{
        if( t.endsWith("/s") ) t.chop( 2 );
        double s = Simulator::parseSi( t.trimmed(), "s", &ok );
        if( !ok ) return false;
        ps = s*1e12;
    }
    if( !( ps >= 1 ) || ps > 1e15 ) return false;       // 1 ps/s … 1000 s/s
    *psPerSec = (uint64_t)llround( ps );
    return true;
}

CircuitWidget* CircuitWidget::m_pSelf = 0l;

CircuitWidget::CircuitWidget( QWidget *parent  )
             : QWidget( parent )
             , m_verticalLayout( this )
             , m_circView( this )
             , m_outPane( this )
             , m_circToolBar( this )
             , m_simToolBar( this )
             , m_infoBar( this )
             , m_fileMenu( this )
             , m_infoMenu( this )
{
    setObjectName( "CircuitWidget" );
    m_pSelf = this;

    m_appPropW = NULL;
    m_about = NULL;

    m_verticalLayout.setObjectName( "verticalLayout" );
    m_verticalLayout.setContentsMargins(0, 0, 0, 0);
    m_verticalLayout.setSpacing(0);

    m_toolRow = new QWidget( this );        // The toolbar in one row, or two when the window is narrow
    m_toolGrid = new QGridLayout( m_toolRow );
    m_toolGrid->setContentsMargins( 0, 0, 0, 0 );
    m_toolGrid->setSpacing( 0 );
    m_toolRow->installEventFilter( this );
    m_circToolBar.installEventFilter( this );   // A label that grows (NOISE, messages) can need the second row
    m_simToolBar.installEventFilter( this );
    m_verticalLayout.addWidget( m_toolRow );

    m_mainSplitter = new QSplitter( this );
    m_mainSplitter->setObjectName("CircuitSplitter");
    m_mainSplitter->setOrientation( Qt::Vertical );
    m_verticalLayout.addWidget( m_mainSplitter );

    m_infoWidget = new InfoWidget( this );
    m_infoWidget->setTargetSpeed( 100 );
    m_infoWidget->setFixedWidth( 322*MainWindow::self()->fontScale() );
    m_infoWidget->setVisible( false );   // Not shown: its numbers go to Messages (pause, stop); it still feeds the toolbar

    m_panelSplitter = new QSplitter( this );
    m_panelSplitter->setObjectName("Panelplitter");
    m_panelSplitter->setOrientation( Qt::Horizontal );
    m_panelSplitter->addWidget( m_infoWidget );
    m_descWidget = new DescriptionWidget( this );
    m_bottomTabs = new QTabWidget( this );
    m_bottomTabs->setObjectName("BottomTabs");
    m_bottomTabs->setDocumentMode( true );
    m_bottomTabs->addTab( m_descWidget, tr("Description") );
    m_msgWidget = new QWidget( this );                // Messages: event log switches above the text
    QVBoxLayout* msgLayout = new QVBoxLayout( m_msgWidget );
    msgLayout->setContentsMargins( 0, 0, 0, 0 );
    msgLayout->setSpacing( 0 );
    QHBoxLayout* logRow = new QHBoxLayout();
    logRow->setContentsMargins( 6, 2, 6, 2 );
    QLabel* logLabel = new QLabel( tr("Log:"), m_msgWidget );
    m_logPartsBox = new QCheckBox( tr("Part states"), m_msgWidget );
    m_logPartsBox->setToolTip( tr("Write every state change of parts that report one (e.g. IC1 supply: ON → HICCUP) with its simulated time. Needs Show Part States on in the toolbar") );
    m_logCapsBox = new QCheckBox( tr("Capacitors"), m_msgWidget );
    m_logCapsBox->setToolTip( tr("Write charging, charged, discharged and over-rating events of capacitors whose \"Log charge\" property is on. Needs Show Capacitor Charge on in the toolbar") );
    logRow->addWidget( logLabel );
    logRow->addWidget( m_logPartsBox );
    logRow->addWidget( m_logCapsBox );
    m_verboseLog = MainWindow::self()->settings()->value("Circuit/logVerbose", false ).toBool();
    m_verboseBox = new QCheckBox( tr("Verbose"), m_msgWidget );
    m_verboseBox->setToolTip( tr("Also write debug logs (lines starting with a [tag]). Off: only information, warnings and errors. Debug logs always go to the terminal (stderr)") );
    m_verboseBox->setChecked( m_verboseLog );
    logRow->addWidget( m_verboseBox );
    logRow->addStretch();
    msgLayout->addLayout( logRow );
    msgLayout->addWidget( &m_outPane );
    m_bottomTabs->addTab( m_msgWidget, tr("Messages") );
    syncEventLog();
    connect( m_logPartsBox, &QCheckBox::toggled, []( bool on ){ EventLog::self()->setLogParts( on ); } );
    connect( m_logCapsBox,  &QCheckBox::toggled, []( bool on ){ EventLog::self()->setLogCaps( on ); } );
    connect( m_verboseBox,  &QCheckBox::toggled, [this]( bool on ){ setVerboseLog( on ); } );
    m_scriptPane = new ScriptPane( this );
    m_bottomTabs->addTab( m_scriptPane, tr("Scripts") );
    connect( m_scriptPane, &ScriptPane::started, [this](){ m_bottomTabs->setCurrentWidget( m_scriptPane ); } );
    m_bottomTabs->setCurrentWidget( m_msgWidget );
    m_panelSplitter->addWidget( m_bottomTabs );
    m_panelSplitter->setSizes( {170, 500} );

    m_mainSplitter->addWidget( &m_circView );
    m_mainSplitter->addWidget( m_panelSplitter );
    m_mainSplitter->setSizes( {500, 100} );

    m_badgeFont = QFont( MainWindow::self()->defaultFontName(), 10, QFont::Bold );   // The NOISE badge
    m_badgeFont.setPixelSize( 14*MainWindow::self()->fontScale() );

    createActions();
    updateRecentFileActions();
    createToolBars();

    m_lastCircDir = MainWindow::self()->settings()->value("lastCircDir").toByteArray();
    if( m_lastCircDir.isEmpty() )
    {
        QString appPath = QCoreApplication::applicationDirPath();
        m_lastCircDir = appPath + "./data/examples";
    }
    newCircuit();
    m_infoWidget->setRate();
}
CircuitWidget::~CircuitWidget() { }

void CircuitWidget::clear()
{
    if( m_appPropW )
    {
        m_appPropW->setParent( NULL );
        m_appPropW->close();
        m_appPropW->deleteLater();
        m_appPropW = NULL;
    }
    m_circView.clear();
    m_infoWidget->clearLastRun();
}

void CircuitWidget::createActions()
{
    for( int i=0; i<MaxRecentFiles; i++ )
    {
        recentFileActs[i] = new QAction( this );
        recentFileActs[i]->setVisible( false );
        connect( recentFileActs[i], &QAction::triggered,
                 this,              &CircuitWidget::openRecentFile, Qt::UniqueConnection );
    }

    newCircAct = new QAction( QIcon(":/new.svg"), tr("New Circuit"), this);
    newCircAct->setStatusTip( tr("Create a new Circuit"));
    connect( newCircAct, &QAction::triggered,
                   this, &CircuitWidget::newCircuit, Qt::UniqueConnection );

    openCircAct = new QAction( QIcon(":/open.svg"), tr("Open Circuit"), this);
    openCircAct->setStatusTip( tr("Open an existing Circuit"));
    connect( openCircAct, &QAction::triggered,
                    this, &CircuitWidget::openCirc, Qt::UniqueConnection );

    saveCircAct = new QAction( QIcon(":/save.svg"), tr("Save Circuit"), this);
    saveCircAct->setStatusTip( tr("Save the Circuit to disk"));
    connect( saveCircAct, &QAction::triggered,
                    this, QOverload<>::of(&CircuitWidget::saveCirc), Qt::UniqueConnection );

    saveCircAsAct = new QAction( QIcon(":/saveas.svg"),tr("Save Circuit As..."), this);
    saveCircAsAct->setStatusTip( tr("Save the Circuit under a new name"));
    connect( saveCircAsAct, &QAction::triggered,
                      this, &CircuitWidget::saveCircAs, Qt::UniqueConnection );

    undoAct = new QAction( QIcon(":/undo.svg"), tr("Undo"), this );
    undoAct->setStatusTip( tr("Undo the last change to the circuit") );
    connect( undoAct, &QAction::triggered, this, [](){ Circuit::self()->undo(); } );

    redoAct = new QAction( QIcon(":/redo.svg"), tr("Redo"), this );
    redoAct->setStatusTip( tr("Redo the last undone change") );
    connect( redoAct, &QAction::triggered, this, [](){ Circuit::self()->redo(); } );

    QTimer* undoTimer = new QTimer( this ); // Grey the buttons out when there is nothing to undo/redo
    connect( undoTimer, &QTimer::timeout, this, [this](){
        Circuit* c = Circuit::self();
        undoAct->setEnabled( c && c->canUndo() );
        redoAct->setEnabled( c && c->canRedo() );
    });
    undoTimer->start( 200 );

    zoomFitAct = new QAction( QIcon(":/zoomfit.svg"),tr("Zoom to fit"), this);
    zoomFitAct->setStatusTip( tr("Zoom Circuit to fit all components"));
    connect( zoomFitAct, &QAction::triggered,
            CircuitView::self(), &CircuitView::zoomToFit, Qt::UniqueConnection );

    zoomSelAct = new QAction( QIcon(":/zoomsel.svg"),tr("Zoom to selected"), this);
    zoomSelAct->setStatusTip( tr("Zoom Circuit to fit all selected components"));
    connect( zoomSelAct, &QAction::triggered,
            CircuitView::self(), &CircuitView::zoomSelected, Qt::UniqueConnection );

    zoomOneAct = new QAction( QIcon(":/zoomone.svg"),tr("Zoom to Scale 1"), this);
    zoomOneAct->setStatusTip( tr("Zoom Circuit to Scale 1:1"));
    connect( zoomOneAct, &QAction::triggered,
             CircuitView::self(), &CircuitView::zoomOne, Qt::UniqueConnection );

    powerCircAct = new QAction( QIcon(":/simplay.svg"),tr("Start Simulation"), this);
    powerCircAct->setStatusTip(tr("Start Simulation"));
    powerCircAct->setIconText("Off");
    connect( powerCircAct, &QAction::triggered,
                     this, &CircuitWidget::powerCirc, Qt::UniqueConnection );

    skipFwdAct = new QAction( QIcon(":/simskipfwd.svg"),tr("Skip Forward"), this);
    skipFwdAct->setStatusTip(tr("Pause and skip forward by the skip interval"));
    connect( skipFwdAct, &QAction::triggered, this, [this](){ skipForward( 1 ); } );

    skipBackAct = new QAction( QIcon(":/simskipback.svg"),tr("Skip Back"), this);
    skipBackAct->setStatusTip(tr("Pause and go back by the skip interval (keyframes of the run)"));
    connect( skipBackAct, &QAction::triggered, this, [this](){ skipBack( 1 ); } );

    pauseSimAct = new QAction( QIcon(":/simpause.svg"),tr("Pause Simulation"), this);
    pauseSimAct->setStatusTip(tr("Pause Simulation"));
    connect( pauseSimAct, &QAction::triggered,
             this, &CircuitWidget::pauseCirc, Qt::UniqueConnection );

    currentFlowAct = new QAction( QIcon(":/currentflow.svg"),tr("Show Current Flow"), this);
    currentFlowAct->setStatusTip(tr("Show current flowing through the wires as moving dots"));
    currentFlowAct->setCheckable( true );
    currentFlowAct->setChecked( CurrentFlow::self()->enabled() );
    connect( currentFlowAct, &QAction::toggled,
             []( bool on ){ CurrentFlow::self()->setEnabled( on ); } );

    capChargeAct = new QAction( QIcon(":/capcharge.svg"),tr("Show Capacitor Charge and Inductor Current"), this);
    capChargeAct->setStatusTip(tr("Show the energy stored in capacitors and inductors: a gauge beside each one, capacitor plates coloured by voltage, coils by current"));
    capChargeAct->setCheckable( true );
    capChargeAct->setChecked( ChargeView::self()->enabled() );
    connect( capChargeAct, &QAction::toggled,
             []( bool on ){ ChargeView::self()->setEnabled( on ); } );

    partStatusAct = new QAction( QIcon(":/partstatus.svg"),tr("Show Part States"), this);
    partStatusAct->setStatusTip(tr("Show the states parts report (start-up, running, fault...): badges above each part and its outline coloured by the most severe one"));
    partStatusAct->setCheckable( true );
    partStatusAct->setChecked( StatusView::self()->enabled() );
    connect( partStatusAct, &QAction::toggled,
             []( bool on ){ StatusView::self()->setEnabled( on ); } );

    instrumentsAct = new QAction( QIcon(":/instruments.svg"),tr("Show Instruments"), this);
    instrumentsAct->setStatusTip(tr("Show the scopes, voltmeters, frequency meters and probes with their leads; off: the circuit alone (they keep measuring)"));
    instrumentsAct->setCheckable( true );
    instrumentsAct->setChecked( InstrumentView::self()->shown() );
    connect( instrumentsAct, &QAction::toggled,
             []( bool on ){ InstrumentView::self()->setShown( on ); } );

    noiseOnAct = new QAction( QIcon(":/noise.svg"),tr("Transient Noise"), this);
    noiseOnAct->setStatusTip(tr("Add the thermal and shot noise of the parts (Settings: Simulation: Noise for bandwidth, scale, seed and window). While running, the simulation restarts"));
    noiseOnAct->setCheckable( true );
    connect( noiseOnAct, &QAction::toggled, this, &CircuitWidget::toggleNoise );

    exportAct = new QAction( QIcon(":/saveimage.svg"),tr("Export as Image (PNG, SVG)..."), this);
    exportAct->setStatusTip(tr("Export the circuit as a PNG or SVG image, with a choice of theme, instruments and grid"));
    connect( exportAct, &QAction::triggered, this, [this](){ ExportDialog::run( this ); } );

    settAppAct = new QAction( QIcon(":/config.svg"),tr("Settings"), this);
    settAppAct->setStatusTip(tr("Settings"));
    connect( settAppAct, &QAction::triggered,
                   this, &CircuitWidget::settApp, Qt::UniqueConnection );

    infoAct = new QAction( QIcon(":/help.svg"),tr("SimulIDE Website"), this);
    infoAct->setStatusTip(tr("SimulIDE Website"));
    connect( infoAct, &QAction::triggered,
                this, &CircuitWidget::openInfo, Qt::UniqueConnection );
    
    aboutAct = new QAction( QIcon(":/about.svg"),tr("About SimulIDE"), this);
    aboutAct->setStatusTip(tr("About SimulIDE"));
    connect( aboutAct, &QAction::triggered,
                 this, &CircuitWidget::about, Qt::UniqueConnection );
    
    aboutQtAct = new QAction( QIcon(":/about.svg"),tr("About Qt"), this);
    aboutQtAct->setStatusTip(tr("About Qt"));
    connect( aboutQtAct, &QAction::triggered,
                   qApp, &QApplication::aboutQt, Qt::UniqueConnection );

    // Toolbar keys. The tooltip shows each one as this platform writes it (⌘, on macOS,
    // Ctrl+, elsewhere). File and edit keys and the zoom keys are read on the canvas
    // (Circuit::keyPressEvent, KeyboardController::zoomKey), so the code editor keeps
    // its own Ctrl+N, Ctrl+S ...; the others are window shortcuts (QAction)
    KeyText::tipWithKey( newCircAct,    "Ctrl+N" );
    KeyText::tipWithKey( openCircAct,   "Ctrl+O" );
    KeyText::tipWithKey( saveCircAct,   "Ctrl+S" );
    KeyText::tipWithKey( saveCircAsAct, "Ctrl+Shift+S" );
    KeyText::tipWithKey( undoAct,       "Ctrl+Z" );
    KeyText::tipWithKey( redoAct,       "Ctrl+Shift+Z" );
    KeyText::tipWithKey( zoomFitAct,    "Ctrl+Alt+0" );  // ⌥⌘0
    KeyText::tipWithKey( zoomSelAct,    "Ctrl+Meta+0" ); // ⌃⌘0
    KeyText::tipWithKey( zoomOneAct,    "Ctrl+0" );
    const QList<QPair<QAction*, QString>> keys{
        { settAppAct,     "Ctrl+," }, { exportAct,      "Ctrl+P" },
        { powerCircAct,   "F5" },     { pauseSimAct,    "F6" },
        { currentFlowAct, "F7" },     { capChargeAct,   "F8" },
        { partStatusAct,  "F9" },     { instrumentsAct, "F10" },
        { noiseOnAct,     "F12" } };  // F11 is Show Desktop on macOS
    for( auto& k : keys )
    {
        k.first->setShortcut( QKeySequence( k.second, QKeySequence::PortableText ) );
        KeyText::tipWithKey( k.first, k.second );
    }
    KeyText::tipWithKey( skipFwdAct, "Right" );    // Read by the key filter, not a shortcut
    qApp->installEventFilter( this );               // Simulation keys (eventFilter, simKey)
    connect( qApp, &QApplication::focusChanged, this, [this](){ updtKeyOwner(); } );
}

void CircuitWidget::createToolBars()
{
    m_circToolBar.setObjectName( "m_circToolBar" );

    double fs = MainWindow::self()->fontScale()*20;
    m_circToolBar.setIconSize( QSize( fs, fs ) );
    m_simToolBar.setObjectName( "m_simToolBar" );
    m_simToolBar.setIconSize( QSize( fs, fs ) );

    m_circToolBar.addAction( settAppAct );
    QWidget* spacer = new QWidget();
    spacer->setFixedWidth( 15 );
    m_circToolBar.addWidget( spacer );
    m_circToolBar.addSeparator();//..........................

    for( int i=0; i<MaxRecentFiles; i++ ) m_fileMenu.addAction( recentFileActs[i] );
    QToolButton* fileButton = new QToolButton( this );
    fileButton->setToolTip( tr("Last Circuits") );
    fileButton->setMenu( &m_fileMenu );
    fileButton->setIcon( QIcon(":/lastfiles.svg") );
    fileButton->setPopupMode( QToolButton::InstantPopup );
    m_circToolBar.addWidget( fileButton );

    m_circToolBar.addAction( newCircAct );

    m_circToolBar.addAction( openCircAct );
    m_circToolBar.addAction( saveCircAct );
    m_circToolBar.addAction( saveCircAsAct );
    m_circToolBar.addAction( exportAct );
    spacer = new QWidget();
    spacer->setFixedWidth( 15 );
    m_circToolBar.addWidget( spacer );
    m_circToolBar.addSeparator();//..........................

    m_circToolBar.addAction( undoAct );
    m_circToolBar.addAction( redoAct );
    spacer = new QWidget();
    spacer->setFixedWidth( 15 );
    m_circToolBar.addWidget( spacer );
    m_circToolBar.addSeparator();//..........................

    m_circToolBar.addAction( zoomFitAct );
    m_circToolBar.addAction( zoomSelAct );
    m_circToolBar.addAction( zoomOneAct );
    spacer = new QWidget();
    spacer->setFixedWidth( 20 );
    m_circToolBar.addWidget( spacer );
    m_circToolBar.addSeparator();//..........................
    // View toggles in the first row: the second keeps speed, run and status (room on a narrow window)
    m_circToolBar.addAction( currentFlowAct );
    m_circToolBar.addAction( capChargeAct );
    m_circToolBar.addAction( partStatusAct );
    m_circToolBar.addAction( instrumentsAct );
    m_circToolBar.addAction( noiseOnAct );

    // Timebase: simulated time per real second (like a speed dial), before play
    m_timebase = new SpeedBox( this );
    m_timebase->setToolTip( tr("Simulation speed: simulated time per real second.\n"
                               "1 s/s is real time. Faster than the computer can manage\n"
                               "runs as fast as it can (see Real Speed).") );
    m_timebase->setSizeAdjustPolicy( QComboBox::AdjustToContents );
    const QList<QPair<QString,double>> presets{
        { "1 µs/s",   1e6  }, { "10 µs/s", 1e7  }, { "100 µs/s", 1e8  },
        { "1 ms/s",   1e9  }, { "10 ms/s", 1e10 }, { "100 ms/s", 1e11 },
        { "0.5 s/s",  5e11 }, { tr("1 s/s (real time)"), 1e12 },
        { "2 s/s",    2e12 }, { "5 s/s",   5e12 }, { "10 s/s",  1e13 } };
    for( auto& p : presets ) m_timebase->addItem( p.first, QVariant::fromValue( (qulonglong)p.second ) );
    connect( m_timebase, QOverload<int>::of( &QComboBox::activated ), [this]( int i )
    {
        qulonglong ps = m_timebase->itemData( i ).toULongLong();
        if( ps == 0 ) return;                          // The "custom" entry
        Cli::setSimSpeed( ps );                        // Adjusts the step size if needed
        Circuit::self()->setChanged();                 // Speed is saved in the circuit
        skipFollowSpeed();
        m_timebase->lineEdit()->clearFocus();
    });
    connect( m_timebase->lineEdit(), &QLineEdit::returnPressed, [this]()   // A typed speed
    {
        uint64_t ps = 0;
        QString typed = m_timebase->lineEdit()->text();
        if( !parseSpeed( typed, &ps ) )
        {
            m_timebase->setBad( true );
            qWarning().noquote() << "[sim] Speed? "+typed+" (e.g. 25%, 3 ms/s; 1 ps/s … 1000 s/s)";
            return;
        }
        QString note = Cli::setSimSpeed( ps );
        Circuit::self()->setChanged();
        skipFollowSpeed();
        m_timebase->setBad( false );
        m_timebase->lineEdit()->clearFocus();
        if( !note.isEmpty() ) qInfo().noquote() << "Speed:" << note;
    });
    m_simRunItems << m_simToolBar.addWidget( m_timebase );

    // Speed lock (specs/sim-control.md §3.9): hold the set speed by coarsening the reactive step
    m_lockAct = new QAction( QIcon(":/simlock.svg"), tr("Speed lock"), this );
    m_lockAct->setCheckable( true );
    m_lockAct->setToolTip( tr("Speed lock: hold the set speed. While the computer can't keep up, capacitors and\n"
                              "inductors use a coarser step (up to the max. reactive step, Settings: Simulation).\n"
                              "Less accurate; skips keep the normal step. Saved with the circuit.") );
    connect( m_lockAct, &QAction::toggled, this, [this]( bool on ){ setSpeedLock( on ); } );
    m_simToolBar.addAction( m_lockAct );
    m_simRunItems << m_lockAct;
    m_lockLabel = new QLabel( this );
    m_lockLabel->setFont( m_badgeFont );
    m_lockLabel->setStyleSheet("QLabel { color: white; background-color: #e67800; padding: 1px 5px; }");
    m_lockBadgeAct = m_simToolBar.addWidget( m_lockLabel );
    m_lockBadgeAct->setVisible( false );
    syncTimebase( Simulator::self() ? Simulator::self()->psPerSec() : (uint64_t)1e12 );

    // Skip interval (specs/sim-control.md §3.3): ↑ ↓ change it, → / ⏭ skip by it
    m_skipBox = new SpeedBox( this );
    m_skipBox->setToolTip( tr("Skip interval: → or ⏭ pauses and skips this far (1 step = to the next solver event).\n"
                              "↑ ↓ change it (⇧ ×5, ⌥ ×10, ⇧⌥ ×100) when the canvas has no keyboard focus;\n"
                              "type a time (5us, 2.5 ms, 1 min) and press Enter.") );
    m_skipBox->setSizeAdjustPolicy( QComboBox::AdjustToContents );
    for( uint64_t v : SimSkip::ladder() ) m_skipBox->addItem( SimSkip::intervalText( v ), QVariant::fromValue( (qulonglong)v ) );
    connect( m_skipBox, QOverload<int>::of( &QComboBox::activated ), [this]( int i )
    {
        qulonglong v = m_skipBox->itemData( i ).toULongLong();
        if( v == (qulonglong)-1 ) return;              // The typed entry
        setSkipInterval( v );
        m_skipBox->lineEdit()->clearFocus();
    });
    connect( m_skipBox->lineEdit(), &QLineEdit::returnPressed, [this]()
    {
        uint64_t ps = 0;
        QString typed = m_skipBox->lineEdit()->text();
        if( !SimSkip::parseInterval( typed, &ps ) )
        {
            m_skipBox->setBad( true );
            qWarning().noquote() << "[sim] Skip interval? "+typed+" (1 step, or 1 ps … 1 min)";
            return;
        }
        m_skipBox->setBad( false );
        setSkipInterval( ps );
        m_skipBox->lineEdit()->clearFocus();
    });
    m_simRunItems << m_simToolBar.addWidget( m_skipBox );
    syncSkip( Simulator::self() ? Simulator::self()->skipInterval() : 1000000000 );

    m_simToolBar.addAction( skipBackAct );
    m_simToolBar.addAction( powerCircAct );
    m_simToolBar.addAction( pauseSimAct );
    m_simToolBar.addAction( skipFwdAct );
    m_simRunItems << skipBackAct << pauseSimAct << skipFwdAct;

    QLabel* timeLabel = new QLabel( this );            // Simulation time, next to the run buttons
    QFont tf = timeLabel->font();
    tf.setPixelSize( 12*MainWindow::self()->fontScale() );
    timeLabel->setFont( tf );
    timeLabel->setContentsMargins( 6*MainWindow::self()->fontScale(), 0, 0, 0 );
    m_simRunItems << m_simToolBar.addWidget( timeLabel );
    m_infoWidget->setToolbarTime( timeLabel );
    m_simRunItems << m_simToolBar.addSeparator();//..........................
    // No status block: the time's colour gives the state (green running, light grey paused),
    // other status messages go to Messages (setMsg)

    // Transient noise on: in sight while the simulation runs (docs/NOISE_SPEC.md). The white noise
    // icon stands for the word
    double ns = MainWindow::self()->fontScale();
    int boxH = 20*ns;
    m_noiseBox = new QWidget( this );
    m_noiseBox->setObjectName("noiseBadge");
    m_noiseBox->setAttribute( Qt::WA_StyledBackground, true );
    m_noiseBox->setStyleSheet("#noiseBadge { background-color: #e67800; }");
    m_noiseBox->setFixedHeight( boxH );
    QHBoxLayout* nl = new QHBoxLayout( m_noiseBox );
    nl->setContentsMargins( 4*ns, 0, 6*ns, 0 );
    nl->setSpacing( 3*ns );
    QLabel* noiseIcon = new QLabel( m_noiseBox );
    noiseIcon->setPixmap( QIcon(":/noise_white.svg").pixmap( QSize( boxH-2, boxH-2 ) ) );
    m_noiseLabel = new QLabel( m_noiseBox );
    m_noiseLabel->setFont( m_badgeFont );
    m_noiseLabel->setStyleSheet("QLabel { color: white; }");
    nl->addWidget( noiseIcon );
    nl->addWidget( m_noiseLabel );
    m_noiseBox->setToolTip( tr("Transient noise is on: thermal and shot noise of the parts are added (Settings: Simulation, or CLI settings noise*)") );
    m_simRunItems << m_simToolBar.addWidget( spacer = new QWidget() );
    spacer->setFixedWidth( 8 );
    m_noiseAct = m_simToolBar.addWidget( m_noiseBox );
    m_noiseAct->setVisible( false );

    spacer = new QWidget( this );
    spacer->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Preferred );
    spacer->setVisible( true );
    m_simToolBar.addWidget( spacer );

    m_infoMenu.addAction( infoAct );
    m_infoMenu.addAction( aboutAct );
    m_infoMenu.addAction( aboutQtAct );

    // Info menu: at the right end of the first row, whatever the simulation shows
    m_infoBar.setObjectName( "m_infoBar" );
    m_infoBar.setIconSize( QSize( fs, fs ) );
    QToolButton* infoButton = new QToolButton( this );
    infoButton->setToolTip( tr("Info") );
    infoButton->setMenu( &m_infoMenu );
    infoButton->setIcon( QIcon(":/help.svg") );
    infoButton->setPopupMode( QToolButton::InstantPopup );
    m_infoBar.addWidget( infoButton );

    setSimExpanded( false );          // Stopped: only the play button
}

// Stopped: the simulation bar is only the play button, beside the first row. Running, paused or
// debugging: all of it (speed, skip, run buttons, time, NOISE badge) in a row of its own
void CircuitWidget::setSimExpanded( bool on )
{
    m_simExpanded = on;
    for( QAction* a : m_simRunItems ) a->setVisible( on );
    updateNoise();                    // The NOISE badge follows the bar
    updateLock();
    m_toolRows = 0;                   // Lay out again
    layoutToolBars();
}

void CircuitWidget::layoutToolBars()
{
    if( !m_toolGrid ) return;
    // QToolBar's size hint is the width with every visible item shown
    int need = m_circToolBar.sizeHint().width()+m_simToolBar.sizeHint().width()+m_infoBar.sizeHint().width();
    int rows = ( m_toolRow->width() > 0 && m_toolRow->width() < need ) ? 2 : 1;
    if( m_simExpanded ) rows = 2;
    if( rows == m_toolRows ) return;
    m_toolRows = rows;
    m_toolGrid->removeWidget( &m_circToolBar );
    m_toolGrid->removeWidget( &m_simToolBar );
    m_toolGrid->removeWidget( &m_infoBar );
    m_toolGrid->setColumnStretch( 0, 0 );
    m_toolGrid->setColumnStretch( 1, 0 );
    if( rows == 1 ){
        m_toolGrid->addWidget( &m_circToolBar, 0, 0 );
        m_toolGrid->addWidget( &m_simToolBar,  0, 1 );
        m_toolGrid->setColumnStretch( 1, 1 );       // The play button's bar takes the rest
    }else{
        m_toolGrid->addWidget( &m_circToolBar, 0, 0 );
        m_toolGrid->addWidget( &m_simToolBar,  1, 0, 1, 3 );
        m_toolGrid->setColumnStretch( 1, 1 );
    }
    m_toolGrid->addWidget( &m_infoBar, 0, 2 );      // Info: right end of the first row
}

bool CircuitWidget::eventFilter( QObject* obj, QEvent* ev )
{
    if( ev->type() == QEvent::KeyPress && obj->isWidgetType() )
    {
        QWidget* w = static_cast<QWidget*>( obj );
        if( w->window() == window() && simKey( static_cast<QKeyEvent*>( ev ) ) ) return true;
        return false;
    }
    if( ev->type() == QEvent::MouseButtonPress && obj->isWidgetType()
     && ( m_circView.hasFocus() || KeyboardController::self()->focused() ) )   // Headless: the flag
    {
        // A click anywhere else in the window (toolbar, Messages, a panel) takes the keyboard
        // from the canvas (specs/sim-control.md §3.1); a widget that takes focus gets it next
        QWidget* w = static_cast<QWidget*>( obj );
        if( w->window() == window() && w != &m_circView && !m_circView.isAncestorOf( w ) )
        {
            m_circView.clearFocus();
            KeyboardController::self()->setFocused( false );
        }
    }
    if( obj == m_toolRow && ev->type() == QEvent::Resize ) layoutToolBars();
    else if( ( obj == &m_circToolBar || obj == &m_simToolBar ) && ev->type() == QEvent::LayoutRequest )
        QTimer::singleShot( 0, this, [this](){ layoutToolBars(); } );   // After the bar's own layout
    return QWidget::eventFilter( obj, ev );
}

bool CircuitWidget::newCircuit()
{
    if( EditorWindow::self() && EditorWindow::self()->debugStarted() ) EditorWindow::self()->stop();
    else if( Simulator::self() ) powerCircOff();
    
    if( MainWindow::self()->windowTitle().endsWith('*') )
    {
        const QMessageBox::StandardButton ret
        = QMessageBox::warning(this, "CircuitWidget::newCircuit",
                               tr("\nCircuit has been modified.\n"
                                  "Do you want to save your changes?\n"),
          QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
                               
        if     ( ret == QMessageBox::Save )   saveCirc();
        else if( ret == QMessageBox::Cancel ) return false;
    }
    clear();

    Circuit::self()->setAutoBck( MainWindow::self()->autoBck() );

    m_curCirc = "";
    updtBackAvail( false );
    Simulator::self()->addToUpdateList( &m_outPane );

    MainWindow::self()->setFile( tr("New Circuit"));
    showDescription();
    MainWindow::self()->settings()->setValue( "lastCircDir", m_lastCircDir );
    updateNoise();   // A new circuit has noise off
    updateLock();
    return true;
}

void CircuitWidget::openRecentFile()
{
    QAction* action = qobject_cast<QAction*>( sender() );
    if( !action ) return;

    QString file = action->data().toString();
    QFile pfile( file );
    if( pfile.exists() ) loadCirc( file );
    else{
        const QMessageBox::StandardButton ret
        = QMessageBox::warning( this, "CircuitWidget::openRecentFile",
                               tr("\nCan't find file:\n")+
                               file+"\n\n"+
                               tr("Do you want to remove it from Recent Circuits?\n"),
          QMessageBox::Yes | QMessageBox::No );

        if( ret != QMessageBox::Yes ) return;

        QSettings* settings = MainWindow::self()->settings();
        QStringList files = settings->value("recentCircList").toStringList();
        files.removeAll( file );
        settings->setValue("recentCircList", files );
        updateRecentFileActions();
    }
}

void CircuitWidget::openCirc()
{
    const QString dir = m_lastCircDir;
    QString fileName = QFileDialog::getOpenFileName( 0l, tr("Load Circuit"), dir,
                                        tr("Circuits (*.sim*);;All files (*.*)"));
    loadCirc( fileName );
}

void CircuitWidget::loadCirc( QString path )
{
    if( !path.isEmpty()
      &&(path.endsWith(".simu") || path.endsWith(".sim1")) )
    {
        if( !newCircuit() ) return;
        Circuit::self()->loadCircuit( path );
   
        m_curCirc = path;
        m_lastCircDir = path;
        MainWindow::self()->setFile(path.split("/").last());

        QSettings* settings = MainWindow::self()->settings();
        settings->setValue( "lastCircDir", m_lastCircDir );

        updateRecentFiles();

        m_infoWidget->clearLastRun();
        showDescription();
        updateNoise();   // The noise button follows the file
        updateLock();
        updtBackAvail( true );   // A warning when going back is off for it
}   }

void CircuitWidget::printDescription()
{
    QString desc = Circuit::self() ? Circuit::self()->description() : QString();
    if( !desc.isEmpty() ) m_outPane.appendPlain( desc+"\n", Qt::white );
}

void CircuitWidget::showDescription()
{
    m_scriptPane->refresh();                     // Scripts next to this circuit
    m_descWidget->showCircuit();                 // The Messages tab stays in front: it prints the description too
}

void CircuitWidget::saveCirc()
{
    if( m_curCirc.isEmpty() ) saveCircAs();
    else                      saveCirc( m_curCirc );
}

void CircuitWidget::saveCircAs()
{
    const QString dir = m_lastCircDir;
    QString fileName = QFileDialog::getSaveFileName( this, tr("Save Circuit"), dir,
                                                     tr("Circuits (*.sim1);;All files (*.*)") );
    if( fileName.isEmpty() ) return;

    saveCirc( fileName );
}

void CircuitWidget::saveCirc( QString file )
{
    if     (  file.endsWith(".simu") ) file = changeExt( file, ".sim1" );
    else if( !file.endsWith(".sim1") ) file.append(".sim1");

    if( Circuit::self()->saveCircuit( file ) )
    {
        m_curCirc = file;
        m_lastCircDir = file;
        MainWindow::self()->setFile( file.split("/").last() );
        MainWindow::self()->settings()->setValue( "lastCircDir", m_lastCircDir );
        updateRecentFiles();
    }
    else qDebug() << "\nError Saving Circuit:\n" << file;
}

void CircuitWidget::powerCirc()
{
    if( Simulator::self()->isPaused() )          powerCircOff();
    else if( powerCircAct->iconText() == "Off" ) powerCircOn();
    else if( powerCircAct->iconText() == "On" )  powerCircOff();
}

void CircuitWidget::powerCircOn()
{
    powerCircAct->setIcon( QIcon(":/simstop.svg") );
    powerCircAct->setText(tr("Stop Simulation"));
    powerCircAct->setEnabled( true );
    powerCircAct->setIconText("On");
    pauseSimAct->setIcon( QIcon(":/simpause.svg") );
    pauseSimAct->setEnabled( true );
    MainWindow::self()->setState("▶");

    CurrentFlow::self()->attach();       // Each circuit has a new Simulator
    ChargeView::self()->attach();
    StatusView::self()->attach();
    EventLog::self()->attach();
    m_loggedMsgs.clear();                // Status messages: once per run
    setSimExpanded( true );
    m_infoWidget->setSimState( InfoWidget::shownRunning );
    Simulator::self()->startSim();
    updtBackAvail( false );              // Parts added since the load
    m_skipPreset = 0;                    // The interval as it is; the real speed moves it (§3.3)
    m_circView.clearFocus();             // The simulation keys work (specs/sim-control.md §3.1)
    updtKeyOwner();
}

void CircuitWidget::powerCircOff()
{
    if( Simulator::self()->isRunning() ){
        if( !Simulator::self()->isPaused() ) logRunInfo( tr("Stopped") );   // Paused: logged then
        Simulator::self()->stopSim();
    }

    powerCircAct->setIcon( QIcon(":/simplay.svg") );
    pauseSimAct->setIcon( QIcon(":/simpause.svg") );
    powerCircAct->setText(tr("Start Simulation"));
    powerCircAct->setIconText("Off");
    powerCircAct->setEnabled( true );
    pauseSimAct->setEnabled( false );
    pauseSimAct->setText(tr("Pause Simulation"));
    MainWindow::self()->setState("■");

    m_infoWidget->setRate( 0, 0 );
    m_infoWidget->setSimState( InfoWidget::shownStopped );
    updtKeyOwner();
    setSimExpanded( false );
    CurrentFlow::self()->clear();
    ChargeView::self()->clear();
    EventLog::self()->stop();
    StatusView::self()->clear();
    Circuit::self()->update();
}

void CircuitWidget::powerCircDebug()
{
    if( Simulator::self()->isRunning() ) Simulator::self()->stopSim();

    powerCircAct->setIcon( QIcon(":/powerdeb.png") );
    powerCircAct->setIconText("Debug");
    powerCircAct->setEnabled( false );
    pauseSimAct->setEnabled( false );
    MainWindow::self()->setState("❚❚");

    m_infoWidget->setRate(-1 );  //m_rateLabel->setText( tr("Speed: Debugger") );
    setSimExpanded( true );

    CurrentFlow::self()->attach();       // Debugger start has its own path
    ChargeView::self()->attach();
    m_infoWidget->setSimState( InfoWidget::shownPaused );
    Simulator::self()->startSim( true );
}

void CircuitWidget::pauseDebug()
{
    m_paused = false;
    Simulator::self()->pauseSim();
}

void CircuitWidget::debugPaused()
{
    if( m_paused ) return;
    m_paused = true;

    MainWindow::self()->setState("❚❚");
}

void CircuitWidget::resumeDebug()
{
    MainWindow::self()->setState("▶");
    Simulator::self()->resumeSim();
}

void CircuitWidget::pauseCirc()
{
    if( Simulator::self()->simState() > SIM_PAUSED )
    {
        Simulator::self()->pauseSim();
        if( !SimSkip::self()->active() ) logRunInfo( tr("Paused") );
        MainWindow::self()->setState("❚❚");
        pauseSimAct->setText(tr("Resume Simulation"));
        pauseSimAct->setIcon( QIcon(":/simresume.svg") );
        powerCircAct->setIconText("Off");   // powerCirc(): power while paused stops (isPaused branch)
        // The power icon stays on: paused is not stopped (the circuit keeps its state)
        powerCircAct->setText(tr("Stop Simulation (resets to 0 s)"));
        m_infoWidget->setSimState( InfoWidget::shownPaused );
    }
    else if( Simulator::self()->isPaused() )
    {
        Simulator::self()->resumeSim();
        MainWindow::self()->setState("▶");
        pauseSimAct->setText(tr("Pause Simulation"));
        pauseSimAct->setIcon( QIcon(":/simpause.svg") );
        powerCircAct->setIcon( QIcon(":/simstop.svg") );
        powerCircAct->setIconText("On");
        powerCircAct->setText(tr("Stop Simulation"));
        m_infoWidget->setSimState( InfoWidget::shownRunning );
    }
}

void CircuitWidget::settApp()
{
    if( !m_appPropW )
    {
        m_appPropW = new AppDialog( this );
        QPoint p = mapToGlobal( QPoint(50, 50) );
        m_appPropW->move( p.x(), p.y() );
    }
    m_appPropW->show();
}

void CircuitWidget::openInfo()
{ QDesktopServices::openUrl(QUrl("http://simulide.com")); }

void CircuitWidget::about()
{
    if( !m_about ) m_about = new AboutDialog( this );
    m_about->show();
}

void CircuitWidget::setError( QString error )
{
    setMsg( error, 2 );
    m_infoWidget->setRate( -1 );
}

void CircuitWidget::toggleNoise( bool on )
{
    Simulator* sim = Simulator::self();
    if( !sim || sim->noise().on == on ) return;
    sim->noise().on = on;
    Circuit::self()->setChanged();   // Saved with the circuit
    if( sim->isRunning() || sim->isPaused() ){ powerCircOff(); powerCircOn(); } // Noise starts with the simulation
    updateNoise();
}

void CircuitWidget::setSpeedLock( bool on )
{
    Simulator* sim = Simulator::self();
    if( !sim ) return;
    if( sim->speedLock() != on ){
        sim->setSpeedLock( on );
        Circuit::self()->setChanged();   // Saved with the circuit
    }
    updateLock();
}

void CircuitWidget::updateLock()
{
    Simulator* sim = Simulator::self();
    if( !m_lockAct || !sim ) return;
    if( m_lockAct->isChecked() != sim->speedLock() ){
        QSignalBlocker b( m_lockAct );
        m_lockAct->setChecked( sim->speedLock() );
    }
    bool show = m_simExpanded && sim->speedLock() && ( sim->stepScale() > 1 || sim->lockLimited() );
    QString t = show ? tr("step ×%1").arg( sim->stepScale() )+( sim->lockLimited() ? " max" : "" ) : "";
    if( m_lockLabel->text() != t )
    {
        m_lockLabel->setText( t );
        m_lockLabel->setToolTip( tr("Speed lock: capacitors and inductors integrate with %1 (reactive step %2 × %3, max. %4)")
            .arg( Simulator::siStr( sim->lockedStep()*1e-12, "s" ), Simulator::siStr( sim->reactStep()*1e-12, "s" ) )
            .arg( sim->stepScale() ).arg( Simulator::siStr( sim->lockMaxStep()*1e-12, "s" ) )
            +( sim->lockLimited() ? "\n"+tr("Can't keep up even so: the speed shown is the real one") : "" ) );
    }
    if( m_lockBadgeAct->isVisible() != show ) m_lockBadgeAct->setVisible( show );
}

void CircuitWidget::updateNoise()
{
    if( noiseOnAct && Simulator::self() && noiseOnAct->isChecked() != Simulator::self()->noise().on )
    {
        QSignalBlocker b( noiseOnAct );
        noiseOnAct->setChecked( Simulator::self()->noise().on );
    }
    if( !m_noiseLabel ) return;
    QString t = Simulator::self() ? Simulator::self()->noiseBadge( true ) : "";
    if( t.startsWith("NOISE ") ) t = t.mid( 6 );   // The icon says it
    if( m_noiseLabel->text() != t ){
        m_noiseLabel->setText( t );
        m_noiseBox->setToolTip( Simulator::self()->noiseBadge()
            +"\n"+tr("Thermal and shot noise of the parts are added (Settings: Simulation: Noise, or CLI settings noise*)") );
    }
    bool show = !t.isEmpty() && m_simExpanded;
    if( m_noiseAct->isVisible() != show ) m_noiseAct->setVisible( show );
}

// Status messages: there is no status block in the toolbar. The state words (Running, Paused,
// Stopped, Debug) are given by the time's colour; any other message goes to Messages, once per
// run (a warning raised at every non-converged step, which EventLog details in red, comes once)
void CircuitWidget::setMsg( QString msg, int type )
{
    QString t = msg.trimmed();
    static const QStringList states{ tr("Running"), tr("Paused"), tr("Stopped"), "Running", "Paused", "Stopped" };
    if( t.isEmpty() || states.contains( t ) || t.startsWith( tr("Debug") ) || m_loggedMsgs.contains( t ) ) return;
    m_loggedMsgs.insert( t );
    if     ( type == 2 ) qCritical().noquote() << "[sim]" << t;
    else if( type == 1 ) qWarning().noquote() << "[sim]" << t;
    else qInfo().noquote() << t;
}

void CircuitWidget::updateRecentFiles()
{
    QSettings* settings = MainWindow::self()->settings();
    QStringList files = settings->value("recentCircList").toStringList();
    files.removeAll( m_curCirc );
    files.prepend( m_curCirc );
    while( files.size() > MaxRecentFiles ) files.removeLast();

    settings->setValue("recentCircList", files );
    updateRecentFileActions();
}

void CircuitWidget::updateRecentFileActions()
{
    QSettings* settings = MainWindow::self()->settings();
    QStringList files = settings->value("recentCircList").toStringList();

    int numRecentFiles = qMin( files.size(), (int)MaxRecentFiles );

    for( int i=0; i<numRecentFiles; i++ )
    {
        QString text = tr("&%1 %2").arg(i + 1).arg( getFileName( files[i] ) );
        recentFileActs[i]->setText(text);
        recentFileActs[i]->setData( files[i] );
        recentFileActs[i]->setVisible( true );
    }
    for( int i=numRecentFiles; i<MaxRecentFiles; i++ ) recentFileActs[i]->setVisible(false);
}

// Simulation keys (specs/sim-control.md §3.1–3.2). They act when the keyboard focus is in none
// of the widgets that use these keys themselves: the canvas (keyboard editor, D1), a text field,
// a list or tree while stopped, an editable combo. At a start the canvas gives up the focus; a click gives it
// back, Esc on an idle canvas hands the keys to the simulation again (D11)
bool CircuitWidget::keysToSim()
{
    if( QApplication::activeWindow() != window() ) return false;
    QWidget* f = QApplication::focusWidget();
    if( !f ) return true;
    if( f == &m_circView || m_circView.isAncestorOf( f ) ) return false;
    if( QPlainTextEdit* t = qobject_cast<QPlainTextEdit*>( f ) ) return t->isReadOnly();   // Messages: not typing
    if( QTextEdit* t = qobject_cast<QTextEdit*>( f ) ) return t->isReadOnly();
    if( qobject_cast<QLineEdit*>( f ) || qobject_cast<QAbstractSpinBox*>( f ) ) return false;
    if( QComboBox* c = qobject_cast<QComboBox*>( f ) ) if( c->isEditable() ) return false;
    if( EditorWindow::self() && EditorWindow::self()->isAncestorOf( f ) ) return false;
    // A list or tree (the parts list, the file browser) keeps its arrows while stopped; running
    // or paused, a part picked in it doesn't take the simulation keys (user, 2026-11-21)
    if( qobject_cast<QAbstractItemView*>( f ) ) return Simulator::self() && Simulator::self()->isRunning();
    return true;
}

bool CircuitWidget::simKey( QKeyEvent* ev )
{
    int key = ev->key();
    if( key != Qt::Key_Space && key != Qt::Key_Escape && key != Qt::Key_Up && key != Qt::Key_Down
     && key != Qt::Key_Left && key != Qt::Key_Right ) return false;
    Qt::KeyboardModifiers mods = ev->modifiers() & ~Qt::KeypadModifier;   // macOS sets it on arrows
    if( mods & ~( Qt::ShiftModifier | Qt::AltModifier ) ) return false;   // ⌘ / ⌃: not ours
    if( !keysToSim() ) return false;

    Simulator* sim = Simulator::self();
    if( !sim || ( EditorWindow::self() && EditorWindow::self()->debugStarted() ) ) return false;
    bool running = sim->isRunning();
    bool shift = mods & Qt::ShiftModifier, alt = mods & Qt::AltModifier;
    int factor = ( shift && alt ) ? 100 : alt ? 10 : shift ? 5 : 1;

    switch( key )
    {
        case Qt::Key_Space:
            if( mods ) return false;
            if( ev->isAutoRepeat() ) return true;
            if( SimSkip::self()->active() ) SimSkip::self()->cancel();
            else if( !running ) powerCircOn();
            else pauseCirc();                       // Pauses or resumes
            return true;
        case Qt::Key_Escape:
            if( mods ) return false;
            if( ev->isAutoRepeat() ) return true;
            if( SimSkip::self()->active() ){ SimSkip::self()->cancel(); return true; }
            if( !running ) return false;
            powerCircOff();
            return true;
        case Qt::Key_Up:
        case Qt::Key_Down:
        {
            uint64_t v = SimSkip::nextInterval( sim->skipInterval(), key == Qt::Key_Up ? 1 : -1, factor == 1 ? 0 : factor );
            setSkipInterval( v );                   // Shown in the skip box
            return true;
        }
        case Qt::Key_Right:
            if( !SimSkip::self()->active() ) skipForward( factor );   // Repeats during a skip are dropped
            return true;
        case Qt::Key_Left:
            if( !SimSkip::self()->active() ) skipBack( factor );      // Held down: steps back as fast as it lands
            return true;
    }
    return false;
}

void CircuitWidget::skipForward( int count )
{
    Simulator* sim = Simulator::self();
    if( !sim ) return;
    QString err = SimSkip::self()->start( sim->skipInterval(), count );
    if( !err.isEmpty() && err.startsWith("busy") ) return;
    if( !err.isEmpty() ) qInfo().noquote() << "Skip: "+err;
}

void CircuitWidget::skipBack( int count )
{
    Simulator* sim = Simulator::self();
    if( !sim ) return;
    QString block = SimHistory::blockers( sim->elementList() );
    if( !block.isEmpty() ){ backOffWarning( block ); return; }   // Again at each ←, as at load
    QString err = SimSkip::self()->startBack( sim->skipInterval(), count );
    if( !err.isEmpty() && err.startsWith("busy") ) return;
    if( !err.isEmpty() ) qInfo().noquote() << "Back: "+err;
}

void CircuitWidget::backOffWarning( QString block )
{
    qWarning().noquote() << "[history] Going back in time is off for this circuit: "+block
                          +". The circuit runs as before; ← and ⏮ work in circuits without these parts.";
}

// Going back needs every part's state in the keyframes (specs/sim-control.md §4.8): ⏮ is greyed
// out, with the reason in its tooltip, when history is off or a part can't be saved
void CircuitWidget::updtBackAvail( bool announce )
{
    Simulator* sim = Simulator::self();
    if( !sim || !skipBackAct ) return;
    QString tip = tr("Pause and go back by the skip interval (keyframes of the run)");
    QString block;
    bool on = SimHistory::self()->enabled();
    if( !on ) tip += "\n\n"+tr("History is off (Settings → Simulation).");
    else{
        block = SimHistory::blockers( sim->elementList() );
        if( !block.isEmpty() ) tip += "\n\n"+tr("Off for this circuit: %1").arg( block );
    }
    skipBackAct->setEnabled( on && block.isEmpty() );
    skipBackAct->setToolTip( tip+"  (←)" );
    if( announce && on && !block.isEmpty() ) backOffWarning( block );
}

// The skip interval follows the speed (specs/sim-control.md §3.3): a tenth of the first speed in
// the speed list at or below it (25 ms/s -> 10 ms/s -> 1 ms; 1 s/s -> 100 ms; 1 µs/s -> 100 ns).
// Below the list: its decade
static double speedPreset( double psPerSec )
{
    static const double list[] = { 1e6, 1e7, 1e8, 1e9, 1e10, 1e11, 5e11, 1e12, 2e12, 5e12, 1e13 };
    double p = 0;
    for( double v : list ) if( v <= psPerSec*1.000001 ) p = v;
    if( p == 0 ) p = std::pow( 10, std::floor( std::log10( std::max( psPerSec, 10.0 ) ) ) );
    return p;
}

void CircuitWidget::skipForSpeed( double psPerSec, bool hysteresis )
{
    Simulator* sim = Simulator::self();
    if( !sim || sim->skipInterval() == 0 ) return;     // 1 step is kept
    if( hysteresis && m_skipPreset == 0 ) m_skipPreset = speedPreset( sim->psPerSec() );   // Run start: from the set speed
    double p = speedPreset( psPerSec );
    if( hysteresis && m_skipPreset > 0 && p > m_skipPreset && psPerSec < p*1.1 ) p = m_skipPreset;
    if( p == m_skipPreset && hysteresis ) return;      // Unchanged: a value set by hand stays
    m_skipPreset = p;
    uint64_t v = SimSkip::nextInterval( (uint64_t)p, -1, 10 );
    if( v != sim->skipInterval() ) sim->setSkipInterval( v );   // Not an edit: the file stays unmodified
}

void CircuitWidget::skipFollowSpeed()
{
    if( Simulator::self() ) skipForSpeed( Simulator::self()->psPerSec(), false );
}

void CircuitWidget::setSkipInterval( uint64_t ps )
{
    Simulator* sim = Simulator::self();
    if( !sim ) return;
    if( ps > SimSkip::maxInterval ) ps = SimSkip::maxInterval;
    if( ps == sim->skipInterval() ){ syncSkip( ps ); return; }
    sim->setSkipInterval( ps );                 // Syncs the box
    Circuit::self()->setChanged();              // Saved with the circuit (D6)
}

void CircuitWidget::syncSkip( uint64_t ps )
{
    if( !m_skipBox ) return;
    QSignalBlocker b( m_skipBox );
    int typed = -1, preset = -1;
    for( int i=0; i<m_skipBox->count(); ++i )
    {
        qulonglong v = m_skipBox->itemData( i ).toULongLong();
        if( v == (qulonglong)-1 ) typed = i;
        else if( v == ps ) preset = i;
    }
    if( preset >= 0 )
    {
        if( typed >= 0 ){ m_skipBox->removeItem( typed ); if( preset > typed ) preset--; }
        m_skipBox->setCurrentIndex( preset );
    }else{                                      // Off the ladder (typed): an entry of its own, at the end
        QString txt = SimSkip::intervalText( ps );
        if( typed < 0 ){ m_skipBox->addItem( txt, QVariant::fromValue( (qulonglong)-1 ) ); typed = m_skipBox->count()-1; }
        else m_skipBox->setItemText( typed, txt );
        m_skipBox->setCurrentIndex( typed );
    }
    m_skipBox->refresh();
    QString tip = tr("Skip Forward")+" "+SimSkip::intervalText( ps );
    if( skipFwdAct ) skipFwdAct->setText( tip );
}

// The numbers of the old info pane, one line in Messages when the simulation pauses or stops
void CircuitWidget::logRunInfo( QString what )
{
    Simulator* sim = Simulator::self();
    if( !sim ) return;
    QString line = what+" at "+InfoWidget::timeText( sim->circTime() )
                  +" · "+tr("set")+" "+InfoWidget::speedText( sim->psPerSec() );
    QString rate = m_infoWidget->rateSummary();
    if( !rate.isEmpty() ) line += " · "+rate;
    qInfo().noquote() << line;
}

// The skip box is framed while the simulation has the keys (specs/sim-control.md §3.1)
void CircuitWidget::updtKeyOwner()
{
    if( !m_skipBox ) return;
    bool sim = Simulator::self() && Simulator::self()->isRunning() && keysToSim();
    if( sim == m_simHasKeys ) return;
    m_simHasKeys = sim;
    m_skipBox->setStyleSheet( sim ? "QComboBox { border: 2px solid #3d7fc4; border-radius: 4px; }" : "" );
    m_skipBox->setToolTip( m_skipBox->toolTip().section("\n\n", 0, 0 )
        +( sim ? "\n\n"+tr("Blue frame: Space, Esc and the arrows control the simulation now.") : "" ) );
}

void CircuitWidget::simKeysBack()
{
    m_circView.clearFocus();                    // The skip box frame shows it
}

void CircuitWidget::syncTimebase( uint64_t psPerSec )
{
    if( !m_timebase ) return;
    m_timebase->blockSignals( true );
    int custom = -1, preset = -1;
    for( int i=0; i<m_timebase->count(); ++i )
    {
        qulonglong ps = m_timebase->itemData( i ).toULongLong();
        if( ps == 0 ) custom = i;
        else if( ps == psPerSec ) preset = i;
    }
    if( preset >= 0 )
    {
        if( custom >= 0 ) m_timebase->removeItem( custom );   // Back on a preset: drop the custom entry
        m_timebase->setCurrentIndex( preset < custom || custom < 0 ? preset : preset-1 );
        m_timebase->blockSignals( false );
        return;
    }
    // Not a preset (set in Settings or by the CLI): show it as a custom entry
    QString txt = InfoWidget::speedText( psPerSec );
    if( custom < 0 ) { m_timebase->addItem( txt, QVariant::fromValue( (qulonglong)0 ) ); custom = m_timebase->count()-1; }
    else m_timebase->setItemText( custom, txt );
    m_timebase->setCurrentIndex( custom );
    m_timebase->blockSignals( false );
}

void CircuitWidget::showRealSpeed( double realPsPerSec )
{
    if( !m_timebase ) return;
    QString text = ( realPsPerSec > 0 ) ? InfoWidget::speedText( realPsPerSec ) : "";
    m_timebase->setOverride( text );
    Simulator* sim = Simulator::self();     // Running (not paused, not skipping): follow the real speed
    if( sim && sim->isRunning() && !sim->isPaused() && !SimSkip::self()->active() )
        skipForSpeed( realPsPerSec > 0 ? realPsPerSec : sim->psPerSec(), true );
    QString tip = tr("Simulation speed: simulated time per real second.\n"
                     "1 s/s is real time. Faster than the computer can manage\n"
                     "runs as fast as it can (see Real Speed).");
    if( !text.isEmpty() ) tip = tr("Running slower than set: %1 real, %2 set.").arg( text, m_timebase->shownText() )+"\n\n"+tip;
    m_timebase->setToolTip( tip );
}

#include "moc_circuitwidget.cpp"

bool CircuitWidget::m_verboseLog = false;

void CircuitWidget::setVerboseLog( bool v )
{
    if( v == m_verboseLog ) return;
    m_verboseLog = v;
    MainWindow::self()->settings()->setValue("Circuit/logVerbose", v );
    MainWindow::self()->settings()->sync();   // Shared by every instance; a headless one may be killed
    if( m_verboseBox && m_verboseBox->isChecked() != v ) m_verboseBox->setChecked( v );
    fprintf( stderr, "[log] Messages verbose %s\n", v ? "on: debug logs shown" : "off: info, warnings and errors only" );
}

void CircuitWidget::syncEventLog()
{
    if( !m_logPartsBox ) return;
    if( m_logPartsBox->isChecked() != EventLog::self()->logParts() ) m_logPartsBox->setChecked( EventLog::self()->logParts() );
    if( m_logCapsBox->isChecked()  != EventLog::self()->logCaps()  ) m_logCapsBox->setChecked( EventLog::self()->logCaps() );
    m_logPartsBox->setEnabled( StatusView::self()->enabled() );      // Only with the toolbar display on
    m_logCapsBox->setEnabled( ChargeView::self()->enabled() );
}

void CircuitWidget::eventMessage( QString text, int level, QString uid, int64_t time )
{
    m_outPane.appendPlain( text, StatusView::levelColor( level, true ), uid, time, StatusView::levelColor( level, false ) );
}
