/***************************************************************************
 *   Circuit colours: canvas, wires, component outlines and text           *
 *   Set in Settings > Circuit (and by the CLI). Defaults = classic look.  *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef THEME_H
#define THEME_H

#include <QColor>
#include <QString>
#include <QStringList>

class Theme
{
    public:
        enum role_t { canvasRole=0, wireRole, lineRole, textRole, bodyRole, flowRole, nameRole, valueRole,
                      icBodyRole, icTextRole,   // Chips, subcircuits and MCUs
                      // Animate mode: wires by voltage, pins by logic state
                      animHighRole, animLowRole, outHighRole, outLowRole, inHighRole, inLowRole, openHighRole,
                      numRoles };
        static constexpr int firstAnimRole = animHighRole;

        static QColor canvas()   { return get( canvasRole ); }
        static QColor grid();                                // Derived from the canvas
        static QColor wire()     { return get( wireRole ); }
        static QColor line()     { return get( lineRole ); } // Component outlines, pins, arrowheads
        static QColor text()     { return get( textRole ); } // Pin names (text inside components)
        static QColor body()     { return get( bodyRole ); } // Fill of components that use the default white
        static QColor flow()     { return get( flowRole ); } // Current flow dots

        static QColor idLabel()  { return get( nameRole ); }  // Component name labels ("R1")
        static QColor valLabel() { return get( valueRole ); } // Component value labels ("470 Ω")

        // IC packages (chips, subcircuits, MCUs). At the default IC body colour each kind keeps
        // its own classic shade (classColor); the pin-1 marker is derived from body and text
        static QColor icBody( QColor classColor );
        static QColor icText()   { return get( icTextRole ); }
        static QColor icMarker();
        // Logic-symbol packages: their own light colour while body and IC body are the defaults
        static QColor lsBody( QColor classColor );

        // Instrument leads (scope channels, meter + / COM): the lead's own colour, made to
        // stand out on the canvas (contrast >= 3:1, darker on light canvases). A grey lead
        // (COM, ground) is moved away from the wire colour, so it does not look like a wire
        static QColor lead( QColor c );
        static double contrast( QColor a, QColor b );  // WCAG contrast ratio, 1..21

        // Instrument screens (meter displays, scope and logic analyzer screens) follow the canvas:
        // light screens with dark text and darkened traces on a light canvas, black ones on a dark canvas
        static bool   lightScreens() { return canvas().lightness() >= 128; }
        static QColor screenBg()     { return lightScreens() ? QColor( 246, 246, 240 ) : QColor( 0, 0, 0 ); }
        static QColor screenText()   { return lightScreens() ? QColor( 25, 25, 25 ) : QColor( Qt::yellow ); }
        static QColor screenTrace( QColor c );     // A channel colour made readable on the screen
        static QColor instrumentBody() { return lightScreens() ? QColor( 230, 230, 230 ) : QColor( 58, 58, 62 ); }

        static QColor animWire( bool high ) { return get( high ? animHighRole : animLowRole ); }
        static QColor pinState( int state, QColor undefColor ); // Animate colour of a pinState_t

        static QColor get( role_t r );
        static void   set( role_t r, QColor c );   // Saves and repaints
        static void   reset();                     // Back to the classic colours

        static QColor  defaultColor( role_t r );
        static QString key( role_t r );            // "canvasColor", ...
        static QString caption( role_t r );        // "Canvas", ...

        static void apply();                       // Repaint the circuit with the current colours

        // Presets: Classic (default), Light, Dark (EveryCircuit style)
        static QStringList presets();
        static QStringList choices();               // "System" + presets (Settings, CLI)
        static bool    applyPreset( QString name ); // false: unknown name. "System": Light or Dark, following the OS
        static QString currentPreset();             // "System" while following the OS, the preset matching all colours, or "Custom"
        static bool    followsSystem();

        // Image export: use a preset for a moment without saving it, then put the colours back
        static bool pushPreset( QString name );     // false: unknown name (nothing changed)
        static void popColors();

    private:
        static void load();
        static void adoptPresetIcColors();
        static bool setPreset( QString name );     // Colours of a preset, saved (no change to following)
        static void setFollowSystem( bool on );
        static void systemChanged( bool dark );
        static bool   m_follow;
        static bool   m_loaded;
        static QColor m_color[numRoles];
        static QColor m_saved[numRoles];
        static bool   m_pushed;
};

#endif
