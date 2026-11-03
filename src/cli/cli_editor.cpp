/***************************************************************************
 *   SimulIDE command line control interface                              *
 *   Code editor, compiler and debugger commands                           *
 ***( see copyright.txt file at root folder )*******************************/

#include <QJsonArray>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QMetaObject>

#include "cliapi.h"
#include "editorwindow.h"
#include "codeeditor.h"
#include "compiler.h"
#include "comproperty.h"
#include "simulator.h"

namespace
{
EditorWindow* editor() { return EditorWindow::self(); }

QString stateName( bebugState_t s )
{
    switch( s ){
        case DBG_STOPPED: return "stopped";
        case DBG_PAUSED:  return "paused";
        case DBG_STEPING: return "stepping";
        case DBG_RUNNING: return "running";
    }
    return "unknown";
}

QString fileOf( QString path ) { return QFileInfo( path ).absoluteFilePath(); }

// Select a document: the given file (opening it if needed) or the current one
CodeEditor* selectDoc( CliCtx& c, QString file="" )
{
    if( file.isEmpty() ) file = c.opt("file");
    if( !file.isEmpty() )
    {
        QString path = fileOf( CliApi::self()->resolvePath( file ) );
        if( !editor()->fileWidget( path ) )
        {
            if( !QFileInfo::exists( path ) ) throw CliError("file not found: "+path );
            editor()->loadFile( path );
            if( !editor()->fileWidget( path ) ) throw CliError("could not open "+path );
        }
        else editor()->loadFile( path );  // Makes it current
    }
    CodeEditor* ce = editor()->getCodeEditor();
    if( !ce ) throw CliError("no file open in the editor (use: editor open <file>)");
    return ce;
}

QJsonArray propsJson( CompBase* comp )
{
    QJsonArray props;
    for( propGroup& group : *comp->properties() )
    {
        if( group.flags & groupHidden ) continue;
        for( ComProperty* p : group.propList )
        {
            if( p->flags() & propHidden ) continue;
            if( p->name().isEmpty() || p->capt().isEmpty() ) continue;  // Separators
            QJsonObject po{
                {"name",  p->name() },
                {"value", p->getValStr() },
                {"caption", p->capt() } };
            if( !p->type().isEmpty() ) po["type"] = p->type();
            props.append( po );
    }   }
    return props;
}

void setProps( CompBase* comp, QJsonObject sets )
{
    for( QString key : sets.keys() )
    {
        ComProperty* found = nullptr;
        for( propGroup& group : *comp->properties() )
            for( ComProperty* p : group.propList )
                if( p->name().compare( key, Qt::CaseInsensitive ) == 0 ) found = p;
        if( !found ) throw CliError("unknown setting: "+key );
        comp->setPropStr( found->name(), sets.value( key ).toString() );
    }
}

QJsonObject docJson( CodeEditor* ce )
{
    QJsonObject o{
        {"file",     ce->getFile() },
        {"current",  ce == editor()->getCodeEditor() },
        {"modified", ce->document()->isModified() },
        {"compiler", ce->compName() } };
    QJsonArray bps;
    for( int l : *ce->getBreakPoints() ) bps.append( l );
    if( !bps.isEmpty() ) o["breakpoints"] = bps;
    return o;
}

QJsonObject editorStatus()
{
    EditorWindow* ew = editor();
    QJsonObject o{ {"debug", stateName( ew->debugState() ) } };
    if( CodeEditor* ce = ew->getCodeEditor() ){
        o["file"] = ce->getFile();
        o["compiler"] = ce->compName();
    }
    if( ew->debugStarted() )
    {
        codeLine_t line = ew->debugLinePos();
        if( line.lineNumber > 0 ){
            o["line"] = line.lineNumber;
            if( !line.file.isEmpty() ) o["lineFile"] = line.file;
        }
        o["simTime"] = Cli::timeStr( Simulator::self()->circTime() );
    }
    return o;
}

// Calls a (possibly protected) slot of the editor by name
bool invoke( const char* slot )
{
    return QMetaObject::invokeMethod( editor(), slot, Qt::DirectConnection );
}

// Run fn while capturing compiler output; returns captured text
QString captureOutput( std::function<void()> fn )
{
    QString out;
    editor()->outPane()->setCapture( &out );
    try { fn(); }
    catch( ... ) { editor()->outPane()->setCapture( nullptr ); throw; }
    editor()->outPane()->setCapture( nullptr );
    return out.trimmed();
}

void requireDebug()
{
    if( !editor()->debugStarted() ) throw CliError("debugger is not running (use: editor debug start)");
}

// Reply when the debugger pauses (line reached, breakpoint) or after timeout
void replyWhenPaused( CliCtx& c, int timeoutMs )
{
    CliPendingPtr reply = c.defer();
    auto elapsed = std::make_shared<QElapsedTimer>();
    elapsed->start();
    Cli::pollUntil( [reply, elapsed, timeoutMs]() -> bool
    {
        bebugState_t st = editor()->debugState();
        if( st == DBG_STOPPED ) { reply->fail("debugger stopped"); return true; }
        if( st == DBG_PAUSED  ) { reply->ok( editorStatus() ); return true; }
        if( elapsed->elapsed() >= timeoutMs )
        {
            QJsonObject o = editorStatus();
            o["timedOut"] = true;
            reply->ok( o );
            return true;
        }
        return false;
    }, 10 );
}
}

