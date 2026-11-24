/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QApplication>
#include <QTranslator>
#include <QStandardPaths>
#include <QtGui>
#include <QFileInfo>
#include <QDateTime>
#include <QFileOpenEvent>
#include <QTimer>

#include "mainwindow.h"
#include "circuitwidget.h"
#include "circuitview.h"
#include "cliserver.h"
#include "cliclient.h"

// Log levels in Messages (everything always goes to stderr):
// - debug:   qDebug() starting with a "[tag]" prefix (developer logs): only with Log: Verbose on;
// - info:    any other qDebug() (upstream's messages for the user) and qInfo();
// - warning: qWarning() with a "[tag]" (SimulIDE's own), shown as "Warning: ...";
// - error:   qCritical() with a "[tag]", shown as "Error: ...".
// Untagged warnings and errors come from Qt itself (e.g. the offscreen plugin's
// "propagateSizeHints"): stderr only, as before.
static bool hasTag( const QString& msg )
{
    static const QRegularExpression tag("^\\[[^\\]\\n]{1,48}\\]");
    return tag.match( msg ).hasMatch();
}

void myMessageOutput( QtMsgType type, const QMessageLogContext &context, const QString &msg )
{
    QByteArray localMsg = msg.toLocal8Bit();
    const char* file     = context.file ? context.file : "";
    const char* function = context.function ? context.function : "";
    bool ours = hasTag( msg ) && !( context.category && QByteArray( context.category ).startsWith("qt.") );
    CircuitWidget* cw = CircuitWidget::self();
    switch (type) {
    case QtDebugMsg:
        if( cw && ( CircuitWidget::verboseLog() || !hasTag( msg ) ) ) cw->simDebugMessage( msg );
        CliServer::logMessage( msg );
        fprintf( stderr, "%s \n", localMsg.constData() );
        break;
    case QtInfoMsg:
        if( cw ) cw->simDebugMessage( msg );
        fprintf(stderr, "Info: %s (%s:%u, %s)\n", localMsg.constData(), file, context.line, function);
        break;
    case QtWarningMsg:
        if( cw && ours ) cw->simDebugMessage("Warning: "+msg );
        fprintf(stderr, "Warning: %s (%s:%u, %s)\n", localMsg.constData(), file, context.line, function);
        break;
    case QtCriticalMsg:
        if( cw && ours ) cw->simDebugMessage("Error: "+msg );
        CliServer::logMessage("Error: "+msg );
        fprintf(stderr, "Critical: %s (%s:%u, %s)\n", localMsg.constData(), file, context.line, function);
        break;
    case QtFatalMsg:
        fprintf(stderr, "Fatal: %s (%s:%u, %s)\n", localMsg.constData(), file, context.line, function);
        break;
    }
}

// macOS: Finder opens a document (double-click, Open With, drop on the Dock icon) with an
// "open document" Apple event, which Qt delivers as QEvent::FileOpen, not in argv. At
// launch it arrives once the event loop runs; later it arrives while the app is open.
class FileOpenFilter : public QObject
{
    public:
        bool opened = false;      // A document was opened this way: don't reopen the last one

    protected:
        bool eventFilter( QObject* obj, QEvent* ev ) override
        {
            if( ev->type() != QEvent::FileOpen ) return QObject::eventFilter( obj, ev );
            QString file = static_cast<QFileOpenEvent*>( ev )->file();
            if( file.endsWith(".sim1") || file.endsWith(".simu") )
            {
                opened = true;
                CircuitWidget::self()->loadCirc( QFileInfo( file ).absoluteFilePath() ); // Asks to save changes
                if( QWidget* w = MainWindow::self() ){ w->raise(); w->activateWindow(); }
            }
            return true;
        }
};

QString langFile( QString locale )
{
    QString langF = ":/simulide_"+locale+".qm";

    QFile file( langF );
    if( !file.exists() ) langF = "";

    return langF;
}

#ifdef Q_OS_MAC
void uiMacTitleNote( void* nsView, const char* text );   // uitheme_mac.mm
#endif

