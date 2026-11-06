/***************************************************************************
 *   Application look: light or dark widgets, following the OS by default *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

// Light or dark look for the application's widgets (not the circuit canvas, which has
// its own Theme). Mode "system" follows the OS appearance and changes with it.
//
// The widgets, .ui files and icons were drawn for a light palette, so dark mode does more
// than set a dark palette: an app-wide event filter adapts colours that widgets set
// themselves (local palettes, "color:" style sheets), and a proxy style redraws grey and
// black icon pixels light. A widget can opt out with the property "uiThemeKeep" = true.

#ifndef UITHEME_H
#define UITHEME_H

#include <QObject>
#include <QColor>
#include <QFont>
#include <QIcon>
#include <QHash>
#include <QPalette>

class QSettings;
class QWidget;

class UiTheme : public QObject
{
    Q_OBJECT
    public:
        enum mode_t { System=0, Light, Dark };

        static UiTheme* self();

        void init( QSettings* settings );   // Sets the style and the palette, call once at start

        mode_t mode() const { return m_mode; }
        void   setMode( mode_t m );
        static QString modeName( mode_t m );
        static bool    parseMode( QString s, mode_t &m );

        bool isDark() const { return m_dark; }
        bool systemDark();

        // Dark-mode versions of colours and icons drawn for a light background (identity in light mode)
        QColor fg( QColor c );          // Text: dark colours become light, hue kept
        QColor bg( QColor c );          // Light greyish backgrounds become dark, hue kept
        // The other way: a text colour chosen for a dark background, made readable on a light one
        static QColor onLight( QColor c );

        // A text widget filled with a signal colour (scope channel buttons and boxes): the colour
        // with black text in light mode; in dark mode a dark shade of it, with the colour as text
        QString chipSheet( QColor c );
        QIcon  icon( const QIcon &i );
        static QImage darkImage( const QImage &img );

        static bool isLightBg( const QColor &c );

        // Section headings (component list categories, Settings sections, property dialog type):
        // purple (lightened by fg() in dark mode), bold, 13 px times the app font scale
        static QColor  headingColor() { return QColor( 85, 0, 127 ); }
        static QFont   headingFont();
        static QString headingSheet();

        bool eventFilter( QObject* obj, QEvent* e ) override;

    signals:
        void changed( bool dark );
        void systemAppearance( bool dark );   // The OS appearance changed (whatever the app's mode)

    private:
        UiTheme();

        void apply();
        void systemChanged();
        void adaptWidget( QWidget* w );
        void adaptPalette( QWidget* w );
        void adaptStyleSheet( QWidget* w );
        QString adaptSheet( QString ss );

        QPalette darkPalette();
        QPalette darkVersion( const QPalette &orig );

        mode_t     m_mode;
        bool       m_dark;
        bool       m_busy;
        bool       m_initDone;
        QSettings* m_settings;
        QPalette   m_lightPalette;
        bool       m_startDark;     // OS palette at start (platforms without appearance query)

        QHash<qint64, QIcon> m_iconCache;
};

#endif