void CliApi::registerEditor()
{
    add("editor open", "editor open <file>", "Open a source file in the code editor (or make it the current one).",
        []( CliCtx& c ) -> QJsonValue
    {
        return docJson( selectDoc( c, c.arg( 0, "file") ) );
    });

    add("editor files", "editor files", "Files open in the code editor.",
        []( CliCtx& ) -> QJsonValue
    {
        QJsonArray rows;
        for( QString f : editor()->getFiles() )
            if( CodeEditor* ce = dynamic_cast<CodeEditor*>( editor()->fileWidget( f ) ) )
                rows.append( docJson( ce ) );
        return rows;
    });

    add("editor status", "editor status", "Current file, compiler and debugger state (with the current source line).",
        []( CliCtx& ) -> QJsonValue { return editorStatus(); });

    add("editor save", "editor save [file]", "Save a file open in the editor.",
        []( CliCtx& c ) -> QJsonValue
    {
        CodeEditor* ce = selectDoc( c, c.argOr( 0, "") );
        bool ok = false;
        QMetaObject::invokeMethod( editor(), "save", Qt::DirectConnection, Q_RETURN_ARG( bool, ok ) );
        if( !ok ) throw CliError("could not save "+ce->getFile() );
        return docJson( ce );
    });

    add("editor reload", "editor reload [file]", "Reload a file from disk (after editing it outside SimulIDE). Discards unsaved changes.",
        []( CliCtx& c ) -> QJsonValue
    {
        CodeEditor* ce = selectDoc( c, c.argOr( 0, "") );
        invoke("reload");
        return docJson( ce );
    });

    add("editor close", "editor close [file] [--discard]", "Close a file. Refuses unsaved changes unless --discard.",
        []( CliCtx& c ) -> QJsonValue
    {
        CodeEditor* ce = selectDoc( c, c.argOr( 0, "") );
        if( editor()->debugStarted() ) throw CliError("stop the debugger first (editor debug stop)");
        if( ce->document()->isModified() )
        {
            if( !c.flag("discard") ) throw CliError("file has unsaved changes: use 'editor save' or --discard");
            ce->document()->setModified( false );
        }
        QString file = ce->getFile();
        int index = editor()->fileTabIndex( file );
        QMetaObject::invokeMethod( editor(), "closeTab", Qt::DirectConnection, Q_ARG( int, index ) );
        return QJsonValue( "closed "+file );
    });

    add("editor compilers", "editor compilers", "Available compilers and assemblers.",
        []( CliCtx& ) -> QJsonValue
    {
        QJsonArray rows;
        for( QString n : editor()->compilers()  ) rows.append( QJsonObject{ {"name", n }, {"kind", "compiler"} } );
        for( QString n : editor()->assemblers() ) rows.append( QJsonObject{ {"name", n }, {"kind", "assembler"} } );
        return rows;
    });

    add("editor compiler", "editor compiler [name] [--file F] [setting=value...]",
        "Show or set the compiler of a file and its settings (tool path, include path, device, family, extra args...).",
        []( CliCtx& c ) -> QJsonValue
    {
        CodeEditor* ce = selectDoc( c );
        if( !c.pos.isEmpty() )
        {
            QString name = c.pos.join(" ");
            QStringList all = editor()->compilers()+editor()->assemblers();
            QString match;
            for( QString n : all ) if( n.compare( name, Qt::CaseInsensitive ) == 0 ) match = n;
            if( match.isEmpty() ) throw CliError("unknown compiler: "+name+" (see: editor compilers)");
            ce->setCompName( match );
        }
        BaseDebugger* comp = ce->getCompiler();
        if( !c.sets.isEmpty() )
        {
            if( !comp ) throw CliError("no compiler set for "+ce->getFile() );
            setProps( comp, c.sets );
        }
        QJsonObject o{ {"file", ce->getFile() }, {"compiler", ce->compName() } };
        if( comp ) o["settings"] = propsJson( comp );
        return o;
    });

    add("editor compile", "editor compile [file]", "Compile a file (saving it first). Returns the compiler output.",
        []( CliCtx& c ) -> QJsonValue
    {
        CodeEditor* ce = selectDoc( c, c.argOr( 0, "") );
        if( !ce->getCompiler() ) throw CliError("no compiler set for "+ce->getFile()+" (see: editor compiler)");
        bool ok = false;
        QString out = captureOutput( [&](){ ok = ce->compile( false ); } );
        if( !ok ) throw CliError("compilation failed:\n"+out );
        return QJsonObject{ {"ok", true }, {"file", ce->getFile() }, {"output", out } };
    });

    add("editor upload", "editor upload [file]", "Compile a file and load the firmware into the main MCU.",
        []( CliCtx& c ) -> QJsonValue
    {
        CodeEditor* ce = selectDoc( c, c.argOr( 0, "") );
        if( !ce->getCompiler() ) throw CliError("no compiler set for "+ce->getFile()+" (see: editor compiler)");
        bool ok = false;
        QString out = captureOutput( [&](){
            QMetaObject::invokeMethod( editor(), "upload", Qt::DirectConnection, Q_RETURN_ARG( bool, ok ) );
        });
        if( !ok ) throw CliError("upload failed:\n"+out );
        return QJsonObject{ {"ok", true }, {"file", ce->getFile() }, {"output", out } };
    });

    add("editor debug", "editor debug start|step|over|run|pause|reset|stop|status [--file F] [--wait T]",
        "Source level debugger. step/over reply when the next line is reached; "
        "run returns at once, or with --wait T when a breakpoint is hit (or T elapses).",
        []( CliCtx& c ) -> QJsonValue
    {
        QString verb = c.arg( 0, "action").toLower();
        int waitMs = c.has("wait") ? (int)( Cli::parseTime( c.opt("wait") )/1000000000 ) : 0;

        if( verb == "status" ) return editorStatus();
        if( verb == "start" )
        {
            if( editor()->debugStarted() ) throw CliError("debugger already running");
            CodeEditor* ce = selectDoc( c );
            if( !ce->getCompiler() ) throw CliError("no compiler set for "+ce->getFile()+" (see: editor compiler)");
            QString out = captureOutput( [](){ invoke("debug"); } );
            if( !editor()->debugStarted() ) throw CliError("debugger could not start:\n"+out );
            QJsonObject o = editorStatus();
            o["output"] = out;
            return o;
        }
        requireDebug();
        if( verb == "step" || verb == "over" )
        {
            if( editor()->debugState() == DBG_RUNNING ) throw CliError("debugger is running: pause it first");
            invoke( verb == "step" ? "step" : "stepOver");
            replyWhenPaused( c, waitMs ? waitMs : 10000 );
            return QJsonValue();
        }
        if( verb == "run" )
        {
            invoke("run");
            if( waitMs ) { replyWhenPaused( c, waitMs ); return QJsonValue(); }
            return editorStatus();
        }
        if( verb == "pause" ) { invoke("pause"); return editorStatus(); }
        if( verb == "reset" ) { invoke("reset"); return editorStatus(); }
        if( verb == "stop"  ) { invoke("stop");  return editorStatus(); }
        throw CliError("unknown debug action: "+verb+" (start, step, over, run, pause, reset, stop, status)");
    });

    add("editor break", "editor break add|remove|list|clear [line] [--file F]",
        "Manage breakpoints (source line numbers) of a file.",
        []( CliCtx& c ) -> QJsonValue
    {
        QString verb = c.argOr( 0, "list").toLower();
        CodeEditor* ce = selectDoc( c );
        QList<int>* bps = ce->getBreakPoints();

        if( verb == "add" || verb == "remove" )
        {
            if( c.pos.size() < 2 ) throw CliError("missing line number");
            for( QString l : c.pos.mid( 1 ) )
            {
                int line = (int)Cli::parseNum( l, "line");
                if( line < 1 || line > ce->document()->blockCount() )
                    throw CliError( QString("line out of range: %1 (file has %2 lines)").arg( line ).arg( ce->document()->blockCount() ) );
                if( verb == "add" ) ce->addBreakPoint( line );
                else                bps->removeAll( line );
            }
            ce->update();
        }
        else if( verb == "clear" ) { ce->slotClearBreak(); ce->update(); }
        else if( verb != "list" ) throw CliError("unknown action: "+verb+" (add, remove, list, clear)");

        QJsonArray lines;
        for( int l : *bps ) lines.append( l );
        return QJsonObject{ {"file", ce->getFile() }, {"breakpoints", lines } };
    });
}
