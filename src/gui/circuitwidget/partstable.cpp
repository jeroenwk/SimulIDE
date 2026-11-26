/***************************************************************************
 *   Parts table: the circuit's parts as BOM rows (specs/parts-pane.md)    *
 ***( see copyright.txt file at root folder )*******************************/

#include <QHash>
#include <QFile>
#include <QDate>
#include <QDebug>
#include <QTreeWidgetItem>
#include <QRegularExpression>
#include <QCoreApplication>
#include <functional>

#include "partstable.h"
#include "circuit.h"
#include "component.h"
#include "comproperty.h"
#include "componentselector.h"
#include "keyboardcontroller.h"
#include "taborder.h"
#include "simulator.h"
#include "utils.h"

#define tr(str) QCoreApplication::translate("PartsTable",str)

QStringList PartsTable::colKeys()
{
    return { "lock", "type", "label", "model", "value", "other", "tab", "category",
             "mfr", "mpn", "supplier", "spn", "notes" };
}

QStringList PartsTable::colTitles()
{
    return { QString::fromUtf8("🔒"), tr("Type"), tr("Label"), tr("Model"), tr("Value"), tr("Other values"),
             tr("Tab"), tr("Category"), tr("Manufacturer"), tr("MPN"), tr("Supplier"), tr("Supplier PN"), tr("Notes") };
}

QStringList PartsTable::bomProps() { return { "bomMfr", "bomMpn", "bomSupplier", "bomSpn", "bomNotes" }; }
QStringList PartsTable::bomKeys()  { return { "mfr", "mpn", "supplier", "spn", "notes" }; }

QString PartsTable::text( const PartRow& r, int col )
{
    switch( col ){
        case colLock:     return r.locked ? QString::fromUtf8("🔒") : QString();
        case colType:     return r.type;
        case colLabel:    return r.label;
        case colModel:    return r.model;
        case colValue:    return r.value;
        case colOther:    return r.other;
        case colTab:      return r.tab ? QString::number( r.tab ) : QString::fromUtf8("—");
        case colCategory: return r.category;
        default: break;
    }
    if( isBomCol( col ) ) return r.bom[col-colMfr];
    return "";
}

// ----- Left pane: caption and top-level group of each part ---------------------

namespace {
struct LibInfo { QString caption, category; };
QHash<QString, LibInfo>& libCache() { static QHash<QString, LibInfo> h; return h; }
QStringList& catOrder() { static QStringList l; return l; }

void buildLib()
{
    QHash<QString, LibInfo>& h = libCache();
    ComponentSelector* sel = ComponentSelector::self();
    if( !sel || !h.isEmpty() ) return;
    std::function<void(QTreeWidgetItem*, QString)> walk = [&]( QTreeWidgetItem* item, QString top )
    {
        QString type = item->data( 0, Qt::UserRole ).toString();
        QString name = item->data( 0, Qt::WhatsThisRole ).toString();
        if( !type.isEmpty() )
        {
            QString cap = item->text( 0 ).trimmed();
            int sp = cap.indexOf("   ");                   // "T_NOT   ternary logic NOT": the name only
            if( sp > 0 ) cap = cap.left( sp );
            LibInfo li{ cap, top };
            if( !h.contains( type+"|"+name ) ) h[type+"|"+name] = li;
            if( !h.contains( name ) ) h[name] = li;
        }
        else for( int i=0; i<item->childCount(); ++i ) walk( item->child( i ), top.isEmpty() ? name : top );
    };
    for( int i=0; i<sel->topLevelItemCount(); ++i )
    {
        QTreeWidgetItem* t = sel->topLevelItem( i );
        QString name = t->data( 0, Qt::WhatsThisRole ).toString();
        if( !catOrder().contains( name ) ) catOrder().append( name );
        walk( t, "" );
    }
}
}

QStringList PartsTable::categories() { buildLib(); return catOrder(); }

