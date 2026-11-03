/***************************************************************************
 *   SimulIDE command line control interface                              *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef CLISERVER_H
#define CLISERVER_H

#include <QObject>
#include <QHash>
#include <QByteArray>

class QLocalServer;
class QLocalSocket;
class CliApi;

// Listens on a local socket for newline-delimited JSON requests and runs them
// in the GUI thread through CliApi.
class CliServer : public QObject
{
    Q_OBJECT
    public:
        CliServer( QObject* parent=nullptr );
        ~CliServer();

 static CliServer* self() { return m_pSelf; }

        bool start( QString name, bool headless );
        QString serverName() { return m_name; }

 static QString defaultName();

        // Called from the Qt message handler: forwards warnings/errors to the current CLI reply
 static void logMessage( QString msg );

    protected:
        bool eventFilter( QObject* obj, QEvent* ev ) override;

    private slots:
        void newConnection();

    private:
 static CliServer* m_pSelf;

        void readSocket( QLocalSocket* sock );

        QLocalServer* m_server;
        CliApi*       m_api;
        QString       m_name;
        bool          m_headless;

        QHash<QLocalSocket*, QByteArray> m_buffers;
};
#endif
