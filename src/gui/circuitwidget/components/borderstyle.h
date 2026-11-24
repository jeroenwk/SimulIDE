/***************************************************************************
 *   The border of a graphical item: its line type ("Border_Style") and   *
 *   colour ("Border_Color"). Rectangle, Ellipse, Image, Line, Text       *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef BORDERSTYLE_H
#define BORDERSTYLE_H

#include <QCoreApplication>
#include <QColor>
#include <QPen>
#include <QStringList>

// The line type is saved as the name of a Qt pen style. It is set from that name, the caption
// shown in the dialog or the index; anything else is Solid. See specs/gfx-border-style.md.
// The colour defaults to black, as the border was always drawn: saved circuits look the same.
class BorderStyle
{
    public:
        static QStringList values()
        { return { "Solid", "Dash", "Dot", "DashDot", "DashDotDot" }; }

        static QStringList captions()   // As shown in the dialog (translated)
        {
            static const char* const c[] = {
                QT_TRANSLATE_NOOP( "BorderStyle", "Solid" ),
                QT_TRANSLATE_NOOP( "BorderStyle", "Dashed" ),
                QT_TRANSLATE_NOOP( "BorderStyle", "Dotted" ),
                QT_TRANSLATE_NOOP( "BorderStyle", "Dash-Dot" ),
                QT_TRANSLATE_NOOP( "BorderStyle", "Dash-Dot-Dot" ) };
            QStringList list;
            for( const char* s : c ) list.append( QCoreApplication::translate( "BorderStyle", s ) );
            return list;
        }

        static QString enums()   // Values and captions, as an enum property's unit ("values;captions")
        { return values().join(',')+";"+captions().join(','); }

        static QString name( QString style )   // The saved value of a name, a caption or an index
        {
            QStringList vals  = values();
            QStringList capts = captions();
            style = style.trimmed();
            for( int i=0; i<vals.size(); ++i )
            {
                if( vals.at(i).compare( style, Qt::CaseInsensitive ) == 0 ) return vals.at(i);
                if( capts.at(i).compare( style, Qt::CaseInsensitive ) == 0 ) return vals.at(i);
            }
            bool isInt = false;
            int index = style.toInt( &isInt );
            return ( isInt && index >= 0 && index < vals.size() ) ? vals.at( index ) : vals.first();
        }

        // --- The border of one item -------------------------------------------------

        QString style() const { return m_name; }
        void setStyle( QString style )
        {
            static const Qt::PenStyle styles[] =
                { Qt::SolidLine, Qt::DashLine, Qt::DotLine, Qt::DashDotLine, Qt::DashDotDotLine };
            m_name  = name( style );
            m_style = styles[ qMax( 0, values().indexOf( m_name ) ) ];
        }

        QColor color() const { return m_color; }
        void setColor( QColor color ) { m_color = color; }

        QString colorStr() const { return m_color.name(); }
        bool setColorStr( QString n )   // False, and the colour kept, when n is not a colour (junk in a file)
        {
            QColor color( n.trimmed() );
            if( !color.isValid() ) return false;
            m_color = color;
            return true;
        }

        QPen pen( qreal width ) const { return pen( m_color, width ); }   // As the borders are drawn
        QPen pen( QColor color, qreal width ) const                       // In another colour (Line)
        { return QPen( color, width, m_style, Qt::RoundCap, Qt::RoundJoin ); }

    private:
        QColor       m_color = Qt::black;
        QString      m_name  = "Solid";
        Qt::PenStyle m_style = Qt::SolidLine;
};

#endif
