/***************************************************************************
 *   SimulIDE command line control interface                              *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef CLISERVER_H
#define CLISERVER_H

#include <QObject>
#include <QTimer>
#include <QHash>
#include <QSet>
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

        // Headless: quit after this long without a CLI request (0 = never). A server left
        // behind by a script or session that ended no longer lives forever.
        void setIdleExit( int ms );
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

        void idleCheck();

        QLocalServer* m_server;
        QTimer*       m_idleTimer;
        int           m_idleMs;
        CliApi*       m_api;
        QString       m_name;
        bool          m_headless;

        QHash<QLocalSocket*, QByteArray> m_buffers;
        QSet<QLocalSocket*> m_busy;   // Connections with a request in progress (replies keep request order)
};
#endif
