/***************************************************************************
 *   Trigger editor: one trigger's watch, condition, action and mode       *
 *   (specs/triggers.html §4, the same form in the Triggers tab's dialog   *
 *   and in a part's properties)                                           *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef TRIGGEREDITOR_H
#define TRIGGEREDITOR_H

#include <QWidget>
#include <QDialog>

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

        void setTrigger( const Trigger& t );
        Trigger trigger();          // The form's trigger (id, hits and the rest kept from setTrigger)
        QString error();            // "" when the form makes a valid trigger

    signals:
        void edited();              // Any change by the user

    private:
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

#endif
