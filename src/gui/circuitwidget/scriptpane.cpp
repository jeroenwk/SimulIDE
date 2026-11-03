/***************************************************************************
 *   Scripts pane: pick, describe, run and stop scenario scripts           *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QComboBox>
#include <QLineEdit>
#include <QToolButton>
#include <QPushButton>
#include <QLabel>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QStandardPaths>
#include <QProcessEnvironment>
#include <QCoreApplication>
#include <QTimer>

#include "scriptpane.h"
#include "outpaneltext.h"
#include "circuit.h"
#include "circuitwidget.h"
#include "simulator.h"
#include "mainwindow.h"
#include "cliserver.h"
#include "cliapi.h"

ScriptPane* ScriptPane::m_pSelf = nullptr;

ScriptPane::ScriptPane( QWidget* parent )
          : QWidget( parent )
{
    m_pSelf = this;
    setObjectName("ScriptPane");
    m_proc = nullptr;
    m_exitCode = 0;
    m_stopping = false;
    m_lastMs = 0;
    m_stateStr = "idle";

    m_list = new QComboBox( this );
    m_list->setSizeAdjustPolicy( QComboBox::AdjustToMinimumContentsLengthWithIcon );
    m_list->setMinimumContentsLength( 24 );
    m_list->setToolTip( tr("Scripts next to the open circuit (its folder and scripts/)") );
    connect( m_list, QOverload<int>::of( &QComboBox::currentIndexChanged ), [this]( int ){ showSelected(); } );

    m_openButton = new QToolButton( this );
    m_openButton->setText( tr("Open…") );
    m_openButton->setToolTip( tr("Add a script file from anywhere") );
    connect( m_openButton, &QToolButton::clicked, [this](){ browse(); } );

    m_refreshButton = new QToolButton( this );
    m_refreshButton->setText( tr("Rescan") );
    m_refreshButton->setToolTip( tr("Look again for scripts next to the open circuit") );
    connect( m_refreshButton, &QToolButton::clicked, [this](){ refresh(); } );

    m_args = new QLineEdit( this );
    m_args->setPlaceholderText( tr("arguments, e.g. --fast") );
    m_args->setToolTip( tr("Command line arguments for the script") );
    connect( m_args, &QLineEdit::returnPressed, [this](){ if( !running() ) m_runButton->click(); } );

    m_runButton = new QPushButton( tr("Run"), this );
    m_runButton->setToolTip( tr("Run the script with python3; it drives this window") );
    connect( m_runButton, &QPushButton::clicked, [this]()
    {
        QString err;
        if( !run( m_list->currentData().toString(), m_args->text(), err ) )
            m_out->appendPlain( "ERROR: "+err, QColor( 255, 120, 120 ) );
    });

    m_stopButton = new QPushButton( tr("Stop"), this );
    m_stopButton->setToolTip( tr("Stop the script and pause the simulation") );
    connect( m_stopButton, &QPushButton::clicked, [this](){ stop(); } );

    m_state = new QLabel( this );
    m_state->setMinimumWidth( 120 );

    QHBoxLayout* bar = new QHBoxLayout();
    bar->setContentsMargins( 4, 3, 4, 3 );
    bar->addWidget( m_list, 2 );
    bar->addWidget( m_openButton );
    bar->addWidget( m_refreshButton );
    bar->addWidget( m_args, 1 );
    bar->addWidget( m_runButton );
    bar->addWidget( m_stopButton );
    bar->addWidget( m_state );

    m_desc = new QPlainTextEdit( this );
    m_desc->setReadOnly( true );
    m_desc->setPlaceholderText( tr("The script's description (its docstring) shows here.") );

    m_out = new OutPanelText( this );
    m_out->setReadOnly( true );
    QFont f = m_out->font();
    m_desc->setFont( f );

    QSplitter* split = new QSplitter( Qt::Horizontal, this );
    split->addWidget( m_desc );
    split->addWidget( m_out );
    split->setSizes( { 300, 500 } );

    QVBoxLayout* layout = new QVBoxLayout( this );
    layout->setContentsMargins( 0, 0, 0, 0 );
    layout->setSpacing( 0 );
    layout->addLayout( bar );
    layout->addWidget( split );

    QTimer* tick = new QTimer( this );       // Elapsed time while running
    connect( tick, &QTimer::timeout, [this](){ if( running() ) updateState(); } );
    tick->start( 500 );

    updateState();
}
ScriptPane::~ScriptPane()
{
    if( m_proc && m_proc->state() != QProcess::NotRunning )
    {
        m_proc->kill();
        m_proc->waitForFinished( 1000 );
    }
    m_pSelf = nullptr;
}

//---------------------------------------------------------------------------
// Script files

bool ScriptPane::isScript( const QString &file )
{
    if( !file.endsWith(".py") ) return false;
    QFile f( file );
    if( !f.open( QIODevice::ReadOnly | QIODevice::Text ) ) return false;
    QString text = QString::fromUtf8( f.read( 64*1024 ) );
    return text.contains("simctl") || text.contains("# simulide-script");
}

QString ScriptPane::describe( const QString &file )
{
    QFile f( file );
    if( !f.open( QIODevice::ReadOnly | QIODevice::Text ) ) return "";
    QStringList lines = QString::fromUtf8( f.read( 256*1024 ) ).split('\n');

    QStringList comments;
    int i = 0;
    for( ; i<lines.size(); ++i )               // Skip shebang, coding line, blank lines; keep # comments
    {
        QString t = lines[i].trimmed();
        if( t.isEmpty() ) continue;
        if( t.startsWith("#") )
        {
            if( i == 0 && t.startsWith("#!") ) continue;
            if( t.contains("coding") || t == "# simulide-script" ) continue;
            comments.append( t.mid( 1 ).trimmed() );
            continue;
        }
        break;
    }
    if( i < lines.size() )                     // Module docstring
    {
        QString t = lines[i].trimmed();
        int q = 0;
        if( t.startsWith("r") || t.startsWith("u") ) { t = t.mid( 1 ); q = 1; }
        QString quote = t.startsWith("\"\"\"") ? "\"\"\"" : t.startsWith("'''") ? "'''" : "";
        if( !quote.isEmpty() )
        {
            QString first = lines[i].trimmed().mid( q+3 );
            int end = first.indexOf( quote );
            if( end >= 0 ) return first.left( end ).trimmed();
            QStringList doc;
            if( !first.trimmed().isEmpty() ) doc.append( first );
            for( ++i; i<lines.size(); ++i )
            {
                int e = lines[i].indexOf( quote );
                if( e >= 0 ) { doc.append( lines[i].left( e ) ); break; }
                doc.append( lines[i] );
            }
            while( !doc.isEmpty() && doc.last().trimmed().isEmpty() ) doc.removeLast();
            return doc.join('\n');
        }
    }
    return comments.join('\n');
}

void ScriptPane::addFile( QString file, bool select )
{
    file = QFileInfo( file ).absoluteFilePath();
    int idx = m_files.indexOf( file );
    if( idx < 0 )
    {
        m_files.append( file );
        QString dir = QFileInfo( Circuit::self() ? Circuit::self()->getFilePath() : "" ).absolutePath();
        QString name = QDir( dir ).relativeFilePath( file );
        if( name.startsWith("../") ) name = QFileInfo( file ).fileName();
        m_list->addItem( name, file );
        m_list->setItemData( m_list->count()-1, file, Qt::ToolTipRole );
        idx = m_files.size()-1;
    }
    if( select ) m_list->setCurrentIndex( idx );
}

void ScriptPane::refresh()
{
    if( running() ) return;
    QString keep = m_list->currentData().toString();

    m_list->blockSignals( true );
    m_list->clear();
    m_files.clear();

    QString circ = Circuit::self() ? Circuit::self()->getFilePath() : "";
    if( !circ.isEmpty() )
    {
        QDir dir = QFileInfo( circ ).absoluteDir();
        QStringList dirs = { dir.absolutePath(), dir.absoluteFilePath("scripts") };
        for( const QString &d : dirs )
        {
            QDir sd( d );
            if( !sd.exists() ) continue;
            for( const QString &name : sd.entryList( { "*.py" }, QDir::Files, QDir::Name ) )
            {
                QString file = sd.absoluteFilePath( name );
                if( isScript( file ) ) addFile( file, false );
            }
        }
    }
    if( !keep.isEmpty() && QFileInfo::exists( keep ) ) addFile( keep, false ); // A script opened by hand stays
    int idx = m_files.indexOf( keep );
    m_list->setCurrentIndex( idx >= 0 ? idx : ( m_files.isEmpty() ? -1 : 0 ) );
    m_list->blockSignals( false );
    showSelected();
}

void ScriptPane::showSelected()
{
    QString file = m_list->currentData().toString();
    if( file.isEmpty() ) m_desc->setPlainText("");
    else{
        QString d = describe( file );
        m_desc->setPlainText( d.isEmpty() ? tr("(no description: start the script with a docstring)") : d );
    }
    updateState();
}

void ScriptPane::browse()
{
    QString dir = m_lastDir;
    if( dir.isEmpty() && Circuit::self() ) dir = QFileInfo( Circuit::self()->getFilePath() ).absolutePath();
    QString file = QFileDialog::getOpenFileName( this, tr("Open Script"), dir, tr("Python scripts (*.py);;All files (*)") );
    if( file.isEmpty() ) return;
    m_lastDir = QFileInfo( file ).absolutePath();
    addFile( file, true );
}

//---------------------------------------------------------------------------
// Running

QString ScriptPane::python()
{
    QStringList extra = { "/opt/homebrew/bin", "/usr/local/bin", "/usr/bin" }; // Apps started from the Finder get a short PATH
    QString py = QStandardPaths::findExecutable("python3");
    if( py.isEmpty() ) py = QStandardPaths::findExecutable("python3", extra );
    if( py.isEmpty() ) py = QStandardPaths::findExecutable("python");
    return py;
}

bool ScriptPane::running() const { return m_proc && m_proc->state() != QProcess::NotRunning; }

bool ScriptPane::run( QString file, QString args, QString &error )
{
    if( running() ) { error = "a script is already running: "+m_runFile; return false; }
    if( file.isEmpty() ) { error = "no script selected"; return false; }
    file = QFileInfo( file ).absoluteFilePath();
    if( !QFileInfo::exists( file ) ) { error = "script not found: "+file; return false; }
    QString py = python();
    if( py.isEmpty() ) { error = "python3 not found"; return false; }
    if( !CliServer::self() ) { error = "the CLI server is not running: scripts cannot reach this window"; return false; }

    addFile( file, true );
    if( m_args->text() != args ) m_args->setText( args );

    if( m_proc ) m_proc->deleteLater();
    m_proc = new QProcess( this );
    m_proc->setProcessChannelMode( QProcess::MergedChannels );
    m_proc->setWorkingDirectory( QFileInfo( file ).absolutePath() );

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("SIMULIDE", QCoreApplication::applicationFilePath() );   // simctl: the CLI client binary
    env.insert("SIMULIDE_SOCKET", CliServer::self()->serverName() );    // simctl: this window
    env.insert("PYTHONUNBUFFERED", "1");
    m_proc->setProcessEnvironment( env );

    connect( m_proc, &QProcess::readyRead, this, &ScriptPane::readOutput );
    connect( m_proc, QOverload<int, QProcess::ExitStatus>::of( &QProcess::finished ), this, &ScriptPane::processFinished );
    connect( m_proc, &QProcess::errorOccurred, [this]( QProcess::ProcessError e )
    {
        if( e == QProcess::FailedToStart ){
            m_out->appendPlain("ERROR: could not start python: "+m_proc->errorString(), QColor( 255, 120, 120 ) );
            m_stateStr = "failed";
            updateState();
        }
    });

    m_runFile  = file;
    m_output.clear();
    m_exitCode = 0;
    m_stopping = false;
    m_stateStr = "running";
    m_clock.start();

    QStringList argv = QProcess::splitCommand( args );
    m_out->appendPlain( "\n▶ "+QFileInfo( file ).fileName()+( args.isEmpty() ? "" : " "+args ), QColor( 140, 200, 255 ) );
    m_proc->start( py, QStringList{ "-u", file } + argv );
    updateState();
    emit started();
    return true;
}

void ScriptPane::readOutput()
{
    if( !m_proc ) return;
    QString text = QString::fromUtf8( m_proc->readAll() );
    m_output += text;
    if( m_output.size() > 2000000 ) m_output = m_output.right( 1000000 );
    m_out->appendText( text );
    m_out->updateStep();
}

void ScriptPane::processFinished( int code, QProcess::ExitStatus st )
{
    readOutput();
    m_lastMs = m_clock.elapsed();
    m_exitCode = code;
    if     ( m_stopping )              m_stateStr = "stopped";
    else if( st == QProcess::CrashExit ) m_stateStr = "failed";
    else                               m_stateStr = ( code == 0 ) ? "finished" : "failed";

    QString msg = m_stopping ? tr("■ stopped") : QString("■ exit code %1").arg( code );
    m_out->appendPlain( msg, ( code == 0 && !m_stopping ) ? QColor( 120, 220, 140 ) : QColor( 255, 170, 90 ) );
    updateState();
    emit finished( code );
}

void ScriptPane::stop()
{
    if( !running() ) return;
    m_stopping = true;
    m_proc->terminate();
    QProcess* p = m_proc;
    QTimer::singleShot( 1500, p, [p](){ if( p->state() != QProcess::NotRunning ) p->kill(); } );

    Cli::cancelRuns();                                 // A "sim run --for" it was waiting on
    Simulator* sim = Simulator::self();
    if( sim && sim->isRunning() && !sim->isPaused() ) CircuitWidget::self()->pauseCirc();
    updateState();
}

void ScriptPane::updateState()
{
    bool run = running();
    m_runButton->setEnabled( !run && !m_list->currentData().toString().isEmpty() );
    m_stopButton->setEnabled( run );
    m_list->setEnabled( !run );
    m_openButton->setEnabled( !run );
    m_refreshButton->setEnabled( !run );

    qint64 ms = run ? m_clock.elapsed() : m_lastMs;
    QString t = QString::number( ms/1000.0, 'f', 1 )+" s";
    QString s;
    if     ( m_stateStr == "running"  ) s = ( m_stopping ? tr("stopping… ") : tr("running ") )+t;
    else if( m_stateStr == "finished" ) s = tr("passed, ")+t;
    else if( m_stateStr == "failed"   ) s = tr("exit %1, ").arg( m_exitCode )+t;
    else if( m_stateStr == "stopped"  ) s = tr("stopped, ")+t;
    m_state->setText( s );
}

QJsonObject ScriptPane::status() const
{
    qint64 ms = running() ? m_clock.elapsed() : m_lastMs;
    return QJsonObject{
        { "state",    m_stateStr },
        { "file",     m_runFile },
        { "exitCode", m_exitCode },
        { "seconds",  ms/1000.0 },
        { "output",   m_output } };
}
