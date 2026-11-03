/***************************************************************************
 *   SimulIDE command line control interface                              *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QDialog>
#include <QMessageBox>
#include <QThread>
#include <QEvent>

#include "cliserver.h"
#include "cliapi.h"

CliServer* CliServer::m_pSelf = nullptr;

CliServer::CliServer( QObject* parent )
         : QObject( parent )
{
    m_pSelf    = this;
    m_server   = nullptr;
    m_headless = false;
    m_api      = new CliApi();
}
CliServer::~CliServer()
{
    if( m_server ) m_server->close();
    delete m_api;
    m_pSelf = nullptr;
}

QString CliServer::defaultName()
{
    QString env = qEnvironmentVariable("SIMULIDE_SOCKET");
    return env.isEmpty() ? "simulide-cli" : env;
}

bool CliServer::start( QString name, bool headless )
{
    m_name = name.isEmpty() ? defaultName() : name;
    m_headless = headless;
    m_api->setHeadless( headless );

    // Another SimulIDE already serving this name? Then don't steal its socket.
    QLocalSocket probe;
    probe.connectToServer( m_name );
    if( probe.waitForConnected( 200 ) )
    {
        probe.disconnectFromServer();
        qDebug() << "CLI: socket" << m_name << "already in use by another SimulIDE: CLI server not started";
        return false;
    }
    QLocalServer::removeServer( m_name ); // Remove stale socket file

    m_server = new QLocalServer( this );
    m_server->setSocketOptions( QLocalServer::UserAccessOption );
    if( !m_server->listen( m_name ) )
    {
        qDebug() << "CLI: could not listen on" << m_name << ":" << m_server->errorString();
        return false;
    }
    connect( m_server, &QLocalServer::newConnection, this, &CliServer::newConnection );

    if( headless ) qApp->installEventFilter( this );

    qDebug() << "CLI: listening on" << m_server->fullServerName();
    return true;
}

void CliServer::newConnection()
{
    while( QLocalSocket* sock = m_server->nextPendingConnection() )
    {
        m_buffers[sock] = QByteArray();
        connect( sock, &QLocalSocket::readyRead, this, [this, sock](){ readSocket( sock ); } );
        connect( sock, &QLocalSocket::disconnected, this, [this, sock]()
        {
            m_buffers.remove( sock );
            sock->deleteLater();
        });
    }
}

void CliServer::readSocket( QLocalSocket* sock )
{
    QByteArray& buf = m_buffers[sock];
    buf.append( sock->readAll() );

    int nl;
    while( (nl = buf.indexOf('\n')) >= 0 )
    {
        QByteArray line = buf.left( nl ).trimmed();
        buf.remove( 0, nl+1 );
        if( line.isEmpty() ) continue;

        QPointer<QLocalSocket> target( sock );
        auto send = [target]( QJsonObject reply )
        {
            if( !target ) return;
            target->write( QJsonDocument( reply ).toJson( QJsonDocument::Compact )+"\n" );
            target->flush();
        };
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson( line, &err );
        if( err.error != QJsonParseError::NoError || !doc.isObject() )
        {
            send( QJsonObject{ {"ok",false}, {"error","invalid JSON request: "+err.errorString() } } );
            continue;
        }
        m_api->exec( doc.object(), send );
    }
}

void CliServer::logMessage( QString msg )
{
    if( !m_pSelf || !CliApi::self() || !CliApi::self()->capturing() ) return;
    if( QThread::currentThread() != qApp->thread() ) return;
    if( !msg.contains("error", Qt::CaseInsensitive ) ) return;
    CliApi::self()->addMessage( msg.remove('"') );
}

// Headless: modal dialogs would block forever. Report their text and close them.
bool CliServer::eventFilter( QObject* obj, QEvent* ev )
{
    if( ev->type() == QEvent::Show )
    {
        QDialog* dlg = qobject_cast<QDialog*>( obj );
        if( dlg && dlg->isWindow() )
        {
            QString text;
            if( QMessageBox* mb = qobject_cast<QMessageBox*>( dlg ) )
            {
                text = mb->text().trimmed();
                if( !mb->informativeText().isEmpty() ) text += " "+mb->informativeText().trimmed();
            }
            QString title = dlg->windowTitle();
            QString msg = "dialog closed (headless)";
            if( !title.isEmpty() ) msg += " ["+title+"]";
            if( !text.isEmpty()  ) msg += ": "+text;
            if( CliApi::self() ) CliApi::self()->addMessage( msg );
            qDebug() << "CLI:" << msg;
            QMetaObject::invokeMethod( dlg, "reject", Qt::QueuedConnection );
        }
    }
    return QObject::eventFilter( obj, ev );
}
