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
#include "componentselector.h"
#include "shortcuts.h"
#include "clirefs.h"
#include "component.h"
#include "pin.h"
#include "circuitwidget.h"
#include <QToolBar>
#include <QAction>

// The controller's state plus its pins as CLI refs: pinFrom, pinTo, and for one selected
// part its selected pin (selectedPin, selectedPinManual) and its pins clockwise
// Qt's own test hook (QTest uses it): a key event goes through the shortcut map first.
// True when a shortcut took it
QT_BEGIN_NAMESPACE
Q_GUI_EXPORT bool qt_sendShortcutOverrideEvent( QObject* o, ulong timestamp, int k, Qt::KeyboardModifiers mods,
                                                const QString& text, bool autorep, ushort count );
QT_END_NAMESPACE

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
// A layout's printable keys as macOS reports them (D21): the character, the physical key
// (kVK_ code), Shift, and Qt's key code (from the character with Shift, as with Cmd)
struct LayoutKey { quint32 vk; bool shift; int key; };

static const QHash<QChar, LayoutKey>& layoutKeys( QString layout )
{
    static QHash<QString, QHash<QChar, LayoutKey>> layouts;
    if( layouts.isEmpty() )
    {
        const quint32 row[10] = { 0x1D, 0x12, 0x13, 0x14, 0x15, 0x17, 0x16, 0x1A, 0x1C, 0x19 };  // 0 … 9
        QHash<QChar, LayoutKey> us, de, fr;
        for( int d=0; d<10; ++d )
        {
            us[ QChar('0'+d) ] = { row[d], false, Qt::Key_0+d };    // QWERTY, QWERTZ: digits
            de[ QChar('0'+d) ] = { row[d], false, Qt::Key_0+d };    // unshifted
            fr[ QChar('0'+d) ] = { row[d], true,  Qt::Key_0+d };    // AZERTY: with Shift
        }
        // US QWERTY
        us['!'] = { 0x12, true, Qt::Key_Exclam };    us['-'] = { 0x1B, false, Qt::Key_Minus };
        us['_'] = { 0x1B, true, Qt::Key_Underscore };us['='] = { 0x18, false, Qt::Key_Equal };
        us['+'] = { 0x18, true, Qt::Key_Plus };      us['.'] = { 0x2F, false, Qt::Key_Period };
        us['>'] = { 0x2F, true, Qt::Key_Greater };   us[','] = { 0x2B, false, Qt::Key_Comma };
        us['<'] = { 0x2B, true, Qt::Key_Less };      us[';'] = { 0x29, false, Qt::Key_Semicolon };
        us[':'] = { 0x29, true, Qt::Key_Colon };     us['/'] = { 0x2C, false, Qt::Key_Slash };
        // German QWERTZ (Mac)
        de['!'] = { 0x12, true, Qt::Key_Exclam };    de['/'] = { 0x1A, true, Qt::Key_Slash };
        de['('] = { 0x1C, true, Qt::Key_ParenLeft }; de[')'] = { 0x19, true, Qt::Key_ParenRight };
        de['='] = { 0x1D, true, Qt::Key_Equal };     de['+'] = { 0x1E, false, Qt::Key_Plus };
        de['<'] = { 0x32, false, Qt::Key_Less };     de['>'] = { 0x32, true, Qt::Key_Greater };
        de['-'] = { 0x2C, false, Qt::Key_Minus };    de['_'] = { 0x2C, true, Qt::Key_Underscore };
        de['.'] = { 0x2F, false, Qt::Key_Period };   de[':'] = { 0x2F, true, Qt::Key_Colon };
        de[','] = { 0x2B, false, Qt::Key_Comma };    de[';'] = { 0x2B, true, Qt::Key_Semicolon };
        // French AZERTY (Mac): the number row types & é " ' ( § è ! ç à without Shift
        fr['&'] = { 0x12, false, Qt::Key_Ampersand };  fr[QChar(0xE9)] = { 0x13, false, Qt::Key_Eacute };
        fr['"'] = { 0x14, false, Qt::Key_QuoteDbl };   fr['\''] = { 0x15, false, Qt::Key_Apostrophe };
        fr['('] = { 0x17, false, Qt::Key_ParenLeft };  fr[QChar(0xA7)] = { 0x16, false, Qt::Key_section };
        fr[QChar(0xE8)] = { 0x1A, false, Qt::Key_Egrave }; fr['!'] = { 0x1C, false, Qt::Key_Exclam };
        fr[QChar(0xE7)] = { 0x19, false, Qt::Key_Ccedilla }; fr[QChar(0xE0)] = { 0x1D, false, Qt::Key_Agrave };
        fr[')'] = { 0x1B, false, Qt::Key_ParenRight }; fr['-'] = { 0x18, false, Qt::Key_Minus };
        fr['_'] = { 0x18, true, Qt::Key_Underscore };  fr['<'] = { 0x32, false, Qt::Key_Less };
        fr['>'] = { 0x32, true, Qt::Key_Greater };     fr[':'] = { 0x2F, false, Qt::Key_Colon };
        fr['/'] = { 0x2F, true, Qt::Key_Slash };       fr['='] = { 0x2C, false, Qt::Key_Equal };
        fr['+'] = { 0x2C, true, Qt::Key_Plus };        fr[','] = { 0x2E, false, Qt::Key_Comma };
        fr[';'] = { 0x2B, false, Qt::Key_Semicolon };  fr['.'] = { 0x2B, true, Qt::Key_Period };
        layouts["us"] = us; layouts["de"] = de; layouts["fr"] = fr;
    }
    static const QHash<QChar, LayoutKey> none;
    auto it = layouts.constFind( layout );
    return it == layouts.constEnd() ? none : it.value();
}

