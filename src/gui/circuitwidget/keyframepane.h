/***************************************************************************
 *   Key frames pane: the saved key frames of the circuit                   *
 *   (specs/sim-control.md §5.1)                                           *
 ***( see copyright.txt file at root folder )*******************************/

// A tab of the bottom pane, or a split beside the tabs (its ⧉ button). A filter line, then
// the key frames by time (time, description, saved, size). A click goes to one (starting a
// stopped simulation); Shift / ⌘ select several, which play as a range when the simulation is
// started; Backspace or the context menu deletes, after a question.

#ifndef KEYFRAMEPANE_H
#define KEYFRAMEPANE_H

#include <QWidget>

class QLineEdit;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;
class QLabel;
class QAction;

class KeyFramePane : public QWidget
{
    Q_OBJECT
    public:
        KeyFramePane( QWidget* parent, QAction* saveAct );

 static KeyFramePane* self() { return m_pSelf; }

        void setSplit( bool split );          // The button and the title follow where it is
        void editNew( int n );                // A key frame just saved: select it, edit its description
        bool rangeSelection( int* from, int* to );   // Two or more selected: earliest and latest
        QList<int> selected();
        void askDelete();                     // The selected ones, after a question

    signals:
        void splitToggled( bool split );

    protected:
        bool eventFilter( QObject* obj, QEvent* ev ) override;

    private:
        void fill();
        void applyFilter();
        void openItem( QTreeWidgetItem* it );
        void contextMenu( const QPoint& pos );
        void markCurrent( int n );

        QLabel*      m_title;
        class QHBoxLayout* m_topRow;
        class QHBoxLayout* m_filterRow;   // The filter's own line in the split (narrow)
        QLineEdit*   m_filter;
        QToolButton* m_splitBtn;
        QTreeWidget* m_tree;
        QLabel*      m_empty;
        bool m_split = false;
        bool m_filling = false;
        int  m_current = 0;                   // The key frame last gone to (bold)

 static KeyFramePane* m_pSelf;
};

#endif
