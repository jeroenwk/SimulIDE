/***************************************************************************
 *   Parts table: the circuit's parts as BOM rows (specs/parts-pane.md)    *
 ***( see copyright.txt file at root folder )*******************************/

// The model behind the Parts pane and CLI parts: one row per top-level part (a subcircuit is
// one row), its columns, the BOM sort, search, filters, grouping and the CSV / Markdown export.
// No widgets here: the pane (partspane.*) and the CLI (cli_parts.cpp) share it.

#ifndef PARTSTABLE_H
#define PARTSTABLE_H

#include <QString>
#include <QStringList>
#include <QList>
#include <QSet>
#include <QMap>

class Component;

struct PartRow
{
    Component* comp = nullptr;
    QString uid;
    QString type;          // As in the left pane (Resistor, NPN, 555, MT7833A)
    QString label;
    QString model;
    QString value;         // The value label's property, as on the canvas
    double  valueNum = 0;  // For sorting and the SI search
    bool    hasNum = false;
    QString other;         // Every changed setting: "Tolerance: 5 % · Show State: on"
    QString category;      // Top-level group of the left pane (Passive, Meters, Sources...)
    int  tab = 0;          // 1-based place in Tab's walk; 0: not in it (locked, hidden)
    bool locked = false;
    bool hidden = false;   // An instrument hidden by Show Instruments
    bool graphical = false;
    QString bom[5];        // Manufacturer, MPN, Supplier, Supplier PN, Notes
};

class PartsTable
{
    public:
        enum col_t { colLock=0, colType, colLabel, colModel, colValue, colOther, colTab, colCategory,
                     colMfr, colMpn, colSupplier, colSpn, colNotes, colCount };

 static QStringList colKeys();     // lock type label model value other tab category mfr mpn supplier spn notes
 static QStringList colTitles();   // As the pane's header shows them
 static QStringList bomProps();    // bomMfr bomMpn bomSupplier bomSpn bomNotes
 static QStringList bomKeys();     // mfr mpn supplier spn notes (CLI parts set)
 static bool isBomCol( int col ) { return col >= colMfr && col <= colNotes; }
 static QString text( const PartRow& r, int col );   // A cell as shown

        struct Filter {
            bool graphical = false;            // Off by default (spec §5)
            bool meters    = false;
            bool sources   = false;
            bool locked    = true;
            QSet<QString> offCats;             // Other categories turned off (More…)
            void all() { graphical = meters = sources = locked = true; offCats.clear(); }
        };
        struct SortKey { int col; bool desc; };

 static QList<PartRow> rows();                    // Every top-level part, unsorted
 static bool passes( const PartRow& r, const Filter& f );
 static bool matches( const PartRow& r, const QString& search );   // Any column; SI values: 10k = 10 kΩ = 10000
 static void sort( QList<PartRow>& rows, const QList<SortKey>& keys );   // Empty: BOM order (by label)
 static int  compare( const PartRow& a, const PartRow& b, int col );
 static int  labelCompare( const QString& a, const QString& b );   // C1 < C2 < C10 < D1; R < Rx; "" last

        // The BOM: grouped (one line per identical part, with Qty and Labels) or per part,
        // format "csv" (RFC 4180, CRLF) or "md". lines: the lines written (groups or parts)
 static QString bom( QList<PartRow> rows, bool perPart, QString format, QString title, int* lines=nullptr );
 static bool writeBom( QString path, QString text, QString format, QString* err );   // CSV: UTF-8 with BOM

        // Set BOM fields (prop bomMpn... -> value) of parts: one undo step. Returns the parts changed
 static int setBom( QList<Component*> parts, QMap<QString, QString> values );

 static QStringList categories();                 // Top-level groups of the left pane, in its order
 static double parseSi( QString s, bool* ok );    // "10k", "10 kΩ", "4k7", "100n" -> number

    private:
 static void lookup( Component* c, QString* caption, QString* category );
 static QString shownValue( class ComProperty* p );
 static QString otherValues( Component* c, QSet<QString> skip );
};

#endif