static void parseKey( QString tok, int& key, Qt::KeyboardModifiers& mods, QString& text,
                      QString layout, quint32& vk )
{
    mods = Qt::NoModifier;
    vk = 0;
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
        {"shift",  { Qt::Key_Shift, "" }},   // Alone: Shift pressed and released
        {"space",  { Qt::Key_Space, " " }},  {"home", { Qt::Key_Home, "" }}, {"end", { Qt::Key_End, "" }},
    };
    if( tok.size() > 1 )
    {
        QString n = tok.toLower();
        bool fOk = false;
        int f = n.startsWith('f') ? n.mid( 1 ).toInt( &fOk ) : 0;
        if( fOk && f >= 1 && f <= 12 ){ key = Qt::Key_F1+f-1; text = ""; return; }  // F1 … F12
        if( !named.contains( n ) ) throw CliError("unknown key: "+tok+" (a character, or Left Right Up Down Tab Backtab Enter Esc Backspace Delete Space Home End Shift F1…F12)");
        key  = named.value( n ).first;
        text = named.value( n ).second;
        if( key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Up || key == Qt::Key_Down )
            mods |= Qt::KeypadModifier;                     // As macOS sends arrows
        if( key == Qt::Key_Tab && ( mods & Qt::ShiftModifier ) ) key = Qt::Key_Backtab;
        return;
    }
    if( tok.isEmpty() ) throw CliError("empty key");
    QChar ch = tok.at( 0 );
    const QHash<QChar, LayoutKey>& lk = layoutKeys( layout );
    if( lk.contains( ch ) )                                 // --layout: as that keyboard sends it
    {
        LayoutKey k = lk.value( ch );
        if( k.shift ) mods |= Qt::ShiftModifier;
        key  = k.key;
        vk   = k.vk;
        text = ( mods & ( Qt::ControlModifier | Qt::MetaModifier ) ) ? QString() : QString( ch );
        return;
    }
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
    add("ui type", "ui type <key> [key ...] [--layout us|de|fr]",
        "Type keys on the canvas as real key events (press + release each), as the keyboard does. "
        "A key is a character (r, 2, <, _, é) or a name (Left Right Up Down Tab Enter Esc Backspace "
        "Delete Space Home End Shift F1…F12; Shift alone: pressed and released, it ends a selection rectangle), with modifiers joined by +: shift+Left, cmd+z, cmd+shift+<. "
        "An uppercase letter implies shift. The AZERTY number row without Shift is typed by its "
        "characters (& é \" ' ( è ç à): each is its physical digit key with that text. "
        "--layout us|de|fr: printable keys as macOS sends them on a US QWERTY, German QWERTZ or "
        "French AZERTY keyboard (Shift where that layout needs it, the physical key code): "
        "e.g. = is Shift+0 with de, digits need Shift with fr. Gives the canvas focus first. A key "
        "bound to a toolbar button (F5, cmd+, ...) triggers it, as a real key does.",
        []( CliCtx& c ) -> QJsonValue
    {
        if( c.pos.isEmpty() ) throw CliError("missing keys");
        CircuitView* v = CircuitView::self();
        if( !v ) throw CliError("no circuit view");

        struct K { int key; Qt::KeyboardModifiers mods; QString text; quint32 vk; };
        QString layout = c.opt("layout");
        if( !layout.isEmpty() && layoutKeys( layout ).isEmpty() ) throw CliError("unknown layout: "+layout+" (us, de, fr)");
        QList<K> keys;
        for( QString tok : c.pos ){ K k; parseKey( tok, k.key, k.mods, k.text, layout, k.vk ); keys.append( k ); }

        v->setFocus( Qt::OtherFocusReason );
        if( CliApi::self()->headless() ) KeyboardController::self()->setFocused( true ); // No active window offscreen
        if( !QApplication::activeWindow() ) QApplication::setActiveWindow( v->window() );   // Window shortcuts need one
        for( const K& k : keys )
        {
            // First as a shortcut, as Qt does with a real key (the toolbar keys: F5, Cmd+, ...)
            if( qt_sendShortcutOverrideEvent( v, 0, k.key, k.mods, k.text, false, 1 ) ) continue;
            QKeyEvent press( QEvent::KeyPress, k.key, k.mods, 0, k.vk, 0, k.text );
            QApplication::sendEvent( v, &press );
            QKeyEvent release( QEvent::KeyRelease, k.key, k.mods, 0, k.vk, 0, k.text );
            QApplication::sendEvent( v, &release );
        }
        return kbdState();
    });

    add("kbd partkey", "kbd partkey <part> [<letter>|none] [--replace]",
        "A part's keyboard shortcut (the letter that places it at the cross), as its properties dialog "
        "sets it: <part> is a component on the canvas or a name in the component list. Without a letter: "
        "show it. A letter another part has is refused unless --replace (that part then has none); "
        "h w k and non-letters are refused. none clears it. Saved in the user's settings, except on "
        "a headless server (in memory only: it shares the user's settings).",
        []( CliCtx& c ) -> QJsonValue
    {
        ComponentSelector* sel = ComponentSelector::self();
        QString ref = c.arg( 0, "part");
        QString name;
        for( Component* comp : *Circuit::self()->compList() )
            if( comp->getUid() == ref || comp->idLabel() == ref ) name = sel->libraryName( comp );
        if( name.isEmpty() && sel->isListed( ref ) ) name = ref;
        if( name.isEmpty() ) throw CliError("no such part on the canvas or in the component list: "+ref );
        if( c.pos.size() > 1 )
        {
            QString key = c.pos.at( 1 ) == "none" ? QString() : c.pos.at( 1 ).toLower();
            QString why = ComponentSelector::keyRefusal( key );
            if( !why.isEmpty() ) throw CliError( why.replace('\n', ' ') );
            QString owner = sel->shortcutOwner( key, name );
            if( !owner.isEmpty() && !c.flag("replace") )
                throw CliError("\""+key+"\" is already the shortcut of "+sel->caption( owner )+" ("+owner+"): add --replace");
            if( !owner.isEmpty() ) CliApi::self()->addMessage( sel->caption( owner )+" no longer has \""+key+"\"" );
            sel->setShortcut( name, key, !CliApi::self()->headless() );   // Headless: not saved
        }
        return QJsonObject{ {"name", name}, {"caption", sel->caption( name )},
                            {"key", sel->getShortCuts().value( name )},
                            {"default", ComponentSelector::defaultShortCuts().value( name )} };
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

    add("ui toolbar", "ui toolbar",
        "The circuit toolbar's buttons: text, tooltip (with the key, as this platform writes it: "
        "⌘, on macOS, Ctrl+, elsewhere), the key bound to it (native text, empty when the canvas "
        "reads it instead), enabled, and checked for the toggles.",
        []( CliCtx& ) -> QJsonValue
    {
        QJsonArray out;
        CircuitWidget* w = CircuitWidget::self();
        if( !w ) throw CliError("no circuit widget");
        for( QToolBar* tb : w->findChildren<QToolBar*>() )
            for( QAction* a : tb->actions() )
            {
                if( a->isSeparator() || a->text().isEmpty() ) continue;
                QJsonObject o{ {"text", a->text()}, {"tooltip", a->toolTip()},
                               {"shortcut", a->shortcut().toString( QKeySequence::NativeText )},
                               {"enabled", a->isEnabled()} };
                if( a->isCheckable() ) o["checked"] = a->isChecked();
                out.append( o );
            }
        return out;
    });

    add("ui click", "ui click <x,y> [--cmd] [--double] | ui click <comp> [index]",
        "Left click (press and release) at a scene position on the canvas, as the mouse does: "
        "on empty canvas it clears the selection and places the keyboard cursor there; --cmd holds Cmd "
        "(Ctrl elsewhere): on a wire, corner or dot it starts network mode there; in network mode, on a "
        "wire it marks or unmarks it (with --double: marks its net, specs/wire-colors.md). With a component "
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
        Qt::KeyboardModifiers mods = c.flag("cmd") ? Qt::ControlModifier : Qt::NoModifier;   // Qt: Cmd on macOS
        QMouseEvent press( QEvent::MouseButtonPress, vp, gp, Qt::LeftButton, Qt::LeftButton, mods );
        QApplication::sendEvent( v->viewport(), &press );
        QMouseEvent release( QEvent::MouseButtonRelease, vp, gp, Qt::LeftButton, Qt::NoButton, mods );
        QApplication::sendEvent( v->viewport(), &release );
        if( c.flag("double") )                   // A double click: Qt's sequence after the first click
        {
            QMouseEvent dbl( QEvent::MouseButtonDblClick, vp, gp, Qt::LeftButton, Qt::LeftButton, mods );
            QApplication::sendEvent( v->viewport(), &dbl );
            QMouseEvent release2( QEvent::MouseButtonRelease, vp, gp, Qt::LeftButton, Qt::NoButton, mods );
            QApplication::sendEvent( v->viewport(), &release2 );
        }
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

    add("kbd shortcuts", "kbd shortcuts [--table]",
        "Every keyboard shortcut of the circuit editor: category, action, keys, context. Keys are "
        "written as on macOS (Cmd+Shift+Z). --table: the rows Settings > Shortcuts shows (K10): keys "
        "as this platform writes them (⇧⌘Z on macOS, Ctrl+Shift+Z elsewhere), the part letters in "
        "effect added (category Part letters), in category order.",
        []( CliCtx& c ) -> QJsonValue
    {
        QJsonArray list;
        for( const Shortcut& s : c.has("table") ? Shortcuts::table() : Shortcuts::all() )
            list.append( QJsonObject{ {"category", s.category}, {"action", s.action},
                                      {"keys", s.keys}, {"context", s.context} } );
        return list;
    });
}
