/***************************************************************************
 *   Parts pane: the circuit's parts, BOM fields and export                *
 *   (specs/parts-pane.md)                                                  *
 ***( see copyright.txt file at root folder )*******************************/

// A tab of the bottom pane after Triggers. A search box, filter toggles (Graphical, Meters,
// Sources, Locked, More…), Export BOM…; one row per top-level part (PartsTable), sortable by
// any column (BOM order by default). Selection follows the canvas both ways; BOM cells are
// edited in place (several selected rows at once, one undo step); a click on the 🔒 column
// locks or unlocks.

#ifndef PARTSPANE_H
#define PARTSPANE_H

#include <QWidget>
#include <QPointer>
#include <QTimer>

#include "partstable.h"

class QLineEdit;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;
class QLabel;
class QCheckBox;
class Circuit;

class PartsPane : public QWidget
{
    Q_OBJECT
    public:
        PartsPane( QWidget* parent );

 static PartsPane* self() { return m_pSelf; }

        void follow();                        // The current circuit (new, opened)
        void syncNow();                       // Rebuild if stale and take the canvas selection (CLI)
        QStringList selectedUids();           // Rows selected in the table
        int shownCount() { return m_shown; }
        void setSearch( QString text );
        void reveal( QString uid );           // Its row selected and in sight (filters opened if needed)
        PartsTable::Filter filter() { return m_filter; }
        void setFilter( PartsTable::Filter f );   // The toggles (CLI view parts --filter)
        QString countText();                  // The line under the table

        void editBom( QString uid, int col, QString text );   // From the cell editor
        void editTab( QString uid, int place );               // Tab cell typed (§12)
        void toggleLock( QTreeWidgetItem* it );
        void exportBom();

    protected:
        bool eventFilter( QObject* obj, QEvent* ev ) override;
        void showEvent( QShowEvent* e ) override;

    private:
        void fill();
        bool editing();
        void poll();                          // Timer: rebuild when the circuit changed
        QString signature();
        void fromCanvas();                    // Canvas selection -> rows
        void toCanvas();                      // Rows -> canvas selection
        void contextMenu( const QPoint& pos );
        void headerMenu( const QPoint& pos );
        void headerClicked( int col );
        void updtSortIndicator();
        void updtCount();
        void saveSettings();
        QList<Component*> compsOf( QStringList uids );

        QLineEdit*   m_search;
        QToolButton* m_graphBtn;
        QToolButton* m_metersBtn;
        QToolButton* m_sourcesBtn;
        QToolButton* m_lockedBtn;
        QToolButton* m_moreBtn;
        QCheckBox*   m_perPart;
        QTreeWidget* m_tree;
        QLabel*      m_count;
        QTimer       m_timer;

        PartsTable::Filter m_filter;
        QList<PartRow> m_rows;                // All rows of the last rebuild (filtered or not)
        QString m_sig;
        int  m_shown = 0;
        bool m_filling = false;
        bool m_syncing = false;
        bool m_fromCanvasQueued = false;
        QPointer<Circuit> m_circ;
        QMetaObject::Connection m_selConn;

 static QList<PartsTable::SortKey> m_sort;   // Kept for the session
 static PartsPane* m_pSelf;
};

#endif
