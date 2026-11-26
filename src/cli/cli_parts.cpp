/***************************************************************************
 *   SimulIDE command line control interface                              *
 *   Parts pane, BOM and locked parts (specs/parts-pane.md §8)             *
 ***( see copyright.txt file at root folder )*******************************/

#include <QFileInfo>
#include <QJsonArray>

#include "cliapi.h"
#include "clirefs.h"
#include "circuit.h"
#include "circuitwidget.h"
#include "component.h"
#include "partstable.h"
#include "partspane.h"
#include "taborder.h"

namespace
{
// "R1,R2 R10" -> parts (all resolved first)
QList<Component*> partsArg( const QStringList& pos, int from=0 )
{
    QList<Component*> out;
    for( int i=from; i<pos.size(); ++i )
        for( QString ref : pos.at( i ).split(",", QString::SkipEmptyParts ) )
        {
            Component* c = CliRefs::comp( ref.trimmed() );
            if( !out.contains( c ) ) out << c;
        }
    return out;
}

PartsTable::Filter filterOpts( CliCtx& c )
{
    PartsTable::Filter f;
    if( c.flag("all") ) f.all();
    for( QString k : c.opt("filter").split(",", QString::SkipEmptyParts ) )
    {
        k = k.trimmed().toLower();
        if     ( k == "graphical" ) f.graphical = true;
        else if( k == "meters" )    f.meters = true;
        else if( k == "sources" )   f.sources = true;
        else if( k == "locked" )    f.locked = true;
        else throw CliError("--filter takes graphical, meters, sources, locked (comma separated)");
    }
    return f;
}

QList<PartsTable::SortKey> sortOpts( CliCtx& c )
{
    QList<PartsTable::SortKey> keys;
    QStringList cols = PartsTable::colKeys();
    for( QString k : c.opt("sort").split(",", QString::SkipEmptyParts ) )
    {
        k = k.trimmed().toLower();
        bool desc = k.startsWith('-');
        if( desc ) k = k.mid( 1 );
        int col = cols.indexOf( k );
        if( col < 0 ) throw CliError("--sort: unknown column "+k+" ("+cols.join(", ")+"; -col: reversed)");
        keys.append( { col, desc } );
    }
    return keys;
}

QJsonObject rowJson( const PartRow& r )
{
    QJsonObject o{ {"uid", r.uid}, {"type", r.type}, {"label", r.label}, {"model", r.model},
                   {"value", r.value}, {"other", r.other}, {"category", r.category},
                   {"locked", r.locked}, {"hidden", r.hidden} };
    o["tab"] = r.tab ? QJsonValue( r.tab ) : QJsonValue();
    if( r.hasNum ) o["valueNum"] = r.valueNum;
    QStringList keys = PartsTable::bomKeys();
    for( int i=0; i<5; ++i ) o[keys.at( i )] = r.bom[i];
    return o;
}

QJsonValue lockCmd( CliCtx& c, bool lock )
{
    Circuit* circ = Circuit::self();
    QList<Component*> parts;
    if( !lock && c.flag("all") ){
        for( Component* p : *circ->compList() ) if( p->isLocked() ) parts << p;
    }else{
        if( c.pos.isEmpty() ) throw CliError( lock ? "lock <comp>[,<comp>…]" : "unlock <comp>[,<comp>…] | --all");
        parts = partsArg( c.pos );
    }
    QString msg = circ->lockParts( parts, lock );
    QJsonArray locked;
    for( Component* p : *circ->compList() ) if( p->isLocked() ) locked.append( p->idLabel() );
    return QJsonObject{ {"message", msg}, {"locked", locked} };
}
}

