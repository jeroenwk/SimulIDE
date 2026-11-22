/***************************************************************************
 *   Triggers pane: the circuit's triggers (specs/triggers.html §3)        *
 ***( see copyright.txt file at root folder )*******************************/

// A tab of the bottom pane after Key frames, or a split beside the tabs (its ⧉ button), as the
// Key frames pane. A filter line, + (a new trigger), then one row per trigger: armed box, what it
// watches, condition, action, mode, hits, the last hit's time (a click on it goes there). Shift /
// ⌘ select several (their boxes arm or disarm together, Backspace deletes after a question);
// double-click or Enter edits; Space plays / pauses as elsewhere.

#ifndef TRIGGERPANE_H
#define TRIGGERPANE_H

#include <QWidget>
#include <QPointer>
#include <QSet>

class QLineEdit;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;
class QLabel;
class QHBoxLayout;
class Triggers;
class Pin;

class TriggerPane : public QWidget
{
    Q_OBJECT
    public:
        TriggerPane( QWidget* parent );

 static TriggerPane* self() { return m_pSelf; }

        void setSplit( bool split );
        void follow();                     // The current circuit's triggers (a new circuit, an open)
        QList<int> selected();
        void askDelete();
        void edit( int id );               // The editor dialog
        void addNew();                     // The editor with an empty watch
        void addNode( Pin* onNet );        // The editor watching the node of that pin (a wire's menu, §5)
        void selectId( int id );

    signals:
        void splitToggled( bool split );
        void wantShown();                  // A trigger paused the run: bring the pane in sight

    protected:
        bool eventFilter( QObject* obj, QEvent* ev ) override;

    private:
        void fill();
        void refresh();                    // Hits and armed boxes only (no rebuild)
        void applyFilter();
        void contextMenu( const QPoint& pos );
        void goToLast( int id );
        void highlightParts();
        void armSelected( bool armed );

        QLabel*      m_title;
        QHBoxLayout* m_topRow;
        QHBoxLayout* m_filterRow;
        QLineEdit*   m_filter;
        QToolButton* m_splitBtn;
        QTreeWidget* m_tree;
        QLabel*      m_empty;
        QPointer<Triggers> m_trig;
        bool m_split = false;
        QSet<int> m_folded;                // ANDs folded by the user
        void combineSelected();
        void splitSelected();
        bool m_filling = false;

 static TriggerPane* m_pSelf;
};

#endif
