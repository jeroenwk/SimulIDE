/***************************************************************************
 *   Shortcut texts in the platform's notation                              *
 ***( see copyright.txt file at root folder )*******************************/

#include <QAction>

#include "keytext.h"

QString KeyText::native( const QString& portable )
{
    return QKeySequence( portable, QKeySequence::PortableText ).toString( QKeySequence::NativeText );
}

QString KeyText::native( const QKeySequence& keys )
{
    return keys.toString( QKeySequence::NativeText );
}

static void updateTip( QAction* a )
{
    QString t = a->text();
    int tab = t.indexOf('\t');
    if( tab >= 0 ) t = t.left( tab );
    t.remove( QRegExp("&(?!&)") );
    if( t.endsWith("...") ) t.chop( 3 );
    QString k = a->property("tipKey").toString();
    a->setToolTip( k.isEmpty() ? t : t+" ("+KeyText::native( k )+")" );  // Same tip: no signal
}

void KeyText::tipWithKey( QAction* a, const QString& portable )
{
    bool first = !a->property("tipKey").isValid();
    a->setProperty("tipKey", portable );
    updateTip( a );
    if( first ) QObject::connect( a, &QAction::changed, a, [a](){ updateTip( a ); } );
}
