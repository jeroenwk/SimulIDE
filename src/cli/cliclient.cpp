/***************************************************************************
 *   SimulIDE command line control interface                              *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <cstdio>

#include <QCoreApplication>
#include <QLocalSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStringList>
#include <QTextStream>
#include <QFile>
#include <QDir>

#include "cliclient.h"
#include "cliserver.h"

namespace
{
const char* clientUsage =
"Usage: simulide cli [--socket NAME] [--json] [--timeout SEC] <command> [args]\n"
"       simulide cli [options] -              Run commands from stdin (one per line)\n"
"       simulide cli [options] --script FILE  Run commands from a file\n"
"\n"
"Connects to a running SimulIDE: the GUI, or one started with\n"
"       simulide --headless [--socket NAME] [circuit.sim1]\n"
"\n"
"Try:   simulide cli help\n";

struct Options {
    QString socket;
    bool json = false;
    bool keepGoing = false;
    int  timeoutMs = -1;
};

void printErr( QString s ) { fprintf( stderr, "%s\n", s.toLocal8Bit().constData() ); }
void printOut( QString s ) { fprintf( stdout, "%s\n", s.toLocal8Bit().constData() ); fflush( stdout ); }

// Shell-like split: spaces separate, '...' and "..." group, backslash escapes
QStringList tokenize( QString line, bool& ok )
{
    QStringList out;
    QString cur;
    bool inTok = false;
    QChar quote = 0;
    ok = true;
    for( int i=0; i<line.size(); ++i )
    {
        QChar ch = line.at( i );
        if( quote != 0 )
        {
            if( ch == quote ) quote = 0;
            else if( ch == '\\' && quote == '"' && i+1 < line.size() ) cur += line.at( ++i );
            else cur += ch;
        }
        else if( ch == '\'' || ch == '"' ) { quote = ch; inTok = true; }
        else if( ch == '\\' && i+1 < line.size() ) { cur += line.at( ++i ); inTok = true; }
        else if( ch.isSpace() ) { if( inTok ){ out.append( cur ); cur.clear(); inTok = false; } }
        else { cur += ch; inTok = true; }
    }
    if( quote != 0 ) ok = false;
    if( inTok ) out.append( cur );
    return out;
}

QString scalarStr( QJsonValue v )
{
    if( v.isString() ) return v.toString();
    if( v.isBool()   ) return v.toBool() ? "true" : "false";
    if( v.isDouble() ) return QString::number( v.toDouble(), 'g', 12 );
    if( v.isNull() || v.isUndefined() ) return "";
    return QString::fromUtf8( QJsonDocument::fromVariant( v.toVariant() ).toJson( QJsonDocument::Compact ) );
}

// Short text for a value inside a table cell
QString cellStr( QJsonValue v )
{
    if( v.isArray() ){
        QJsonArray a = v.toArray();
        bool scalars = true;
        for( QJsonValue e : a ) if( e.isArray() || e.isObject() ) scalars = false;
        if( scalars ){
            QStringList parts;
            for( QJsonValue e : a ) parts.append( scalarStr( e ) );
            return parts.join(",");
        }
        return QString("[%1 items]").arg( a.size() );
    }
    return scalarStr( v );
}

void printTable( QJsonArray rows, QString indent="" )
{
    static const QStringList preferred = {"uid","ref","index","id","name","label","type","value","unit",
                                          "from","to","wire","pos","rotation","voltage","pins","group","caption"};
    QStringList cols;
    for( QString c : preferred )
        for( QJsonValue r : rows ) if( r.toObject().contains( c ) ){ cols.append( c ); break; }
    for( QJsonValue r : rows )
        for( QString k : r.toObject().keys() ) if( !cols.contains( k ) ) cols.append( k );

    QList<QStringList> cells;
    QList<int> widths;
    for( QString c : cols ) widths.append( c.size() );
    for( QJsonValue r : rows ){
        QStringList line;
        QJsonObject o = r.toObject();
        for( int i=0; i<cols.size(); ++i ){
            QString t = o.contains( cols[i] ) ? cellStr( o.value( cols[i] ) ) : "";
            widths[i] = qMax( widths[i], t.size() );
            line.append( t );
        }
        cells.append( line );
    }
    auto fmt = [&]( QStringList line ){
        QString out = indent;
        for( int i=0; i<line.size(); ++i ) out += ( i+1 < line.size() ) ? line[i].leftJustified( widths[i]+2 ) : line[i];
        return out;
    };
    printOut( fmt( cols ).toUpper() );
    for( QStringList& line : cells ) printOut( fmt( line ) );
}

bool isTable( QJsonValue v )
{
    if( !v.isArray() ) return false;
    for( QJsonValue e : v.toArray() ) if( !e.isObject() ) return false;
    return !v.toArray().isEmpty();
}

void printHuman( QString cmd, QJsonValue result )
{
    if( result.isString() ) { printOut( result.toString() ); return; }
    if( result.isNull() || result.isUndefined() ) return;
    if( !result.isArray() && !result.isObject() ) { printOut( scalarStr( result ) ); return; }

    if( cmd == "serial read" && result.isObject() )   // Raw data
    {
        QJsonObject o = result.toObject();
        QString text = o.contains("hex") ? o.value("hex").toString() : o.value("text").toString();
        fputs( text.toLocal8Bit().constData(), stdout );
        if( !text.isEmpty() && !text.endsWith('\n') ) fputs("\n", stdout );
        fflush( stdout );
        return;
    }
    if( cmd == "help" && result.isArray() )
    {
        int w = 0;
        for( QJsonValue v : result.toArray() ) w = qMax( w, v.toObject().value("usage").toString().size() );
        for( QJsonValue v : result.toArray() )
        {
            QJsonObject o = v.toObject();
            printOut( o.value("usage").toString().leftJustified( w+2 )+o.value("help").toString() );
        }
        return;
    }
    if( result.isArray() )
    {
        if( result.toArray().isEmpty() ) { printOut("(none)"); return; }
        if( isTable( result ) ) { printTable( result.toArray() ); return; }
        for( QJsonValue v : result.toArray() ) printOut( cellStr( v ) );
        return;
    }
    // Object: scalar fields first, then nested lists as tables
    QJsonObject o = result.toObject();
    int w = 0;
    for( QString k : o.keys() ) w = qMax( w, k.size() );
    for( QString k : o.keys() )
        if( !isTable( o.value( k ) ) && !o.value( k ).isObject() ) printOut( (k+":").leftJustified( w+2 )+cellStr( o.value( k ) ) );
    for( QString k : o.keys() )
    {
        QJsonValue v = o.value( k );
        if( v.isObject() ){
            printOut("\n"+k+":");
            QJsonObject sub = v.toObject();
            for( QString sk : sub.keys() ) printOut("  "+sk+": "+cellStr( sub.value( sk ) ) );
        }
        else if( isTable( v ) ){
            printOut("\n"+k+":");
            printTable( v.toArray(), "  " );
    }   }
}

class Connection
{
    public:
        bool open( QString name )
        {
            m_sock.connectToServer( name );
            return m_sock.waitForConnected( 2000 );
        }
        // Returns false if the connection failed
        bool request( QJsonObject req, int timeoutMs, QJsonObject& reply )
        {
            req["id"] = ++m_id;
            m_sock.write( QJsonDocument( req ).toJson( QJsonDocument::Compact )+"\n" );
            if( !m_sock.waitForBytesWritten( 5000 ) ) return false;

            while( true )
            {
                int nl;
                while( (nl = m_buf.indexOf('\n')) >= 0 )
                {
                    QByteArray line = m_buf.left( nl );
                    m_buf.remove( 0, nl+1 );
                    QJsonObject r = QJsonDocument::fromJson( line ).object();
                    if( r.value("id").toInt() == m_id || !r.contains("id") ) { reply = r; return true; }
                }
                if( m_sock.state() != QLocalSocket::ConnectedState && !m_sock.bytesAvailable() ) return false;
                if( !m_sock.waitForReadyRead( timeoutMs ) )
                {
                    if( m_sock.state() != QLocalSocket::ConnectedState ) return false;
                    reply = QJsonObject{ {"ok",false}, {"error","timed out waiting for reply"} };
                    return true;
                }
                m_buf.append( m_sock.readAll() );
            }
        }
    private:
        QLocalSocket m_sock;
        QByteArray   m_buf;
        int m_id = 0;
};

// Returns exit code: 0 ok, 1 command error, 2 connection error
int runOne( Connection& conn, QStringList words, const Options& opt )
{
    QJsonObject req{
        {"argv", QJsonArray::fromStringList( words ) },
        {"cwd",  QDir::currentPath() } };
    QJsonObject reply;
    if( !conn.request( req, opt.timeoutMs, reply ) )
    {
        printErr("error: lost connection to SimulIDE");
        return 2;
    }
    if( opt.json ) printOut( QString::fromUtf8( QJsonDocument( reply ).toJson( QJsonDocument::Compact ) ) );

    for( QJsonValue m : reply.value("messages").toArray() ) if( !opt.json ) printErr("note: "+m.toString() );

    if( !reply.value("ok").toBool() )
    {
        if( !opt.json ) printErr("error: "+reply.value("error").toString() );
        return 1;
    }
    if( !opt.json )
    {
        QString cmd = words.mid( 0, 2 ).join(" ");
        if( cmd.startsWith("help") ) cmd = "help";
        printHuman( cmd, reply.value("result") );
    }
    return 0;
}
}

int runCliClient( int argc, char* argv[] )
{
    QCoreApplication app( argc, argv );

    Options opt;
    opt.socket = CliServer::defaultName();
    QStringList args;
    for( int i=2; i<argc; ++i ) args.append( QString::fromLocal8Bit( argv[i] ) );

    QString script;
    bool fromStdin = false;
    while( !args.isEmpty() && args.first().startsWith("-") )   // Client options before the command
    {
        QString a = args.takeFirst();
        if     ( a == "--json" ) opt.json = true;
        else if( a == "--keep-going" ) opt.keepGoing = true;
        else if( a == "-" ) { fromStdin = true; break; }
        else if( a == "--help" || a == "-h" ) { printOut( clientUsage ); return 0; }
        else if( a == "--socket" || a == "--timeout" || a == "--script" )
        {
            if( args.isEmpty() ) { printErr("error: "+a+" needs a value"); return 1; }
            QString v = args.takeFirst();
            if     ( a == "--socket"  ) opt.socket = v;
            else if( a == "--timeout" ) opt.timeoutMs = (int)( v.toDouble()*1000 );
            else                        script = v;
        }
        else { printErr("error: unknown option "+a+"\n\n"+clientUsage ); return 1; }
    }
    if( args.isEmpty() && script.isEmpty() && !fromStdin ) { printErr( clientUsage ); return 1; }

    Connection conn;
    if( !conn.open( opt.socket ) )
    {
        printErr("error: no SimulIDE listening on socket '"+opt.socket+"'.\n"
                 "Start one with:  simulide --headless --socket "+opt.socket+"\n"
                 "or open the SimulIDE GUI (it listens on '"+CliServer::defaultName()+"' by default).");
        return 2;
    }
    if( script.isEmpty() && !fromStdin ) return runOne( conn, args, opt );

    // Script mode: one command per line
    QFile file;
    if( fromStdin ) file.open( stdin, QIODevice::ReadOnly );
    else{
        file.setFileName( script );
        if( !file.open( QIODevice::ReadOnly ) ) { printErr("error: cannot read script "+script ); return 1; }
    }
    QTextStream in( &file );
    int lineNum = 0;
    int result = 0;
    while( !in.atEnd() )
    {
        QString line = in.readLine().trimmed();
        lineNum++;
        if( line.isEmpty() || line.startsWith('#') ) continue;
        bool ok;
        QStringList words = tokenize( line, ok );
        if( !ok ) { printErr( QString("error: line %1: unbalanced quotes").arg( lineNum ) ); return 1; }

        if( !opt.json ) printOut("> "+line );
        int r = runOne( conn, words, opt );
        if( r == 2 ) return 2;
        if( r != 0 ){
            result = r;
            if( !opt.keepGoing ){ printErr( QString("stopped at line %1").arg( lineNum ) ); return r; }
        }
    }
    return result;
}
