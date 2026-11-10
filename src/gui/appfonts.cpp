/***************************************************************************
 *   Bundled fonts: Inter (sans) and JetBrains Mono (mono)                 *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QFontDatabase>
#include <QFontInfo>
#include <QFontMetrics>
#include <QStringList>
#include <QDebug>
#include <QSet>
#include <cstdlib>

#include "appfonts.h"

namespace
{
const char* SANS = "Inter";
const char* MONO = "JetBrains Mono";

const QList<AppFonts::Face>& faceList()
{
    static const QList<AppFonts::Face> list = {
        { SANS, 400, false, ":/fonts/Inter-Regular.woff2" },
        { SANS, 500, false, ":/fonts/Inter-Medium.woff2" },
        { SANS, 600, false, ":/fonts/Inter-SemiBold.woff2" },
        { SANS, 700, false, ":/fonts/Inter-Bold.woff2" },
        { MONO, 400, false, ":/fonts/JetBrainsMono-Regular.woff2" },
        { MONO, 500, false, ":/fonts/JetBrainsMono-Medium.woff2" },
        { MONO, 600, false, ":/fonts/JetBrainsMono-SemiBold.woff2" },
        { MONO, 700, false, ":/fonts/JetBrainsMono-Bold.woff2" },
        { MONO, 400, true,  ":/fonts/JetBrainsMono-Italic.woff2" },
    };
    return list;
}
}

void AppFonts::registerFonts()
{
    static bool done = false;
    if( done ) return;
    done = true;

    const QStringList files = {
        "Inter-Regular", "Inter-Medium", "Inter-SemiBold", "Inter-Bold",
        "JetBrainsMono-Regular", "JetBrainsMono-Medium", "JetBrainsMono-SemiBold",
        "JetBrainsMono-Bold", "JetBrainsMono-Italic" };

    for( const QString& f : files )
    {
        QString path = ":/fonts/"+f+".ttf";
        int id = QFontDatabase::addApplicationFont( path );
        if( id < 0 ) qWarning() << "[Fonts] FAILED to load" << path;
        else         qDebug() << "[Fonts] loaded" << path << "families:" << QFontDatabase::applicationFontFamilies( id );
    }
    QFontDatabase db;
    qDebug() << "[Fonts] sans" << SANS << "styles:" << db.styles( SANS );
    qDebug() << "[Fonts] mono" << MONO << "styles:" << db.styles( MONO );
}

QString AppFonts::sans() { return SANS; }
QString AppFonts::mono() { return MONO; }

QString AppFonts::normalize( const QString& family )
{
    static const QStringList toSans = { "", "Ubuntu", "Helvetica [Cronyx]", "Helvetica",
                                        "Sans", "Sans Serif", "Arial" };
    static const QStringList toMono = { "Ubuntu Mono", "Menlo", "Monospace", "Courier",
                                        "Courier New" };
    QString f = family.trimmed();
    QString result = family;
    if     ( toSans.contains( f, Qt::CaseInsensitive ) ) result = SANS;
    else if( toMono.contains( f, Qt::CaseInsensitive ) ) result = MONO;

    if( result != family ) qDebug() << "[Fonts] normalize" << family << "->" << result;
    return result;
}

bool AppFonts::isBundled( const QString& family )
{
    return family == SANS || family == MONO;
}

QString AppFonts::cssStack( const QString& family )
{
    if( family == SANS ) return "Inter, 'Helvetica Neue', Arial, sans-serif";
    if( family == MONO ) return "'JetBrains Mono', Menlo, Consolas, 'DejaVu Sans Mono', monospace";
    return family;
}

const QList<AppFonts::Face>& AppFonts::faces() { return faceList(); }

const AppFonts::Face* AppFonts::closestFace( const QString& family, int weight, bool italic )
{
    const Face* best = nullptr;
    int bestDist = 100000;
    for( const Face& face : faceList() )
    {
        if( face.family != family ) continue;
        // A wrong style costs more than any weight difference
        int dist = std::abs( face.weight-weight ) + ( face.italic != italic ? 10000 : 0 );
        // Ties: prefer the heavier face above 500, the lighter one below (CSS matching)
        if( dist < bestDist || ( dist == bestDist && weight > 500 && face.weight > best->weight ) )
        {
            best = &face;
            bestDist = dist;
        }
    }
    return best;
}

int AppFonts::legacySpaceWidth( const QFont& font )
{
    const double UBUNTU_SPACE_EM = 0.231;   // Ubuntu-R.ttf: advance 231 / 1000 units per em
    int px = QFontInfo( font ).pixelSize();
    int w  = qRound( UBUNTU_SPACE_EM*px );
    static QSet<int> logged;
    if( !logged.contains( px ) ){
        logged.insert( px );
        qDebug() << "[Fonts] legacy space width at" << px << "px:" << w << "(" << font.family()
                 << "space:" << QFontMetrics( font ).width(" ") << ")";
    }
    return w;
}