void PartsTable::lookup( Component* c, QString* caption, QString* category )
{
    buildLib();
    QString type = c->itemType();
    QString name = type;
    if( type == "Subcircuit" || type == "MCU" ) name = c->getUid().section('-', 0, -2 );   // "MT7833A-5"
    QHash<QString, LibInfo>& h = libCache();
    LibInfo li;
    if     ( h.contains( type+"|"+name ) ) li = h.value( type+"|"+name );
    else if( h.contains( name ) )          li = h.value( name );
    else li = { name, c->isGraphical() ? "Graphical" : "Other" };
    *caption  = li.caption;
    *category = li.category;
}

// ----- Values --------------------------------------------------------------------

QString PartsTable::shownValue( ComProperty* p )
{
    QString t = p->type();
    QString v = p->getValStr();
    if( t == "bool" ) return v == "true" ? "on" : "off";
    if( t == "enum" )                                       // "ids;Names": the shown name
    {
        QStringList l = p->unit().split(";");
        QStringList ids = l.first().split(",");
        if( l.size() > 1 ){
            QStringList names = l.at( 1 ).split(",");
            int i = ids.indexOf( v );
            if( i >= 0 && i < names.size() ) return names.at( i ).trimmed();
        }
        return v;
    }
    return v.simplified();
}

namespace {
// Defaults of a freshly placed part of each native type (a reference instance, built once)
QHash<QString, QHash<QString, QString>>& defaults() { static QHash<QString, QHash<QString, QString>> h; return h; }

bool visibleProp( const propGroup& g, ComProperty* p )
{
    if( g.flags & groupHidden ) return false;
    if( p->flags() & propHidden ) return false;
    if( p->name().isEmpty() ) return false;                 // A label or separator
    QString t = p->type();
    return t == "double" || t == "uint" || t == "int" || t == "string" || t == "enum" || t == "bool"
        || t.startsWith("color") || t == "path" || t == "textEdit";
}

const QHash<QString, QString>* defaultsOf( Component* c )
{
    QString type = c->itemType();
    if( type == "Subcircuit" || type == "MCU" ) return nullptr;   // Not built: heavy, global side effects
    QString model = c->property("Model") ? c->getPropStr("Model") : QString();
    QString key = type+"|"+model;                    // A model sets parameters: compared with a fresh part of that model
    QHash<QString, QHash<QString, QString>>& d = defaults();
    if( d.contains( key ) ) return &d[key];
    Circuit* circ = Circuit::self();
    if( !circ || circ->loading() || circ->isBusy() ) return nullptr;

    static int n = 900000;
    Simulator* sim = Simulator::self();
    bool hold = sim && sim->isRunning();
    if( hold ) sim->holdWorker();
    QHash<QString, QString> vals;
    for( int k=0; k<2; ++k )        // Two: a property that differs between fresh parts (a random ROM) is left out
    {
        Component* ref = circ->createItem( type, type+"-"+QString::number( ++n ), false );
        if( !ref ) break;
        if( !model.isEmpty() && ref->property("Model") ) ref->setPropStr("Model", model );
        for( const propGroup& g : *ref->properties() )
            for( ComProperty* p : g.propList )
            {
                if( !visibleProp( g, p ) ) continue;
                QString v = p->getValStr();
                if( k == 0 ) vals[p->name()] = v;
                else if( vals.contains( p->name() ) && vals.value( p->name() ) != v ) vals[p->name()] = QString( QChar( 0x1f ) );   // Unstable
            }
        delete ref;
    }
    if( hold ) sim->resumeWorker();
    d[key] = vals;
    qDebug() << "[parts] defaults of" << key << ":" << vals.size() << "properties";
    return &d[key];
}
}

QString PartsTable::otherValues( Component* c, QSet<QString> skip )
{
    const QHash<QString, QString>* def = defaultsOf( c );
    if( !def ) return "";
    QStringList out;
    for( const propGroup& g : *c->properties() )
        for( ComProperty* p : g.propList )
        {
            if( !visibleProp( g, p ) || skip.contains( p->name() ) ) continue;
            QString v = p->getValStr();
            if( def->value( p->name() ) == QString( QChar( 0x1f ) ) ) continue;   // Differs in every fresh part
            if( def->contains( p->name() ) && def->value( p->name() ) == v ) continue;
            if( !def->contains( p->name() ) && v.isEmpty() ) continue;
            QString cap = p->capt().isEmpty() ? p->name() : p->capt();
            QString shown = shownValue( p );
            if( shown.size() > 60 ) shown = shown.left( 57 )+"…";
            out << cap+": "+shown;
        }
    return out.join( QString::fromUtf8(" · ") );
}

