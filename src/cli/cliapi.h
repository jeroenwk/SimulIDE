/***************************************************************************
 *   SimulIDE command line control interface                              *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef CLIAPI_H
#define CLIAPI_H

#include <functional>
#include <memory>

#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QMap>

// Thrown by command handlers to report an error to the client
class CliError
{
    public:
        CliError( QString m ) : msg( m ) {}
        QString msg;
};

// Reply that will be sent later (long running commands)
class CliPending
{
    public:
        void ok( QJsonValue result=QJsonValue() );
        void fail( QString error );
        bool done() { return !m_send; }

        std::function<void(bool, QJsonValue, QString)> m_send;
};
typedef std::shared_ptr<CliPending> CliPendingPtr;

// Arguments of one command call
class CliCtx
{
    public:
        QStringList pos;   // Positional arguments
        QJsonObject opts;  // --key value  /  --flag
        QJsonObject sets;  // key=value pairs

        QString arg( int i, QString name );                 // Required positional
        QString argOr( int i, QString def ) { return i < pos.size() ? pos.at(i) : def; }
        QString opt( QString key, QString def="" );
        bool    flag( QString key );
        bool    has( QString key ) { return opts.contains( key ); }

        CliPendingPtr defer();
        CliPendingPtr pending() { return m_pending; }

    private:
        CliPendingPtr m_pending;
};

typedef std::function<QJsonValue(CliCtx&)> CliHandler;

struct CliCommand
{
    QString name;
    QString usage;
    QString help;
    CliHandler fn;
};

class CliApi
{
    public:
        CliApi();
        ~CliApi();

 static CliApi* self() { return m_pSelf; }

        void add( QString name, QString usage, QString help, CliHandler fn );

        // Execute a request; done() is called now or later with the reply
        void exec( const QJsonObject& req, std::function<void(QJsonObject)> done );

        void addMessage( QString msg );
        bool capturing() { return m_capturing; }

        bool headless() { return m_headless; }
        void setHeadless( bool h ) { m_headless = h; }

        QList<CliCommand> commands() { return m_cmds.values(); }

        // Resolve a path relative to the client's working directory
        QString resolvePath( QString path );

    private:
 static CliApi* m_pSelf;

        bool parseArgv( QJsonArray argv, QString& cmd, CliCtx& ctx, QString& error );

        void registerCore();
        void registerCircuit();   // cli_circuit.cpp
        void registerEdit();      // cli_edit.cpp
        void registerMeasure();   // cli_measure.cpp
        void registerSim();       // cli_sim.cpp
        void registerAverage();   // cli_average.cpp
        void registerI2c();       // cli_i2c.cpp
        void registerScript();    // cli_script.cpp
        void registerMcu();       // cli_mcu.cpp
        void registerEditor();    // cli_editor.cpp
        void registerSettings();  // cli_settings.cpp
        void registerView();      // cli_view.cpp

        QMap<QString, CliCommand> m_cmds;

        QStringList m_messages;
        QString m_cwd;
        bool m_capturing;
        bool m_headless;
};

// Helpers shared by command files
namespace Cli
{
    uint64_t parseTime( QString t );         // "10ms" -> picoseconds
    QString  timeStr( uint64_t ps );
    double   parseNum( QString v, QString what );

    // Set target speed (picoseconds simulated per real second), keeping steps
    // per second and step size consistent as the settings dialog does
    void setSimSpeed( uint64_t psPerSec, uint64_t stepSize=0 );

    // Call fn every ms milliseconds (GUI thread) until it returns true
    void pollUntil( std::function<bool()> fn, int ms=5 );

    // Pending "sim run --for" calls: cancelRuns() makes them reply with an error and
    // stop waiting (used when a script is stopped)
    int  runGeneration();
    void cancelRuns();
}
#endif
