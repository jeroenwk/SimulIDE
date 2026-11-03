/***************************************************************************
 *   CLI: scenario scripts (the Scripts pane)                              *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QJsonArray>
#include <QFileInfo>

#include "cliapi.h"
#include "scriptpane.h"

static ScriptPane* pane()
{
    ScriptPane* p = ScriptPane::self();
    if( !p ) throw CliError("no Scripts pane");
    return p;
}

void CliApi::registerScript()
{
    add("script list", "script list", "Scripts next to the open circuit (its folder and scripts/: .py files that use simctl "
        "or carry '# simulide-script'), with their descriptions (docstrings).",
        []( CliCtx& ) -> QJsonValue
    {
        ScriptPane* p = pane();
        p->refresh();
        QJsonArray list;
        for( const QString &f : p->scripts() )
            list.append( QJsonObject{ {"file", f }, {"description", ScriptPane::describe( f ) } } );
        return list;
    });

    add("script run", "script run <file.py> [--args \"ARGS\"] [--wait]", "Run a script in the Scripts pane (python3; it drives "
        "this window through the CLI). Returns at once, or with --wait when it ends. One script at a time.",
        [this]( CliCtx& c ) -> QJsonValue
    {
        ScriptPane* p = pane();
        QString file = resolvePath( c.arg( 0, "file") );
        if( !QFileInfo::exists( file ) ) throw CliError("script not found: "+file );
        QString err;
        if( !p->run( file, c.opt("args"), err ) ) throw CliError( err );
        if( !c.flag("wait") ) return p->status();

        CliPendingPtr reply = c.defer();
        Cli::pollUntil( [reply]()
        {
            ScriptPane* p = ScriptPane::self();
            if( !p ) { reply->fail("Scripts pane gone"); return true; }
            if( p->running() ) return false;
            reply->ok( p->status() );
            return true;
        }, 20 );
        return QJsonValue();
    });

    add("script stop", "script stop", "Stop the running script: it is terminated, a pending 'sim run --for' is cancelled "
        "and the simulation pauses.",
        []( CliCtx& ) -> QJsonValue
    {
        ScriptPane* p = pane();
        if( !p->running() ) throw CliError("no script running");
        p->stop();
        return p->status();
    });

    add("script status", "script status", "State of the last script run (idle, running, finished, failed, stopped), "
        "its exit code, run time and output.",
        []( CliCtx& ) -> QJsonValue { return pane()->status(); });
}