void CliApi::registerParts()
{
    add("parts list", "parts list [--all] [--filter graphical,meters,sources] [--search TEXT] [--sort COL[,COL]] [--locked] [--selected] [--json]",
        "The Parts pane's rows (specs/parts-pane.md): type as in the component list, label, model, value, other values "
        "(every changed setting), Tab place, category, locked, BOM fields. By default the pane's default filters "
        "(graphical, meters and sources left out) and BOM order (by label: C1 C2 C10 D1 R1). --all: every part; "
        "--sort type,-value (- reversed; columns lock type label model value other tab category mfr mpn supplier spn notes); "
        "--locked: only locked parts; --selected: the rows the pane shows selected (its two-way sync with the canvas).",
        []( CliCtx& c ) -> QJsonValue
    {
        if( c.flag("selected") )
        {
            PartsPane* pane = PartsPane::self();
            if( !pane ) throw CliError("no Parts pane");
            pane->syncNow();
            QStringList uids = pane->selectedUids();
            QJsonArray out;
            for( const PartRow& r : PartsTable::rows() ) if( uids.contains( r.uid ) ) out.append( rowJson( r ) );
            CliApi::self()->addMessage( pane->countText() );
            return out;
        }
        PartsTable::Filter f = filterOpts( c );
        if( c.flag("locked") ) f.all();
        QList<PartRow> rows;
        for( const PartRow& r : PartsTable::rows() )
        {
            if( c.flag("locked") && !r.locked ) continue;
            if( !PartsTable::passes( r, f ) ) continue;
            if( !PartsTable::matches( r, c.opt("search") ) ) continue;
            rows << r;
        }
        PartsTable::sort( rows, sortOpts( c ) );
        QJsonArray out;
        for( const PartRow& r : rows ) out.append( rowJson( r ) );
        return out;
    });

    add("parts set", "parts set <comp>[,<comp>…] mfr|mpn|supplier|spn|notes=TEXT …",
        "Set BOM fields of parts, one undo step (as editing them in the Parts pane on several rows; "
        "comp set <comp> bomMpn=… works too). An empty value clears the field.",
        []( CliCtx& c ) -> QJsonValue
    {
        if( c.pos.isEmpty() ) throw CliError("missing component");
        if( c.sets.isEmpty() ) throw CliError("nothing to set: give mfr=, mpn=, supplier=, spn= or notes=");
        QList<Component*> parts = partsArg( c.pos );
        QMap<QString, QString> vals;
        QStringList keys = PartsTable::bomKeys(), props = PartsTable::bomProps();
        for( QString k : c.sets.keys() )
        {
            int i = keys.indexOf( k.toLower() );
            if( i < 0 ) i = props.indexOf( k );
            if( i < 0 ) throw CliError("unknown BOM field "+k+" ("+keys.join(", ")+")");
            vals[props.at( i )] = c.sets.value( k ).toString();
        }
        int n = PartsTable::setBom( parts, vals );
        QJsonArray rows;
        for( const PartRow& r : PartsTable::rows() )
            for( Component* p : parts ) if( p->getUid() == r.uid ) rows.append( rowJson( r ) );
        return QJsonObject{ {"changed", n}, {"parts", rows} };
    });

    add("parts bom", "parts bom [--out FILE] [--format csv|md] [--per-part] [--all] [--filter graphical,meters,sources]",
        "The BOM of the parts the filters let through (default: graphical, meters, sources left out), identical parts "
        "grouped with their quantity and labels (--per-part: one line per part, with its Tab place). CSV (RFC 4180, "
        "UTF-8 with a BOM in the file) or Markdown (--format, or the file's extension). Without --out: the text.",
        []( CliCtx& c ) -> QJsonValue
    {
        PartsTable::Filter f = filterOpts( c );
        QString out = c.opt("out");
        QString format = c.opt("format");
        if( format.isEmpty() ) format = out.endsWith(".md", Qt::CaseInsensitive ) ? "md" : "csv";
        if( format != "csv" && format != "md" ) throw CliError("--format csv|md");
        QList<PartRow> rows;
        for( const PartRow& r : PartsTable::rows() ) if( PartsTable::passes( r, f ) ) rows << r;
        QString circ = CircuitWidget::self()->curCirc();
        int lines = 0;
        QString text = PartsTable::bom( rows, c.flag("per-part"), format, circ.isEmpty() ? "" : QFileInfo( circ ).completeBaseName(), &lines );
        if( out.isEmpty() ) return text;
        QString path = CliApi::self()->resolvePath( out );
        QString err;
        if( !PartsTable::writeBom( path, text, format, &err ) ) throw CliError("BOM not saved: "+err);
        QString msg = QString("BOM: %1 lines, %2 parts → %3").arg( lines ).arg( rows.size() ).arg( path );
        qInfo().noquote() << msg;
        return QJsonObject{ {"path", path}, {"lines", lines}, {"parts", rows.size()}, {"format", format} };
    });

    add("lock", "lock <comp>[,<comp>…]",
        "Lock parts (specs/parts-pane.md §10), as ⇧⌘K: the mouse passes through them, rubber bands, Tab, Space and "
        "⌘A leave them out, they can't be moved, rotated, flipped or deleted until unlocked. One undo step. Saved in the .sim1.",
        []( CliCtx& c ) -> QJsonValue { return lockCmd( c, true ); });

    add("unlock", "unlock <comp>[,<comp>…] | unlock --all",
        "Unlock parts (⌥⇧⌘K, the 🔒 in the Parts pane). One undo step.",
        []( CliCtx& c ) -> QJsonValue { return lockCmd( c, false ); });

    add("tab order", "tab order [list|selected|front|back|set N|reset] [comp[,comp…]]",
        "The canvas Tab walk (specs/parts-pane.md §12). list (default): the walk, each part with its place and "
        "whether it has its own index. selected: the parts follow each other in the order given (or, without parts, "
        "the order they were selected on the canvas, as ⇧⌘T), where the first of them was. front / back: to the "
        "front or back of the walk (⌥⌘T / ⇧⌥⌘T; without parts: the selection). set N: they start at place N, the "
        "others shift (as typing in the Parts pane's Tab column). reset: by position again. Each is one undo step; "
        "locked and hidden parts are not in the walk.",
        []( CliCtx& c ) -> QJsonValue
    {
        QString what = c.argOr( 0, "list").toLower();
        int from = 1;
        int place = 0;
        if( what == "set" ){
            bool ok = false;
            place = c.arg( 1, "N").toInt( &ok );
            if( !ok || place < 1 ) throw CliError("tab order set N <comp>…: N is a place from 1");
            from = 2;
        }
        QList<Component*> parts = partsArg( c.pos, from );
        Circuit* circ = Circuit::self();
        QString msg;
        if     ( what == "list" ) {}
        else if( what == "selected" ) msg = TabOrder::asSelected( parts.isEmpty() ? circ->selectionOrder() : parts );
        else if( what == "front" ) msg = TabOrder::toFront( parts.isEmpty() ? circ->selectedParts() : parts );
        else if( what == "back" )  msg = TabOrder::toBack( parts.isEmpty() ? circ->selectedParts() : parts );
        else if( what == "set" ){
            if( parts.isEmpty() ) throw CliError("tab order set N <comp>[,<comp>…]");
            msg = TabOrder::moveTo( parts, place );
        }
        else if( what == "reset" ) msg = TabOrder::reset();
        else throw CliError("tab order [list|selected|front|back|set N|reset] [comp…]");
        QJsonArray walk;
        QList<Component*> w = TabOrder::walk();
        for( int i=0; i<w.size(); ++i )
            walk.append( QJsonObject{ {"place", i+1}, {"label", w.at( i )->idLabel()}, {"uid", w.at( i )->getUid()},
                                      {"index", w.at( i )->tabIndex()} } );
        QJsonObject r{ {"walk", walk}, {"custom", TabOrder::custom()} };
        if( !msg.isEmpty() ) r["message"] = msg;
        QJsonArray order;
        for( Component* p : circ->selectionOrder() ) order.append( p->idLabel() );
        r["selectionOrder"] = order;
        return r;
    });

    add("view parts", "view parts [--search TEXT] [--show COMP] [--filter default|all|graphical,meters,sources,locked]",
        "Show the Parts tab (shared GUI session). --search: type in its search box; --show: that part's row "
        "selected (filters opened when it is filtered out); --filter: set its toggles (default: only Locked on; "
        "a list: those on, the others off; remembered as the user's).",
        []( CliCtx& c ) -> QJsonValue
    {
        PartsPane* pane = PartsPane::self();
        if( !pane ) throw CliError("no Parts pane");
        QString uid;
        if( c.has("show") ) uid = CliRefs::comp( c.opt("show") )->getUid();
        CircuitWidget::self()->showParts( uid );
        if( c.has("filter") )
        {
            PartsTable::Filter f;                       // The defaults
            QString v = c.opt("filter").toLower();
            if( v == "all" ) f.all();
            else if( v != "default" )
            {
                f.locked = false;
                for( QString k : v.split(",", QString::SkipEmptyParts ) )
                {
                    k = k.trimmed();
                    if     ( k == "graphical" ) f.graphical = true;
                    else if( k == "meters" )    f.meters = true;
                    else if( k == "sources" )   f.sources = true;
                    else if( k == "locked" )    f.locked = true;
                    else throw CliError("--filter default|all|graphical,meters,sources,locked");
                }
            }
            pane->setFilter( f );
        }
        if( c.has("search") ) pane->setSearch( c.opt("search") );
        pane->syncNow();
        return QJsonObject{ {"shown", pane->shownCount()}, {"count", pane->countText()}, {"selected", QJsonArray::fromStringList( pane->selectedUids() )} };
    });
}
