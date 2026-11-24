/***************************************************************************
 *   The line type of a border: the "Border_Style" property of the        *
 *   graphical items (Rectangle, Ellipse, Image, Line, Text)              *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef BORDERSTYLE_H
#define BORDERSTYLE_H

#include <QCoreApplication>
#include <QColor>
#include <QPen>
#include <QStringList>

// Saved as the name of a Qt pen style. Set from that name, the caption shown in the dialog or
// the index; anything else is Solid. See specs/gfx-border-style.md.
struct BorderStyle
{
    static QStringList values()
    { return { "Solid", "Dash", "Dot", "DashDot", "DashDotDot" }; }

    static QString enums()     // Values and captions, as an enum property's unit ("values;captions")
    {
        QStringList captions;
        for( const char* c : { "Solid", "Dashed", "Dotted", "Dash-Dot", "Dash-Dot-Dot" } )
            captions.append( QCoreApplication::translate( "BorderStyle", c ) );
        return values().join(',')+";"+captions.join(',');
    }

    static QString name( QString style )   // The saved value of a name, a caption or an index
    {
        QStringList vals  = values();
        QStringList capts = enums().split(';').at(1).split(',');
        style = style.trimmed();
        for( int i=0; i<vals.size(); ++i )
        {
            if( vals.at(i).compare( style, Qt::CaseInsensitive ) == 0 ) return vals.at(i);
            if( i < capts.size() && capts.at(i).compare( style, Qt::CaseInsensitive ) == 0 ) return vals.at(i);
        }
        bool isInt = false;
        int index = style.toInt( &isInt );
        return ( isInt && index >= 0 && index < vals.size() ) ? vals.at( index ) : vals.first();
    }

    static Qt::PenStyle penStyle( QString name )   // Of a value given by name()
    {
        int index = values().indexOf( name );
        if( index < 0 ) index = 0;
        static const Qt::PenStyle styles[] =
            { Qt::SolidLine, Qt::DashLine, Qt::DotLine, Qt::DashDotLine, Qt::DashDotDotLine };
        return styles[index];
    }

    static QPen pen( QColor color, qreal width, Qt::PenStyle style )   // As the borders are drawn
    { return QPen( color, width, style, Qt::RoundCap, Qt::RoundJoin ); }
};

#endif
