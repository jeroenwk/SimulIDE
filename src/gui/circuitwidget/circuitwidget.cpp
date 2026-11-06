/***************************************************************************
 *   Copyright (C) 2010 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

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
#include "circuitwidget.h"
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
#include "cliapi.h"

// The speed box: while the simulation can't keep up, it shows the real speed in orange
class SpeedBox : public QComboBox
{
    public:
        SpeedBox( QWidget* parent ) : QComboBox( parent ) {}

        void setOverride( QString text )
        {
            if( text == m_override ) return;
            m_override = text;
            update();
        }

    protected:
        void paintEvent( QPaintEvent* ) override
        {
            QStylePainter p( this );
            QStyleOptionComboBox opt;
            initStyleOption( &opt );
            if( !m_override.isEmpty() )
            {
                opt.currentText = m_override;
                QColor orange = InfoWidget::slowColor();
                opt.palette.setColor( QPalette::ButtonText, orange );
                opt.palette.setColor( QPalette::Text, orange );
            }
            p.drawComplexControl( QStyle::CC_ComboBox, opt );
            p.drawControl( QStyle::CE_ComboBoxLabel, opt );
        }

    private:
        QString m_override;
};

CircuitWidget* CircuitWidget::m_pSelf = 0l;

CircuitWidget::CircuitWidget( QWidget *parent  )
             : QWidget( parent )
             , m_verticalLayout( this )
             , m_circView( this )
             , m_outPane( this )
             , m_circToolBar( this )
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

    m_verticalLayout.addWidget( &m_circToolBar );

    m_mainSplitter = new QSplitter( this );
    m_mainSplitter->setObjectName("CircuitSplitter");
    m_mainSplitter->setOrientation( Qt::Vertical );
    m_verticalLayout.addWidget( m_mainSplitter );

    m_infoWidget = new InfoWidget( this );
    m_infoWidget->setTargetSpeed( 100 );
    m_infoWidget->setFixedWidth( 322*MainWindow::self()->fontScale() );
    m_infoWidget->setVisible( MainWindow::self()->settings()->value("showInfoPanel", false ).toBool() );

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
    logRow->addStretch();
    msgLayout->addLayout( logRow );
    msgLayout->addWidget( &m_outPane );
    m_bottomTabs->addTab( m_msgWidget, tr("Messages") );
    syncEventLog();
    connect( m_logPartsBox, &QCheckBox::toggled, []( bool on ){ EventLog::self()->setLogParts( on ); } );
    connect( m_logCapsBox,  &QCheckBox::toggled, []( bool on ){ EventLog::self()->setLogCaps( on ); } );
    m_scriptPane = new ScriptPane( this );
    m_bottomTabs->addTab( m_scriptPane, tr("Scripts") );
    connect( m_scriptPane, &ScriptPane::started, [this](){ m_bottomTabs->setCurrentWidget( m_scriptPane ); } );
    m_bottomTabs->setCurrentWidget( m_msgWidget );
    m_panelSplitter->addWidget( m_bottomTabs );
    m_panelSplitter->setSizes( {170, 500} );

    m_mainSplitter->addWidget( &m_circView );
    m_mainSplitter->addWidget( m_panelSplitter );
    m_mainSplitter->setSizes( {500, 100} );

    QFont font( MainWindow::self()->defaultFontName(), 10, QFont::Bold );
    double scale = MainWindow::self()->fontScale();
    font.setPixelSize( 14*scale );
    m_msgLabel = new QLabel( this );
    m_msgLabel->setFont( font );
    m_msgLabel->setMaximumSize( 200*scale, 20*scale );

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

    newCircAct = new QAction( QIcon(":/new.svg"), tr("New C&ircuit\tCtrl+N"), this);
    newCircAct->setStatusTip( tr("Create a new Circuit"));
    connect( newCircAct, &QAction::triggered,
                   this, &CircuitWidget::newCircuit, Qt::UniqueConnection );

    openCircAct = new QAction( QIcon(":/open.svg"), tr("&Open Circuit\tCtrl+O"), this);
    openCircAct->setStatusTip( tr("Open an existing Circuit"));
    connect( openCircAct, &QAction::triggered,
                    this, &CircuitWidget::openCirc, Qt::UniqueConnection );

    saveCircAct = new QAction( QIcon(":/save.svg"), tr("&Save Circuit\tCtrl+S"), this);
    saveCircAct->setStatusTip( tr("Save the Circuit to disk"));
    connect( saveCircAct, &QAction::triggered,
                    this, QOverload<>::of(&CircuitWidget::saveCirc), Qt::UniqueConnection );

    saveCircAsAct = new QAction( QIcon(":/saveas.svg"),tr("Save Circuit &As...\tCtrl+Shift+S"), this);
    saveCircAsAct->setStatusTip( tr("Save the Circuit under a new name"));
    connect( saveCircAsAct, &QAction::triggered,
                      this, &CircuitWidget::saveCircAs, Qt::UniqueConnection );

    QString undoKey = QKeySequence( QKeySequence::Undo ).toString( QKeySequence::NativeText );
    QString redoKey = QKeySequence( QKeySequence::Redo ).toString( QKeySequence::NativeText );
    undoAct = new QAction( QIcon(":/undo.svg"), tr("Undo")+" ("+undoKey+")", this );
    undoAct->setStatusTip( tr("Undo the last change to the circuit") );
    connect( undoAct, &QAction::triggered, this, [](){ Circuit::self()->undo(); } );

    redoAct = new QAction( QIcon(":/redo.svg"), tr("Redo")+" ("+redoKey+")", this );
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
}

void CircuitWidget::createToolBars()
{
    m_circToolBar.setObjectName( "m_circToolBar" );

    double fs = MainWindow::self()->fontScale()*20;
    m_circToolBar.setIconSize( QSize( fs, fs ) );

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
    });
    m_circToolBar.addWidget( m_timebase );
    syncTimebase( Simulator::self() ? Simulator::self()->psPerSec() : (uint64_t)1e12 );

    m_circToolBar.addAction( powerCircAct );
    m_circToolBar.addAction( pauseSimAct );
    m_circToolBar.addAction( currentFlowAct );
    m_circToolBar.addAction( capChargeAct );
    m_circToolBar.addAction( partStatusAct );
    m_circToolBar.addAction( instrumentsAct );
    m_circToolBar.addSeparator();//..........................

    spacer = new QWidget();
    spacer->setFixedWidth( 15 );
    m_circToolBar.addWidget( spacer );
    m_circToolBar.addWidget( m_msgLabel );

    spacer = new QWidget( this );
    spacer->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Preferred );
    spacer->setVisible( true );
    m_circToolBar.addWidget( spacer );

    m_infoMenu.addAction( infoAct );
    m_infoMenu.addAction( aboutAct );
    m_infoMenu.addAction( aboutQtAct );

    QLabel* timeLabel = new QLabel( this );            // Simulation time, right-aligned before Info
    QFont tf = timeLabel->font();
    tf.setPixelSize( 12*MainWindow::self()->fontScale() );
    timeLabel->setFont( tf );
    m_circToolBar.addWidget( timeLabel );
    m_infoWidget->setToolbarTime( timeLabel );

    QToolButton* infoPanelButton = new QToolButton( this );   // Shows / hides the speed and load pane
    infoPanelButton->setCheckable( true );
    infoPanelButton->setChecked( m_infoWidget->isVisibleTo( this ) );
    auto syncPanelButton = [this, infoPanelButton]( bool on )
    {
        infoPanelButton->setArrowType( on ? Qt::UpArrow : Qt::DownArrow );
        infoPanelButton->setToolTip( on ? tr("Hide the simulation info pane (speed, load, FPS)")
                                        : tr("Show the simulation info pane (speed, load, FPS)") );
    };
    syncPanelButton( infoPanelButton->isChecked() );
    connect( infoPanelButton, &QToolButton::toggled, [this, syncPanelButton]( bool on )
    {
        m_infoWidget->setVisible( on );
        MainWindow::self()->settings()->setValue("showInfoPanel", on );
        syncPanelButton( on );
    });
    m_circToolBar.addWidget( infoPanelButton );

    QToolButton* infoButton = new QToolButton( this );
    infoButton->setToolTip( tr("Info") );
    infoButton->setMenu( &m_infoMenu );
    infoButton->setIcon( QIcon(":/help.svg") );
    infoButton->setPopupMode( QToolButton::InstantPopup );
    m_circToolBar.addWidget( infoButton );
    
    m_circToolBar.addSeparator();//..........................
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
    Simulator::self()->addToUpdateList( &m_outPane );

    MainWindow::self()->setFile( tr("New Circuit"));
    showDescription();
    MainWindow::self()->settings()->setValue( "lastCircDir", m_lastCircDir );
    
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
    setMsg( " "+tr("Running")+" ", 0 );

    CurrentFlow::self()->attach();       // Each circuit has a new Simulator
    ChargeView::self()->attach();
    StatusView::self()->attach();
    EventLog::self()->attach();
    m_infoWidget->setSimState( InfoWidget::shownRunning );
    Simulator::self()->startSim();
}

void CircuitWidget::powerCircOff()
{
    if( Simulator::self()->isRunning() ) Simulator::self()->stopSim();

    powerCircAct->setIcon( QIcon(":/simplay.svg") );
    pauseSimAct->setIcon( QIcon(":/simpause.svg") );
    powerCircAct->setText(tr("Start Simulation"));
    powerCircAct->setIconText("Off");
    powerCircAct->setEnabled( true );
    pauseSimAct->setEnabled( false );
    pauseSimAct->setText(tr("Pause Simulation"));
    MainWindow::self()->setState("■");
    setMsg( " "+tr("Stopped")+" ", 1 );

    m_infoWidget->setRate( 0, 0 );
    m_infoWidget->setSimState( InfoWidget::shownStopped );
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
    setMsg( " "+tr("Debug")+"❚❚", 3 );

    m_infoWidget->setRate(-1 );  //m_rateLabel->setText( tr("Speed: Debugger") );

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

    setMsg( " "+tr("Debug")+"❚❚", 3 );
    MainWindow::self()->setState("❚❚");
}

void CircuitWidget::resumeDebug()
{
    setMsg( " "+tr("Debug")+"  ▶ ", 4 );
    MainWindow::self()->setState("▶");
    Simulator::self()->resumeSim();
}

void CircuitWidget::pauseCirc()
{
    if( Simulator::self()->simState() > SIM_PAUSED )
    {
        Simulator::self()->pauseSim();
        setMsg( " "+tr("Paused")+" ", 1 );
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
        setMsg( " "+tr("Running")+" ", 0 );
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

void CircuitWidget::setMsg( QString msg, int type )
{
    if     ( type == 0 ) m_msgLabel->setStyleSheet("QLabel { background-color: lightgreen; color: black;  font-weight: bold;}");
    else if( type == 1 ) m_msgLabel->setStyleSheet("QLabel { background-color: orange;     color: white;  font-weight: bold;}");
    else if( type == 2 ) m_msgLabel->setStyleSheet("QLabel { background-color: red;        color: yellow; font-weight: bold;}");
    else if( type == 3 ) m_msgLabel->setStyleSheet("QLabel { background-color: blue;       color: white;  font-weight: bold;}");
    else if( type == 4 ) m_msgLabel->setStyleSheet("QLabel { background-color: lightblue;  color: black;  font-weight: bold;}");
    m_msgLabel->setText( "   "+msg+"   " );
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
    QString tip = tr("Simulation speed: simulated time per real second.\n"
                     "1 s/s is real time. Faster than the computer can manage\n"
                     "runs as fast as it can (see Real Speed).");
    if( !text.isEmpty() ) tip = tr("Running slower than set: %1 real, %2 set.").arg( text, m_timebase->currentText() )+"\n\n"+tip;
    m_timebase->setToolTip( tip );
}

#include "moc_circuitwidget.cpp"

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
