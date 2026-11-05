/***************************************************************************
 *   Bundled fonts: Inter (sans) and JetBrains Mono (mono)                 *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

// SimulIDE ships its own fonts so the GUI, circuits and exports look the same on
// every OS (specs/inter-fonts.md). Use sans()/mono() instead of family names in code.
// The fonts are subsets made by tools/fonts_subset.sh: TTF for Qt, WOFF2 for embedding
// in SVG exports.

#ifndef APPFONTS_H
#define APPFONTS_H

#include <QString>
#include <QList>
#include <QFont>

namespace AppFonts
{
    struct Face
    {
        QString family;
        int     weight;   // CSS weight: 400, 500, 600, 700
        bool    italic;
        QString woff2;    // Resource path of the WOFF2 copy
    };

    void registerFonts();          // Add the bundled TTFs to Qt's font database (once)

    QString sans();                // "Inter"
    QString mono();                // "JetBrains Mono"

    // Legacy or OS-specific names (Ubuntu, Helvetica [Cronyx], Menlo...) -> bundled family.
    // Any other name is returned unchanged
    QString normalize( const QString& family );

    bool isBundled( const QString& family );
    QString cssStack( const QString& family );   // Family plus fallbacks, for SVG/CSS

    // Width of a space in Ubuntu at this font's pixel size. Dialogs derive their layout scale
    // from the space width (fm.width(" ")/2); Inter's space is wider (0.281 em vs 0.231 em),
    // which made every dialog and field about 33 % (or with integer division 100 %) larger
    int legacySpaceWidth( const QFont& font );

    const QList<Face>& faces();
    const Face* closestFace( const QString& family, int weight, bool italic ); // nullptr if not bundled
}

#endif
