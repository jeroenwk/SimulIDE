/***************************************************************************
 *   SimulIDE command line control interface                              *
 *   Keyboard input on the canvas (specs/keyboard-input.md): real key      *
 *   events, mouse moves and focus for tests, and the keyboard state       *
 ***( see copyright.txt file at root folder )*******************************/

#include <QJsonArray>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QApplication>
#include <QScrollBar>

#include "cliapi.h"
#include "circuitview.h"
#include "circuit.h"
#include "keyboardcontroller.h"
#include "shortcuts.h"

// One key token: [mod+...]key. Mods: shift, cmd (= ctrl, Qt's ControlModifier, the Command
// key on macOS), alt (opt), meta. Key: a single character, or a name (Left, Tab, Enter, ...)
static void parseKey( QString tok, int& key, Qt::KeyboardModifiers& mods, QString& text )
{
    mods = Qt::NoModifier;
    while( tok.size() > 1 && tok.contains('+') )
    {
        int i = tok.indexOf('+');
        if( i == 0 ) break;                       // "+" itself, or "++"
        QString m = tok.left( i ).toLower();
        if     ( m == "shift" )                 mods |= Qt::ShiftModifier;
        else if( m == "cmd" || m == "ctrl" )    mods |= Qt::ControlModifier;
        else if( m == "alt" || m == "opt" )     mods |= Qt::AltModifier;
        else if( m == "meta" )                  mods |= Qt::MetaModifier;
        else throw CliError("unknown modifier: "+m+" (shift, cmd, ctrl, alt, meta)");
        tok = tok.mid( i+1 );
    }
    static const QHash<QString, QPair<int, QString>> named = {
        {"left",   { Qt::Key_Left, "" }},    {"right", { Qt::Key_Right, "" }},
        {"up",     { Qt::Key_Up, "" }},      {"down",  { Qt::Key_Down, "" }},
        {"tab",    { Qt::Key_Tab, "\t" }},   {"backtab", { Qt::Key_Backtab, "" }},
        {"enter",  { Qt::Key_Return, "\r" }},{"return", { Qt::Key_Return, "\r" }},
        {"esc",    { Qt::Key_Escape, "\x1b" }}, {"escape", { Qt::Key_Escape, "\x1b" }},
        {"backspace", { Qt::Key_Backspace, "\b" }}, {"delete", { Qt::Key_Delete, "\x7f" }},
        {"space",  { Qt::Key_Space, " " }},  {"home", { Qt::Key_Home, "" }}, {"end", { Qt::Key_End, "" }},
    };
    if( tok.size() > 1 )
    {
        QString n = tok.toLower();
        if( !named.contains( n ) ) throw CliError("unknown key: "+tok+" (a character, or Left Right Up Down Tab Backtab Enter Esc Backspace Delete Space Home End)");
        key  = named.value( n ).first;
        text = named.value( n ).second;
        if( key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Up || key == Qt::Key_Down )
            mods |= Qt::KeypadModifier;                     // As macOS sends arrows
        if( key == Qt::Key_Tab && ( mods & Qt::ShiftModifier ) ) key = Qt::Key_Backtab;
        return;
    }
    if( tok.isEmpty() ) throw CliError("empty key");
    QChar ch = tok.at( 0 );
    if( ch.isLetter() && ch.isUpper() ) mods |= Qt::ShiftModifier;
    key  = ch.toUpper().unicode();                          // Qt key codes = Latin-1 upper case
    text = ( mods & ( Qt::ControlModifier | Qt::MetaModifier ) ) ? QString() : QString( ch );
}

void CliApi::registerKbd()
{
    add("ui type", "ui type <key> [key ...]",
        "Type keys on the canvas as real key events (press + release each), as the keyboard does. "
        "A key is a character (r, 2, <, _, é) or a name (Left Right Up Down Tab Enter Esc Backspace "
        "Delete Space Home End), with modifiers joined by +: shift+Left, cmd+z, cmd+shift+<. "
        "An uppercase letter implies shift. Gives the canvas focus first.",
        []( CliCtx& c ) -> QJsonValue
    {
        if( c.pos.isEmpty() ) throw CliError("missing keys");
        CircuitView* v = CircuitView::self();
        if( !v ) throw CliError("no circuit view");

        struct K { int key; Qt::KeyboardModifiers mods; QString text; };
        QList<K> keys;
        for( QString tok : c.pos ){ K k; parseKey( tok, k.key, k.mods, k.text ); keys.append( k ); }

        v->setFocus( Qt::OtherFocusReason );
        if( CliApi::self()->headless() ) KeyboardController::self()->setFocused( true ); // No active window offscreen
        for( const K& k : keys )
        {
            QKeyEvent press( QEvent::KeyPress, k.key, k.mods, k.text );
            QApplication::sendEvent( v, &press );
            QKeyEvent release( QEvent::KeyRelease, k.key, k.mods, k.text );
            QApplication::sendEvent( v, &release );
        }
        return KeyboardController::self()->state();
    });

    add("ui mouse", "ui mouse <x,y>",
        "Move the mouse over the canvas to a scene position (no button), as a real mouse move. "
        "The view scrolls to show the point first.",
        []( CliCtx& c ) -> QJsonValue
    {
        QStringList xy = c.arg( 0, "x,y").split(',');
        if( xy.size() != 2 ) throw CliError("position must be x,y");
        QPointF p( Cli::parseNum( xy[0], "x"), Cli::parseNum( xy[1], "y") );
        CircuitView* v = CircuitView::self();
        v->ensureVisible( QRectF( p.x()-1, p.y()-1, 2, 2 ), 40, 40 );
        QPoint vp = v->mapFromScene( p );
        QMouseEvent move( QEvent::MouseMove, vp, v->viewport()->mapToGlobal( vp ), Qt::NoButton, Qt::NoButton, Qt::NoModifier );
        QApplication::sendEvent( v->viewport(), &move );
        return KeyboardController::self()->state();
    });

    add("ui focus", "ui focus canvas|none",
        "Give the canvas keyboard focus, or take it away (as clicking elsewhere does).",
        []( CliCtx& c ) -> QJsonValue
    {
        QString w = c.arg( 0, "canvas|none").toLower();
        CircuitView* v = CircuitView::self();
        bool headless = CliApi::self()->headless();
        if( w == "canvas" ){
            v->setFocus( Qt::OtherFocusReason );
            if( headless ) KeyboardController::self()->setFocused( true );
        }else if( w == "none" ){
            v->clearFocus();
            if( headless ) KeyboardController::self()->setFocused( false );
        }
        else throw CliError("focus must be canvas or none");
        return KeyboardController::self()->state();
    });

    add("kbd state", "kbd state",
        "Keyboard state of the canvas: focus, keyboard cursor (scene x,y on the grid), whether the cross "
        "is shown and the mouse pointer hidden, the selected parts.",
        []( CliCtx& ) -> QJsonValue
    {
        return KeyboardController::self()->state();
    });

    add("kbd shortcuts", "kbd shortcuts",
        "Every keyboard shortcut of the circuit editor: category, action, keys, context.",
        []( CliCtx& ) -> QJsonValue
    {
        QJsonArray list;
        for( const Shortcut& s : Shortcuts::all() )
            list.append( QJsonObject{ {"category", s.category}, {"action", s.action},
                                      {"keys", s.keys}, {"context", s.context} } );
        return list;
    });
}
