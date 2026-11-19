/***************************************************************************
 *   Keyboard input: typing the selected part's value (K7,                 *
 *   specs/keyboard-input.md, decisions D8 D10)                            *
 *                                                                         *
 *   Buffer grammar [-]digits[.digits][mult[digits]] (4k7 = 4.7 k).        *
 *   The part's value label shows the buffer while typing; the property    *
 *   changes only on commit (one undo step). The part is kept by uid.      *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef VALUEEDITOR_H
#define VALUEEDITOR_H

#include <QString>
#include <QChar>
#include <QJsonObject>
#include <qnamespace.h>

class QKeyEvent;
class Component;

class ValueEditor
{
    public:
        ValueEditor();

        // The char a digit, - or . , key types ('\0': not one). A digit character on any
        // layout; the number row without Shift also by position (AZERTY: D10, D21)
 static QChar entryChar( QKeyEvent* event, Qt::KeyboardModifiers mods );

        bool active() { return m_active; }
        QString uid() { return m_uid; }

        // Starts editing comp's value with its first char. false: comp takes no typed value
        bool start( Component* comp, QChar first );

        // A key while active
        enum result_t { notMine=0, handled, committed };
        result_t key( QKeyEvent* event, Qt::KeyboardModifiers mods );

        void revert();                        // The label shows what the property holds
        void reset();                         // New circuit: forget, touch nothing

        QJsonObject state();                  // CLI kbd state: part, property, buffer

    private:
        void commit();                        // Set the property (one undo step), then revert
        void addChar( QChar c );              // One buffer char, per the grammar
        void updateLabel();                   // The label: buffer + marker + unit (200k▏Ω)
        QString valueString( Component* comp );   // The buffer as a property value ("200 kΩ")
        Component* part();

        bool    m_active;
        QString m_uid;
        QString m_prop;
        QString m_buf;
        QString m_oldText;                    // The value label before the edit
};

#endif
