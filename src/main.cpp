/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QApplication>
#include <QTranslator>
#include <QStandardPaths>
#include <QtGui>
#include <QFileInfo>
#include <QFileOpenEvent>
#include <QTimer>

#include "mainwindow.h"
#include "circuitwidget.h"
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

int main( int argc, char *argv[] )
{
    // Client mode: "simulide cli <command>" talks to a running SimulIDE, no GUI here
    if( argc > 1 && QString( argv[1] ) == "cli" ) return runCliClient( argc, argv );

    bool headless = false;
    bool cliServer = true;
    QString socketName;
    QString circFile;
    for( int i=1; i<argc; ++i )
    {
        QString arg = QString::fromLocal8Bit( argv[i] );
        if     ( arg == "--headless"  ) headless  = true;
        else if( arg == "--no-server" ) cliServer = false;
        else if( arg == "--socket" && i+1 < argc ) socketName = QString::fromLocal8Bit( argv[++i] );
        else if( arg.endsWith(".simu") || arg.endsWith(".sim1") ) circFile = arg;
    }
    if( headless ){
        qputenv("QT_QPA_PLATFORM", "offscreen");  // No windows, no display needed
        cliServer = true;
    }
    qInstallMessageHandler( myMessageOutput );

    QApplication app( argc, argv );

    QSettings settings( QStandardPaths::standardLocations( QStandardPaths::DataLocation).first()+"/simulide.ini",  QSettings::IniFormat, 0l );

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

    if( !headless ) window.show();

    return app.exec();
}

