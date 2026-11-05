/***************************************************************************
 *   Export the circuit as an image (PNG or SVG)                           *
 ***( see copyright.txt file at root folder )*******************************/

#include <QImage>
#include <QPainter>
#include <QSvgGenerator>
#include <QFileInfo>
#include <QGraphicsItem>
#include <QtMath>
#include <QFile>
#include <QRegularExpression>
#include <QDebug>

#include "circuitexport.h"
#include "circuit.h"
#include "instrumentview.h"
#include "theme.h"
#include "appfonts.h"

namespace
{
// Bounding rect of what is drawn (hidden instruments and leads left out)
QRectF drawnRect( Circuit* circ )
{
    QRectF rect;
    for( QGraphicsItem* item : circ->items() )
    {
        if( item->parentItem() || !item->isVisible() ) continue;
        QRectF r = item->sceneBoundingRect();
        if( r.isValid() ) rect |= r;
    }
    if( rect.isEmpty() ) rect = QRectF( -200, -150, 400, 300 );   // Empty circuit: the origin
    return rect;
}

// Puts the view settings back when the export is done (also on errors)
class ViewState
{
    public:
        ViewState( Circuit* circ, const ExportOptions& opt )
            : m_circ( circ )
        {
            m_grid = circ->drawGrid();
            m_meters = InstrumentView::self()->shown();
            for( QGraphicsItem* item : circ->selectedItems() ) m_selected.append( item );
            circ->clearSelection();

            circ->setGridShown( opt.grid );
            if( opt.meters != m_meters ) InstrumentView::self()->apply( opt.meters );
            if( !opt.theme.isEmpty() ) m_themed = Theme::pushPreset( opt.theme );
        }
        ~ViewState()
        {
            if( m_themed ) Theme::popColors();
            InstrumentView::self()->apply( m_meters );
            m_circ->setGridShown( m_grid );
            for( QGraphicsItem* item : m_selected ) item->setSelected( true );
            m_circ->update();
        }
    private:
        Circuit* m_circ;
        bool m_grid;
        bool m_meters;
        bool m_themed = false;
        QList<QGraphicsItem*> m_selected;
};

// QSvgGenerator writes the family names only. Replace them by CSS stacks and, if asked,
// embed the bundled faces the text uses as @font-face rules with WOFF2 data URIs, so any
// browser shows the fonts SimulIDE laid the text out with
bool fixSvgFonts( const QString& file, bool embed, QStringList* embedded, QString* error )
{
    QFile f( file );
    if( !f.open( QIODevice::ReadOnly ) ){ *error = "could not read "+file; return false; }
    QString svg = QString::fromUtf8( f.readAll() );
    f.close();
    qDebug() << "[Export] SVG fonts: post-processing" << file << svg.size() << "chars, embed:" << embed;

    // QSvgGenerator writes Qt 5 weights times ten (Medium 57 -> "570"), except Normal (400) and
    // Bold (700). Map them to the CSS weight of the face Qt draws with, else a browser picks
    // another face (570 -> SemiBold instead of Medium)
    QRegularExpression anyWgtRe("font-weight=\"([0-9]+)\"");
    QMap<int, int> cssWeight;
    auto wit = anyWgtRe.globalMatch( svg );
    while( wit.hasNext() )
    {
        int w = wit.next().captured( 1 ).toInt();
        if( w == 400 || w == 700 || cssWeight.contains( w ) ) continue;
        static const int qtW[]  = {  0,  12,  25,  50,  57,  63,  75,  81,  87 };
        static const int cssW[] = { 100, 200, 300, 400, 500, 600, 700, 800, 900 };
        int best = 0;
        for( int i=1; i<9; ++i ) if( qAbs( qtW[i]*10-w ) < qAbs( qtW[best]*10-w ) ) best = i;
        cssWeight[w] = cssW[best];
        qDebug() << "[Export] SVG fonts: Qt weight" << w << "-> CSS weight" << cssW[best];
    }
    for( int w : cssWeight.keys() )
        svg.replace("font-weight=\""+QString::number( w )+"\"", "font-weight=\""+QString::number( cssWeight[w] )+"\"");

    // Faces used by text elements: family, weight, style
    QMap<QString, const AppFonts::Face*> faces;   // Sort key -> face
    QSet<QString> seen;
    QRegularExpression textRe("<text\\b[^>]*>");
    QRegularExpression famRe ("font-family=\"([^\"]*)\"");
    QRegularExpression wgtRe ("font-weight=\"([0-9]+)\"");
    QRegularExpression styRe ("font-style=\"([a-z]+)\"");
    auto it = textRe.globalMatch( svg );
    while( it.hasNext() )
    {
        QString tag = it.next().captured( 0 );
        QString family = famRe.match( tag ).captured( 1 );
        int weight = wgtRe.match( tag ).hasMatch() ? wgtRe.match( tag ).captured( 1 ).toInt() : 400;
        bool italic = styRe.match( tag ).captured( 1 ) == "italic";
        QString key = family+"|"+QString::number( weight )+"|"+( italic ? "i" : "n" );
        if( seen.contains( key ) ) continue;
        seen.insert( key );

        const AppFonts::Face* face = AppFonts::closestFace( family, weight, italic );
        qDebug() << "[Export] SVG fonts: text uses" << family << weight << ( italic ? "italic" : "normal" )
                 << "->" << ( face ? face->woff2 : QString("not bundled, left as is") );
        if( face ) faces[ face->family+QString::number( face->weight )+( face->italic ? "i" : "" ) ] = face;
    }

    // Family names -> stacks with fallbacks (also on <g> elements)
    for( QString fam : { AppFonts::sans(), AppFonts::mono() } )
        svg.replace("font-family=\""+fam+"\"", "font-family=\""+AppFonts::cssStack( fam )+"\"");

    if( embed && !faces.isEmpty() )
    {
        QString css;
        for( const AppFonts::Face* face : faces )
        {
            QFile ff( face->woff2 );
            if( !ff.open( QIODevice::ReadOnly ) ){ *error = "missing font resource "+face->woff2; return false; }
            QByteArray data = ff.readAll();
            css += QString("@font-face{font-family:'%1';font-weight:%2;font-style:%3;"
                           "src:url(data:font/woff2;base64,%4) format('woff2');}\n")
                   .arg( face->family ).arg( face->weight ).arg( face->italic ? "italic" : "normal" )
                   .arg( QString::fromLatin1( data.toBase64() ) );
            QString name = face->family+" "+QString::number( face->weight )+( face->italic ? " italic" : "" );
            embedded->append( name );
            qDebug() << "[Export] SVG fonts: embedded" << name << data.size() << "bytes";
        }
        QString style = "<style type=\"text/css\"><![CDATA[\n"+css+"]]></style>\n";
        // @font-face needs full SVG, not the Tiny profile QSvgGenerator declares
        svg.replace("version=\"1.2\" baseProfile=\"tiny\"", "version=\"1.1\"");
        int defs = svg.indexOf("<defs>");
        if( defs >= 0 ) svg.insert( defs+6, "\n"+style );
        else{
            int svgEnd = svg.indexOf('>', svg.indexOf("<svg") );
            svg.insert( svgEnd+1, "\n<defs>\n"+style+"</defs>\n");
        }
    }
    if( !f.open( QIODevice::WriteOnly | QIODevice::Truncate ) ){ *error = "could not write "+file; return false; }
    f.write( svg.toUtf8() );
    qDebug() << "[Export] SVG fonts: done," << embedded->size() << "faces embedded, file" << f.size() << "bytes";
    return true;
}
}