int main( int argc, char *argv[] )
{
    // Client mode: "simulide cli <command>" talks to a running SimulIDE, no GUI here
    if( argc > 1 && QString( argv[1] ) == "cli" ) return runCliClient( argc, argv );

    bool headless = false;
    bool cliServer = true;
    QString socketName;
    QString circFile;
    QString idleExit;                 // Headless: quit after this long without a CLI request
    bool keep = false;
    for( int i=1; i<argc; ++i )
    {
        QString arg = QString::fromLocal8Bit( argv[i] );
        if     ( arg == "--headless"  ) headless  = true;
        else if( arg == "--no-server" ) cliServer = false;
        else if( arg == "--socket" && i+1 < argc ) socketName = QString::fromLocal8Bit( argv[++i] );
        else if( arg == "--idle-exit" && i+1 < argc ) idleExit = QString::fromLocal8Bit( argv[++i] );
        else if( arg == "--keep" ) keep = true;
        else if( arg.endsWith(".simu") || arg.endsWith(".sim1") ) circFile = arg;
    }
    if( headless ){
        qputenv("QT_QPA_PLATFORM", "offscreen");  // No windows, no display needed
        cliServer = true;
    }
    qInstallMessageHandler( myMessageOutput );

    QApplication app( argc, argv );

    QSettings settings( MainWindow::configDir()+"/simulide.ini",  QSettings::IniFormat, 0l );

    QString locale = QLocale::system().name();
    if( settings.contains( "language" ) ) locale = settings.value( "language" ).toString();

    QString langF = langFile( locale );
    if( langF == "" )
    {
        locale = QLocale::system().name().split("_").first();
        langF = langFile( locale );
    }
    if( langF == "" ) langF = ":/simulide_en.qm";

    QTranslator translator;
    translator.load( langF );
    app.installTranslator( &translator );
    app.setApplicationVersion( APP_VERSION );

    MainWindow window( headless );
    window.setLoc( locale );

    CliServer* server = nullptr;
    if( cliServer ){
        server = new CliServer( &app );
        if( !server->start( socketName, headless ) && headless )
        {
            fprintf( stderr, "SimulIDE headless: could not start CLI server on socket '%s'\n",
                     ( socketName.isEmpty() ? CliServer::defaultName() : socketName ).toLocal8Bit().constData() );
            return 2;
        }
        if( headless && server && !keep )
        {
            // Default 30 min: servers started for a session or a script outlived them by days
            // (44 of them, 1.9 GB). "--idle-exit 90s|10m|2h|0", "--keep" = never.
            int ms = 30*60*1000;
            if( !idleExit.isEmpty() )
            {
                QString v = idleExit.trimmed().toLower();
                double mult = 60*1000;                         // Plain number: minutes
                if     ( v.endsWith("ms") ){ mult = 1;         v.chop( 2 ); }
                else if( v.endsWith("s")  ){ mult = 1000;      v.chop( 1 ); }
                else if( v.endsWith("m")  ){ mult = 60*1000;   v.chop( 1 ); }
                else if( v.endsWith("h")  ){ mult = 3600*1000; v.chop( 1 ); }
                bool ok = false;
                double n = v.toDouble( &ok );
                if( !ok || n < 0 ){
                    fprintf( stderr, "SimulIDE headless: --idle-exit takes a time (90s, 10m, 2h; 0 = never), got '%s'\n",
                             idleExit.toLocal8Bit().constData() );
                    return 2;
                }
                ms = int( n*mult );
            }
            server->setIdleExit( ms );
        }
    }
    FileOpenFilter* fileOpen = new FileOpenFilter;
    app.installEventFilter( fileOpen );

    if( !circFile.isEmpty() ) CircuitWidget::self()->loadCirc( QFileInfo( circFile ).absoluteFilePath() );
    else if( !headless )
    {
        // Give a launch "open document" event the first chance, then reopen the last circuit
        QTimer::singleShot( 300, [fileOpen, &window](){
            if( !fileOpen->opened ) window.reopenLastCircuit();
        });
    }

    if( !headless )
    {
        window.show();
        {   // The build time (the executable's), right in the title bar: which build is this
            QDateTime b = QFileInfo( QCoreApplication::applicationFilePath() ).lastModified();
            QString note = "built "+( b.date() == QDate::currentDate() ? b.toString("HH:mm")
                                                                    : b.toString("d MMM yyyy HH:mm") );
#ifdef Q_OS_MAC
            // winId() is an NSView only on the Cocoa platform: under offscreen (QT_QPA_PLATFORM)
            // it is not, and sending it a message crashed the app at start
            if( QGuiApplication::platformName() == "cocoa" )
                uiMacTitleNote( (void*)window.winId(), note.toUtf8().constData() );
#else
            Q_UNUSED( note )
#endif
        }
        QTimer::singleShot( 0, [](){               // The canvas starts with the keyboard: the
            if( CircuitView* v = CircuitView::self() ) v->setFocus( Qt::OtherFocusReason ); // cursor
        });                                        // cross shows and keys work at once
    }

    return app.exec();
}

