/***************************************************************************
 *   SimulIDE command line control interface                              *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QCoreApplication>
#include <QRegularExpression>
#include <QTimer>
#include <QDir>
#include <QFileInfo>

#include "cliapi.h"

CliApi* CliApi::m_pSelf = nullptr;

//----------------------------------------------------------------------------
// CliPending / CliCtx

void CliPending::ok( QJsonValue result )
{
    if( !m_send ) return;
    auto send = m_send;
    m_send = nullptr;
    send( true, result, "" );
}

void CliPending::fail( QString error )
{
    if( !m_send ) return;
    auto send = m_send;
    m_send = nullptr;
    send( false, QJsonValue(), error );
}

QString CliCtx::arg( int i, QString name )
{
    if( i >= pos.size() ) throw CliError("missing argument: "+name );
    return pos.at( i );
}

QString CliCtx::opt( QString key, QString def )
{
    if( !opts.contains( key ) ) return def;
    QJsonValue v = opts.value( key );
    if( v.isString() ) return v.toString();
    if( v.isDouble() ) return QString::number( v.toDouble() );
    if( v.isBool()   ) return v.toBool() ? "true" : "false";
    return def;
}

bool CliCtx::flag( QString key )
{
    if( !opts.contains( key ) ) return false;
    QJsonValue v = opts.value( key );
    if( v.isBool() ) return v.toBool();
    QString s = v.toVariant().toString().toLower();
    return !( s == "false" || s == "0" || s == "no" || s == "off" );
}

CliPendingPtr CliCtx::defer()
{
    if( !m_pending ) m_pending = std::make_shared<CliPending>();
    return m_pending;
}

//----------------------------------------------------------------------------
// Helpers

namespace Cli
{
uint64_t parseTime( QString t )
{
    static const QRegularExpression re("^\\s*([0-9]*\\.?[0-9]+(?:[eE][-+]?[0-9]+)?)\\s*(ps|ns|us|µs|ms|s)?\\s*$");
    QRegularExpressionMatch m = re.match( t );
    if( !m.hasMatch() ) throw CliError("invalid time: "+t+" (examples: 500us, 10ms, 1s)");

    double val = m.captured(1).toDouble();
    QString unit = m.captured(2);
    double mult = 1e12;                     // Default: seconds
    if     ( unit == "ps" ) mult = 1;
    else if( unit == "ns" ) mult = 1e3;
    else if( unit == "us" || unit == "µs" ) mult = 1e6;
    else if( unit == "ms" ) mult = 1e9;
    return (uint64_t)( val*mult + 0.5 );
}

QString timeStr( uint64_t ps )
{
    double s = (double)ps/1e12;
    if( s >= 1    ) return QString::number( s )+" s";
    if( s >= 1e-3 ) return QString::number( s*1e3 )+" ms";
    if( s >= 1e-6 ) return QString::number( s*1e6 )+" us";
    if( s >= 1e-9 ) return QString::number( s*1e9 )+" ns";
    return QString::number( ps )+" ps";
}

double parseNum( QString v, QString what )
{
    bool ok = false;
    double d = v.toDouble( &ok );
    if( !ok ) throw CliError("invalid number for "+what+": "+v );
    return d;
}

void pollUntil( std::function<bool()> fn, int ms )
{
    QTimer* timer = new QTimer();
    QObject::connect( timer, &QTimer::timeout, [timer, fn]()
    {
        bool finished = true;
        try { finished = fn(); }
        catch( ... ) { finished = true; }
        if( finished ){ timer->stop(); timer->deleteLater(); }
    });
    timer->start( ms );
}
}

//----------------------------------------------------------------------------
// CliApi

CliApi::CliApi()
{
    m_pSelf = this;
    m_capturing = false;
    m_headless  = false;

    registerCore();
    registerCircuit();
    registerEdit();
    registerMeasure();
    registerSim();
    registerMcu();
}
CliApi::~CliApi(){ m_pSelf = nullptr; }

void CliApi::add( QString name, QString usage, QString help, CliHandler fn )
{
    m_cmds[name] = { name, usage, help, fn };
}

QString CliApi::resolvePath( QString path )
{
    if( path.isEmpty() ) return path;
    if( path.startsWith("~/") ) path = QDir::homePath()+path.mid( 1 );
    QFileInfo fi( path );
    if( fi.isRelative() && !m_cwd.isEmpty() ) fi = QFileInfo( QDir( m_cwd ), path );
    return fi.absoluteFilePath();
}

void CliApi::addMessage( QString msg )
{
    msg = msg.trimmed();
    if( msg.isEmpty() ) return;
    if( !m_messages.contains( msg ) ) m_messages.append( msg );
}

bool CliApi::parseArgv( QJsonArray argv, QString& cmd, CliCtx& ctx, QString& error )
{
    QStringList tokens;
    for( QJsonValue v : argv ) tokens.append( v.toVariant().toString() );

    if( tokens.isEmpty() ) { error = "no command given (try: help)"; return false; }

    int start = 1;
    if( tokens.size() > 1 && m_cmds.contains( tokens.at(0)+" "+tokens.at(1) ) )
    {
        cmd = tokens.at(0)+" "+tokens.at(1);
        start = 2;
    }
    else cmd = tokens.at(0);

    // Options that never take a value (so a following positional is not consumed)
    static const QStringList boolFlags = {"discard","hex","peek","cr","lf","crlf","rx","tx","all"};

    bool optsEnded = false;
    for( int i=start; i<tokens.size(); ++i )
    {
        QString t = tokens.at(i);
        if( optsEnded ) { ctx.pos.append( t ); continue; }
        if( t == "--" ) { optsEnded = true; continue; }  // Everything after "--" is positional
        if( t.startsWith("--") && t.size() > 2 )
        {
            QString key = t.mid( 2 );
            int eq = key.indexOf('=');
            if( eq > 0 ) ctx.opts[key.left( eq )] = key.mid( eq+1 );
            else if( boolFlags.contains( key ) ) ctx.opts[key] = true;
            else if( i+1 < tokens.size() && !tokens.at(i+1).startsWith("--") )
                ctx.opts[key] = tokens.at( ++i );
            else ctx.opts[key] = true;
        }
        else if( t.contains('=') && !t.startsWith('=') && !t.startsWith('-') )
        {
            int eq = t.indexOf('=');
            ctx.sets[t.left( eq )] = t.mid( eq+1 );
        }
        else ctx.pos.append( t );
    }
    return true;
}

void CliApi::exec( const QJsonObject& req, std::function<void(QJsonObject)> done )
{
    QJsonValue id = req.value("id");
    m_cwd = req.value("cwd").toString();
    CliCtx ctx;
    QString cmd;
    QString error;

    if( req.contains("argv") )
    {
        if( !parseArgv( req.value("argv").toArray(), cmd, ctx, error ) )
        {
            done( QJsonObject{ {"id",id}, {"ok",false}, {"error",error} } );
            return;
        }
    }else{
        cmd = req.value("cmd").toString();
        QJsonObject args = req.value("args").toObject();
        for( QString key : args.keys() )
        {
            QJsonValue v = args.value( key );
            if( key == "_" ){
                for( QJsonValue p : v.toArray() ) ctx.pos.append( p.toVariant().toString() );
            }
            else if( key == "set" ) ctx.sets = v.toObject();
            else                    ctx.opts[key] = v;
    }   }

    auto makeReply = [id]( bool ok, QJsonValue result, QString err, QStringList msgs )
    {
        QJsonObject reply{ {"id",id}, {"ok",ok} };
        if( ok ) reply["result"] = result;
        else     reply["error"]  = err;
        if( !msgs.isEmpty() ) reply["messages"] = QJsonArray::fromStringList( msgs );
        return reply;
    };

    if( !m_cmds.contains( cmd ) )
    {
        QStringList verbs;
        for( QString name : m_cmds.keys() )
            if( name.startsWith( cmd+" ") ) verbs.append( name.mid( cmd.size()+1 ) );
        QString err = "unknown command: "+cmd;
        if( !ctx.pos.isEmpty() ) err += " "+ctx.pos.first();
        err += verbs.isEmpty() ? " (try: help)" : " (valid: "+cmd+" "+verbs.join("|")+")";
        done( makeReply( false, QJsonValue(), err, {} ) );
        return;
    }
    m_messages.clear();
    m_capturing = true;

    bool ok = true;
    QJsonValue result;
    try {
        result = m_cmds[cmd].fn( ctx );
    }
    catch( CliError& e ) { ok = false; error = e.msg; }
    catch( std::exception& e ) { ok = false; error = QString("internal error: ")+e.what(); }

    m_capturing = false;
    QStringList msgs = m_messages;
    m_messages.clear();

    CliPendingPtr pending = ctx.pending();
    if( ok && pending )   // Reply will be sent later
    {
        pending->m_send = [done, makeReply, msgs]( bool ok, QJsonValue res, QString err )
        {
            done( makeReply( ok, res, err, msgs ) );
        };
        return;
    }
    done( makeReply( ok, result, error, msgs ) );
}

//----------------------------------------------------------------------------
// Core commands

void CliApi::registerCore()
{
    add("ping", "ping", "Check that SimulIDE is listening.", []( CliCtx& ) -> QJsonValue
    {
        return "pong";
    });

    add("version", "version", "SimulIDE version.", [this]( CliCtx& ) -> QJsonValue
    {
        return QJsonObject{
            {"version", QCoreApplication::applicationVersion() },
            {"headless", m_headless } };
    });

    add("help", "help [command]", "List commands, or show usage of one command.", [this]( CliCtx& c ) -> QJsonValue
    {
        QString filter = c.pos.join(" ");
        QJsonArray list;
        for( CliCommand& cmd : m_cmds )
        {
            if( !filter.isEmpty() && cmd.name != filter && !cmd.name.startsWith( filter+" ") ) continue;
            list.append( QJsonObject{ {"command",cmd.name}, {"usage",cmd.usage}, {"help",cmd.help} } );
        }
        if( list.isEmpty() ) throw CliError("unknown command: "+filter );
        return list;
    });
}