double PartsTable::parseSi( QString s, bool* ok )
{
    *ok = false;
    s = s.trimmed();
    static const QRegularExpression re( QString::fromUtf8("^([-+]?\\d*\\.?\\d+(?:[eE][-+]?\\d+)?)\\s*([pnuµmkKMG]?)(\\d*)\\s*([A-Za-zΩ°%]*)$") );
    QRegularExpressionMatch m = re.match( s );
    if( !m.hasMatch() ) return 0;
    QString mant = m.captured( 1 );
    QString mult = m.captured( 2 );
    QString frac = m.captured( 3 );
    if( !frac.isEmpty() ){                                   // 4k7
        if( mult.isEmpty() || mant.contains('.') ) return 0;
        mant += "."+frac;
    }
    double v = mant.toDouble( ok );
    if( !*ok ) return 0;
    double f = 1;
    if     ( mult == "p" ) f = 1e-12;
    else if( mult == "n" ) f = 1e-9;
    else if( mult == "u" || mult == QString::fromUtf8("µ") ) f = 1e-6;
    else if( mult == "m" ) f = 1e-3;
    else if( mult == "k" || mult == "K" ) f = 1e3;
    else if( mult == "M" ) f = 1e6;
    else if( mult == "G" ) f = 1e9;
    return v*f;
}

// ----- Rows ----------------------------------------------------------------------

QList<PartRow> PartsTable::rows()
{
    QList<PartRow> out;
    Circuit* circ = Circuit::self();
    if( !circ ) return out;
    QList<Component*> order = TabOrder::walk();          // The canvas Tab walk (§12)
    QHash<Component*, int> tabOf;
    for( int i=0; i<order.size(); ++i ) tabOf[order.at( i )] = i+1;

    for( Component* c : *circ->compList() )
    {
        if( c->itemType() == "Node" || c->parentItem() ) continue;
        PartRow r;
        r.comp  = c;
        r.uid   = c->getUid();
        r.label = c->idLabel();
        lookup( c, &r.type, &r.category );
        r.locked    = c->isLocked();
        r.hidden    = !c->isVisible();
        r.graphical = c->isGraphical();
        r.tab = tabOf.value( c, 0 );

        QSet<QString> skip = { "label", "Model" };
        for( QString b : bomProps() ) skip.insert( b );
        if( ComProperty* mp = c->property("Model") ) r.model = shownValue( mp );

        QString vp = c->shownProperty();
        if( ComProperty* p = vp.isEmpty() ? nullptr : c->property( vp ) )
        {
            skip.insert( vp );
            r.value = shownValue( p );
            QString t = p->type();
            if( t == "double" || t == "int" || t == "uint" ){ r.valueNum = p->getValue(); r.hasNum = true; }
            else{
                bool ok; double v = parseSi( r.value, &ok );
                if( ok ){ r.valueNum = v; r.hasNum = true; }
            }
            if( vp == "Model" ) r.model.clear();             // Shown as the value: once
        }
        r.other = otherValues( c, skip );
        for( int i=0; i<5; ++i ) r.bom[i] = c->getPropStr( bomProps().at( i ) );
        out.append( r );
    }
    return out;
}

bool PartsTable::passes( const PartRow& r, const Filter& f )
{
    if( r.locked && !f.locked ) return false;
    if( r.category == "Meters"  ) return f.meters;        // Before isGraphical(): a scope is "graphical" too
    QString t = r.comp ? r.comp->itemType() : QString();
    if( t == "Tunnel" || t == "Bus" ) return f.meters;    // Not bought parts: with the meters (user, 2026-11-26)
    if( r.category == "Sources" ) return f.sources;
    if( r.category == "Graphical" ) return f.graphical;
    return !f.offCats.contains( r.category );
}