QString CircuitExport::formatOf( QString file )
{
    QString ext = QFileInfo( file ).suffix().toLower();
    if( ext == "png" || ext == "svg" ) return ext;
    return "";
}

bool CircuitExport::write( QString file, const ExportOptions& opt, ExportResult* res, QString* error )
{
    auto fail = [error]( QString e ){ if( error ) *error = e; return false; };

    Circuit* circ = Circuit::self();
    if( !circ ) return fail("no circuit");
    QString format = opt.format.toLower();
    if( format != "png" && format != "svg" ) return fail("format must be png or svg");
    if( !opt.theme.isEmpty() && !Theme::presets().contains( opt.theme, Qt::CaseInsensitive ) )
        return fail("unknown theme: "+opt.theme+" ("+Theme::presets().join(", ")+")");
    if( opt.scale <= 0 ) return fail("scale must be > 0");

    ViewState state( circ, opt );

    QRectF rect = drawnRect( circ );
    rect.adjust( -opt.margin, -opt.margin, opt.margin, opt.margin );

    ExportResult r;
    r.file = file;
    r.area = rect;

    if( format == "svg" )
    {
        QSize size( qCeil( rect.width() ), qCeil( rect.height() ) );
        QSvgGenerator svg;
        svg.setFileName( file );
        svg.setSize( size );
        svg.setResolution( 96 );        // 1 circuit unit = 1 CSS px
        svg.setViewBox( QRect( QPoint(0,0), size ) );
        svg.setTitle( QFileInfo( circ->getFilePath() ).completeBaseName() );
        svg.setDescription("Generated by SimulIDE");

        QPainter painter;
        if( !painter.begin( &svg ) ) return fail("could not write "+file );
        circ->render( &painter, QRectF( QPointF(0,0), QSizeF( size ) ), rect, Qt::KeepAspectRatio );
        painter.end();
        r.size = size;

        QString fontError;
        if( !fixSvgFonts( file, opt.embedFonts, &r.fonts, &fontError ) ) return fail( fontError );
    }else{
        double scale = opt.scale;
        double maxSide = 16000;
        if( rect.width()*scale > maxSide || rect.height()*scale > maxSide )
            scale = maxSide/qMax( rect.width(), rect.height() );

        QImage img( QSize( qCeil( rect.width()*scale ), qCeil( rect.height()*scale ) ), QImage::Format_ARGB32 );
        img.fill( Theme::canvas() );
        QPainter painter( &img );
        painter.setRenderHints( QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform );
        circ->render( &painter, QRectF( QPointF(0,0), QSizeF( img.size() ) ), rect, Qt::KeepAspectRatio );
        painter.end();
        if( !img.save( file, "PNG" ) ) return fail("could not write "+file );
        r.size = img.size();
        r.scale = scale;
    }
    if( !QFileInfo::exists( file ) ) return fail("could not write "+file );
    if( res ) *res = r;
    return true;
}
