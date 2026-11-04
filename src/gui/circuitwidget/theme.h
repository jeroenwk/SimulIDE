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

        // Instrument leads (scope channels, meter + / COM): the lead's own colour, made to
        // stand out on the canvas (contrast >= 3:1, darker on light canvases). A grey lead
        // (COM, ground) is moved away from the wire colour, so it does not look like a wire
        static QColor lead( QColor c );
        static double contrast( QColor a, QColor b );  // WCAG contrast ratio, 1..21

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
        static bool    applyPreset( QString name ); // false: unknown name
        static QString currentPreset();             // Preset matching all colours, or "Custom"

    private:
        static void load();
        static bool   m_loaded;
        static QColor m_color[numRoles];
};

#endif