bool PartsTable::matches( const PartRow& r, const QString& search )
{
    QString s = search.trimmed();
    if( s.isEmpty() ) return true;
    for( int col=colType; col<colCount; ++col )
        if( text( r, col ).contains( s, Qt::CaseInsensitive ) ) return true;
    bool ok;
    double v = parseSi( s, &ok );
    if( ok && r.hasNum && qAbs( r.valueNum-v ) <= 1e-9*qMax( qAbs( v ), 1e-30 ) ) return true;
    return false;
}

// ----- Sorting -------------------------------------------------------------------

namespace {
struct LabelParts { QString prefix; bool hasNum; qulonglong num; QString rest; };
LabelParts splitLabel( const QString& l )
{
    LabelParts p{ "", false, 0, "" };
    int i = 0;
    while( i < l.size() && !l.at( i ).isDigit() ) i++;
    p.prefix = l.left( i );
    int j = i;
    while( j < l.size() && l.at( j ).isDigit() ) j++;
    if( j > i ){ p.hasNum = true; p.num = l.mid( i, j-i ).toULongLong(); }
    p.rest = l.mid( j );
    return p;
}
}

int PartsTable::labelCompare( const QString& a, const QString& b )
{
    if( a.isEmpty() || b.isEmpty() ) return a.isEmpty() == b.isEmpty() ? 0 : ( a.isEmpty() ? 1 : -1 );
    LabelParts pa = splitLabel( a ), pb = splitLabel( b );
    int c = QString::compare( pa.prefix, pb.prefix, Qt::CaseInsensitive );
    if( c ) return c < 0 ? -1 : 1;
    if( pa.hasNum != pb.hasNum ) return pa.hasNum ? -1 : 1;  // R1 before R
    if( pa.num != pb.num ) return pa.num < pb.num ? -1 : 1;
    if( pa.rest != pb.rest ) return labelCompare( pa.rest, pb.rest );
    return QString::compare( a, b );
}

int PartsTable::compare( const PartRow& a, const PartRow& b, int col )
{
    switch( col ){
        case colLock:  return a.locked == b.locked ? 0 : ( a.locked ? -1 : 1 );
        case colLabel: return labelCompare( a.label, b.label );
        case colTab:
            if( a.tab == b.tab ) return 0;
            if( !a.tab || !b.tab ) return a.tab ? -1 : 1;     // Not in the walk: last
            return a.tab < b.tab ? -1 : 1;
        case colValue:
            if( a.hasNum && b.hasNum ){
                if( a.valueNum == b.valueNum ) return 0;
                return a.valueNum < b.valueNum ? -1 : 1;
            }
            if( a.hasNum != b.hasNum ) return a.hasNum ? -1 : 1;
            break;
        default: break;
    }
    QString ta = text( a, col ), tb = text( b, col );
    if( ta.isEmpty() != tb.isEmpty() ) return ta.isEmpty() ? 1 : -1;   // Empty cells last
    int c = QString::localeAwareCompare( ta.toLower(), tb.toLower() );
    return c < 0 ? -1 : ( c > 0 ? 1 : 0 );
}

void PartsTable::sort( QList<PartRow>& rows, const QList<SortKey>& keys )
{
    std::stable_sort( rows.begin(), rows.end(), [&keys]( const PartRow& a, const PartRow& b )
    {
        for( const SortKey& k : keys )
        {
            int c = compare( a, b, k.col );
            if( c ) return k.desc ? c > 0 : c < 0;
        }
        int c = labelCompare( a.label, b.label );            // BOM order, then by type
        if( c ) return c < 0;
        return compare( a, b, colType ) < 0;
    });
}

// ----- BOM export ----------------------------------------------------------------

namespace {
QString csvField( QString s )
{
    if( s.contains(',') || s.contains('"') || s.contains('\n') || s.contains('\r')
     || s.startsWith(' ') || s.endsWith(' ') )
        return "\""+s.replace("\"", "\"\"")+"\"";
    return s;
}
QString mdField( QString s ) { return s.replace("|", "\\|").replace("\n", " "); }
}

