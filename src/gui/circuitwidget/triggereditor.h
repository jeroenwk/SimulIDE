/***************************************************************************
 *   Trigger editor: one trigger's watch, condition, action and mode       *
 *   (specs/triggers.html §4, the same form in the Triggers tab's dialog   *
 *   and in a part's properties)                                           *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef TRIGGEREDITOR_H
#define TRIGGEREDITOR_H

#include <QWidget>
#include <QDialog>
#include <QPointer>

#include "triggers.h"

class QComboBox;
class QLineEdit;
class QCheckBox;
class QSpinBox;
class QStackedWidget;
class QLabel;
class QRadioButton;
class QWidget;

class TriggerEditor : public QWidget
{
    Q_OBJECT
    public:
        // part: the editor of that part's properties (no part list); null: any part
        TriggerEditor( QWidget* parent, Component* part=nullptr );
        ~TriggerEditor();

        void setTrigger( const Trigger& t );
        Trigger trigger();          // The form's trigger (id, hits and the rest kept from setTrigger)
        QString error();            // "" when the form makes a valid trigger

    signals:
        void edited();              // Any change by the user

    protected:
        void showEvent( QShowEvent* e ) override;
        void hideEvent( QHideEvent* e ) override;
        bool eventFilter( QObject* obj, QEvent* ev ) override;

    private:
        // The watched pins marked on the canvas (PinMarker); a pin list open: the entry under
        // the pointer instead of the chosen one (hoverPin / hoverRef: an index, -1: the chosen)
        void markPins( int hoverPin=-1, int hoverRef=-1 );

        void fillParts();
        void partChanged();
        void kindChanged();
        void updateVisible();
        Component* part();
        void changed();

        Trigger m_t;
        Component* m_fixed;
        bool m_filling = false;

        QComboBox* m_part;
        QRadioButton* m_kState;
        QRadioButton* m_kVolt;
        QRadioButton* m_kCurr;
        QComboBox* m_block;         // State
        QComboBox* m_pin;           // Voltage / current
        QLabel*    m_toLabel;
        QComboBox* m_ref;           // Voltage: ground or another pin of the part
        QComboBox* m_when;          // crosses | leaves | enters  /  changes | any change | reaches
        QStackedWidget* m_cond;
        QComboBox* m_edge;
        QLineEdit* m_level;
        QLineEdit* m_low;
        QLineEdit* m_high;
        QLineEdit* m_hyst;
        QWidget*   m_hystRow;
        QComboBox* m_from;
        QComboBox* m_to;
        QComboBox* m_sev;
        QCheckBox* m_pause;
        QCheckBox* m_keyframe;
        QLineEdit* m_before;
        QComboBox* m_mode;
        QLineEdit* m_holdoff;
        QSpinBox*  m_max;
        QWidget*   m_normalRow;
        QCheckBox* m_armed;
        QLineEdit* m_name;
        QLabel*    m_note;
        QLabel*    m_andNote;          // An AND, or a member of one: what applies
        class QFormLayout* m_form;
        QList<QWidget*> m_watchRows;   // Hidden for an AND
};

class TriggerDialog : public QDialog   // The Triggers tab's editor: OK applies
{
    Q_OBJECT
    public:
        TriggerDialog( QWidget* parent, const Trigger& t, bool isNew );
        TriggerEditor* editor() { return m_ed; }
    private:
        TriggerEditor* m_ed;
};

// The Triggers tab of a part's properties (§4): its triggers (the same rows as the circuit's tab,
// "Watches" without the part's label), + and −, and the editor of the selected one. Changes
// apply at once, as the other property tabs (a short pause after typing: one undo step)
class TriggerTab : public QWidget
{
    Q_OBJECT
    public:
        TriggerTab( QWidget* parent, Component* part );

 static bool wanted( Component* part );   // Parts whose pins or states can be watched

    private:
        void fill();
        void select( int id );
        void selectionChanged();
        void apply();
        void addNew();
        void removeSelected();

        Component*     m_part;
        class QTreeWidget* m_list;
        TriggerEditor* m_ed;
        class QPushButton* m_addBtn;   // A new trigger's form: adds it as it is
        class QTimer*  m_apply;
        QPointer<Triggers> m_trig;
        int  m_id = 0;              // The trigger in the editor; 0: a new one, not added yet
        bool m_draft = false;
        bool m_filling = false;
};

// The scope's Triggers tab (D21, §4.5): its own trigger channel and level (Auto or a voltage),
// edited here as in its Expand panel, and a box that makes it a simulation trigger: a row in
// the circuit's Triggers tab (its action and mode are set there); deleting the row clears the box
class ScopeTriggerTab : public QWidget
{
    Q_OBJECT
    public:
        ScopeTriggerTab( QWidget* parent, Component* scope );

    private:
        void fill();
        void setProp( const QString& prop, const QString& val );   // One undo step

        Component* m_scope;
        QPointer<Triggers> m_trig;
        QComboBox* m_ch;
        QCheckBox* m_auto;
        QLineEdit* m_level;
        QCheckBox* m_sim;
        QLabel*    m_row;
        bool m_filling = false;
};

#endif
