/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QApplication>
#include <QTranslator>
#include <QStandardPaths>
#include <QtGui>
#include <QFileInfo>

#include "mainwindow.h"
#include "circuitwidget.h"
#include "cliserver.h"
#include "cliclient.h"

void myMessageOutput( QtMsgType type, const QMessageLogContext &context, const QString &msg )
{
    QByteArray localMsg = msg.toLocal8Bit();
    const char* file     = context.file ? context.file : "";
    const char* function = context.function ? context.function : "";
    switch (type) {
    case QtDebugMsg:
        if( CircuitWidget::self() ) CircuitWidget::self()->simDebugMessage( msg );
        CliServer::logMessage( msg );
        fprintf( stderr, "%s \n", localMsg.constData() );
        break;
    case QtInfoMsg:
        fprintf(stderr, "Info: %s (%s:%u, %s)\n", localMsg.constData(), file, context.line, function);
        break;
    case QtWarningMsg:
        fprintf(stderr, "Warning: %s (%s:%u, %s)\n", localMsg.constData(), file, context.line, function);
        break;
    case QtCriticalMsg:
        fprintf(stderr, "Critical: %s (%s:%u, %s)\n", localMsg.constData(), file, context.line, function);
        break;
    case QtFatalMsg:
        fprintf(stderr, "Fatal: %s (%s:%u, %s)\n", localMsg.constData(), file, context.line, function);
        break;
    }
}

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
    if( !circFile.isEmpty() ) CircuitWidget::self()->loadCirc( QFileInfo( circFile ).absoluteFilePath() );
    else                      window.reopenLastCircuit();

    if( !headless ) window.show();

    return app.exec();
}