QString PartsTable::bom( QList<PartRow> rows, bool perPart, QString format, QString title, int* lines )
{
    sort( rows, {} );
    QStringList header;
    QList<QStringList> table;
    if( perPart )
    {
        header = QStringList{ tr("Label"), tr("Type"), tr("Model"), tr("Value"), tr("Other values"), tr("Tab") };
        for( const PartRow& r : rows )
        {
            QStringList l{ r.label, r.type, r.model, r.value, r.other, r.tab ? QString::number( r.tab ) : "" };
            for( int i=0; i<5; ++i ) l << r.bom[i];
            table << l;
        }
    }else{
        header = QStringList{ tr("Qty"), tr("Labels"), tr("Type"), tr("Model"), tr("Value"), tr("Other values") };
        QList<QString> keys;                                 // In the order of their first label
        QHash<QString, QList<PartRow>> groups;
        for( const PartRow& r : rows )
        {
            QStringList k{ r.type, r.model, r.value, r.other };
            for( int i=0; i<5; ++i ) k << r.bom[i];
            QString key = k.join( QChar( 0x1f ) );
            if( !groups.contains( key ) ) keys << key;
            groups[key] << r;
        }
        for( const QString& key : keys )
        {
            const QList<PartRow>& g = groups.value( key );
            QStringList labels;
            for( const PartRow& r : g ) if( !r.label.isEmpty() ) labels << r.label;
            const PartRow& f = g.first();
            QStringList l{ QString::number( g.size() ), labels.join(", "), f.type, f.model, f.value, f.other };
            for( int i=0; i<5; ++i ) l << f.bom[i];
            table << l;
        }
    }
    header << tr("Manufacturer") << tr("MPN") << tr("Supplier") << tr("Supplier PN") << tr("Notes");
    if( lines ) *lines = table.size();

    QString out;
    if( format == "md" )
    {
        int parts = rows.size();
        out += "# "+tr("BOM")+( title.isEmpty() ? "" : ": "+title )+"\n\n";
        out += QDate::currentDate().toString( Qt::ISODate )+QString(" · %1 lines, %2 parts\n\n").arg( table.size() ).arg( parts );
        QStringList h, sep;
        for( const QString& c : header ){ h << mdField( c ); sep << ( c == tr("Qty") || c == tr("Tab") ? "---:" : "---" ); }
        out += "| "+h.join(" | ")+" |\n| "+sep.join(" | ")+" |\n";
        for( const QStringList& l : table )
        {
            QStringList f;
            for( const QString& c : l ) f << mdField( c );
            out += "| "+f.join(" | ")+" |\n";
        }
    }else{
        QStringList h;
        for( const QString& c : header ) h << csvField( c );
        out += h.join(",")+"\r\n";
        for( const QStringList& l : table )
        {
            QStringList f;
            for( const QString& c : l ) f << csvField( c );
            out += f.join(",")+"\r\n";
        }
    }
    return out;
}

bool PartsTable::writeBom( QString path, QString text, QString format, QString* err )
{
    QFile f( path );
    if( !f.open( QIODevice::WriteOnly | QIODevice::Truncate ) ){ *err = f.errorString(); return false; }
    QByteArray data = text.toUtf8();
    if( format != "md" ) data.prepend( "\xEF\xBB\xBF" );    // So Excel reads Ω and µ
    f.write( data );
    f.close();
    return true;
}

int PartsTable::setBom( QList<Component*> parts, QMap<QString, QString> values )
{
    Circuit* circ = Circuit::self();
    QList<Component*> todo;
    for( Component* c : parts )
        for( QString k : values.keys() )
            if( c->getPropStr( k ) != values.value( k ) ){ if( !todo.contains( c ) ) todo << c; }
    if( todo.isEmpty() ) return 0;
    circ->beginUndoStep();
    QList<QPair<QString, QPair<QString, QString>>> old;   // uid, prop, old value
    for( Component* c : todo )
        for( QString k : values.keys() )
        {
            old.append( { c->getUid(), { k, c->getPropStr( k ) } } );
            c->setPropStr( k, values.value( k ) );
        }
    circ->calcCircuitChanges();
    for( auto& o : old ) circ->addCompChange( o.first, o.second.first, o.second.second );
    circ->endCircuitBatch();
    circ->setChanged();
    return todo.size();
}
