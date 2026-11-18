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
#include "clirefs.h"
#include "component.h"
#include "pin.h"

// The controller's state plus its pins as CLI refs: pinFrom, pinTo, and for one selected
// part its selected pin (selectedPin, selectedPinManual) and its pins clockwise
static QJsonObject kbdState()
{
    KeyboardController* k = KeyboardController::self();
    QJsonObject o = k->state();
    Pin* f = k->pinFrom();
    Pin* t = k->pinTo();
    o["pinFrom"] = f ? QJsonValue( CliRefs::pinRef( f ) ) : QJsonValue();
    o["pinTo"]   = t ? QJsonValue( CliRefs::pinRef( t ) ) : QJsonValue();
    if( Component* c = k->singlePart() )
    {
        Pin* p = c->selectedPin();
        o["selectedPin"] = p ? QJsonValue( CliRefs::pinRef( p ) ) : QJsonValue();
        o["selectedPinManual"] = c->selectedPinManual();
        QJsonArray cw;
        for( Pin* q : c->pinsClockwise() ) cw.append( CliRefs::pinRef( q ) );
        o["pinsClockwise"] = cw;
    }
    return o;
}

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
    // The AZERTY number row without Shift (D10): the physical digit keys with their
    // AZERTY characters, as macOS reports them (key code by position, text by layout).
    // "-" and "_" stay their own keys, which the editor also reads by text
    static const QString azRow = "&é\"'(*è_çà";          // * = the '-' key, never typed
    int az = azRow.indexOf( ch );
    if( az >= 0 )
    {
        key  = ( az == 9 ) ? Qt::Key_0 : Qt::Key_1+az;
        text = ( mods & ( Qt::ControlModifier | Qt::MetaModifier ) ) ? QString() : QString( ch );
        return;
    }
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
        "An uppercase letter implies shift. The AZERTY number row without Shift is typed by its "
        "characters (& é \" ' ( è ç à): each is its physical digit key with that text. "
        "Gives the canvas focus first.",
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
        return kbdState();
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
        return kbdState();
    });

    add("ui click", "ui click <x,y> | ui click <comp> [index]",
        "Left click (press and release, no modifier) at a scene position on the canvas, as the mouse does: "
        "on empty canvas it clears the selection and places the keyboard cursor there. With a component "
        "name instead of x,y: click that part's button or toggle its switch. The view scrolls to show "
        "the point first.",
        []( CliCtx& c ) -> QJsonValue
    {
        if( c.pos.isEmpty() ) throw CliError("missing position x,y or component name");
        if( !c.pos.at( 0 ).contains(',') ) return uiClickControl( c );   // The older form

        QStringList xy = c.arg( 0, "x,y").split(',');
        if( xy.size() != 2 ) throw CliError("position must be x,y");
        QPointF p( Cli::parseNum( xy[0], "x"), Cli::parseNum( xy[1], "y") );
        CircuitView* v = CircuitView::self();
        v->ensureVisible( QRectF( p.x()-1, p.y()-1, 2, 2 ), 40, 40 );
        QPoint vp = v->mapFromScene( p );
        QPoint gp = v->viewport()->mapToGlobal( vp );
        QMouseEvent press( QEvent::MouseButtonPress, vp, gp, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
        QApplication::sendEvent( v->viewport(), &press );
        QMouseEvent release( QEvent::MouseButtonRelease, vp, gp, Qt::LeftButton, Qt::NoButton, Qt::NoModifier );
        QApplication::sendEvent( v->viewport(), &release );
        return kbdState();
    });

    add("ui drag", "ui drag <x1,y1> <x2,y2>",
        "Drag with the left mouse button from one scene position to another, as the mouse does: "
        "press at the start, moves in 8 px steps, release at the end. Moves a selected part (the "
        "hidden keyboard cursor follows). The view scrolls to show both points first.",
        []( CliCtx& c ) -> QJsonValue
    {
        QStringList p1 = c.arg( 0, "x1,y1").split(',');
        QStringList p2 = c.arg( 1, "x2,y2").split(',');
        if( p1.size() != 2 || p2.size() != 2 ) throw CliError("positions must be x,y");
        QPointF a( Cli::parseNum( p1[0], "x1"), Cli::parseNum( p1[1], "y1") );
        QPointF b( Cli::parseNum( p2[0], "x2"), Cli::parseNum( p2[1], "y2") );
        CircuitView* v = CircuitView::self();
        v->ensureVisible( QRectF( a, b ).normalized(), 40, 40 );
        auto send = [&]( QEvent::Type t, QPointF scene, Qt::MouseButton button, Qt::MouseButtons buttons ){
            QPoint vp = v->mapFromScene( scene );
            QMouseEvent ev( t, vp, v->viewport()->mapToGlobal( vp ), button, buttons, Qt::NoModifier );
            QApplication::sendEvent( v->viewport(), &ev );
        };
        send( QEvent::MouseButtonPress, a, Qt::LeftButton, Qt::LeftButton );
        int steps = qMax( 1, int( ( b-a ).manhattanLength()/8 ) );
        for( int i=1; i<=steps; ++i )
            send( QEvent::MouseMove, a+( b-a )*i/steps, Qt::NoButton, Qt::LeftButton );
        send( QEvent::MouseButtonRelease, b, Qt::LeftButton, Qt::NoButton );
        return kbdState();
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
        return kbdState();
    });

    add("kbd state", "kbd state",
        "Keyboard state of the canvas: focus, keyboard cursor (scene x,y on the grid), whether the cross "
        "is shown and the mouse pointer hidden, the selected parts, the pins to wire (pinFrom, pinTo) and, "
        "with one part selected, its selected pin (manual or auto) and its pins clockwise.",
        []( CliCtx& ) -> QJsonValue
    {
        return kbdState();
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
