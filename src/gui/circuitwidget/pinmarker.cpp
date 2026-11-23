/***************************************************************************
 *   Pin markers (see pinmarker.h)                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QPainter>
#include <QFontMetrics>
#include <QGraphicsScene>
#include <cmath>

#include "pinmarker.h"
#include "pin.h"
#include "component.h"
#include "appfonts.h"

QList<PinMarker*> PinMarker::m_markers;
const void* PinMarker::m_owner = nullptr;

static const double R = 9;                 // The ring's radius (screen pixels)

static QFont tagFont()
{
    QFont f( AppFonts::mono() );
    f.setPixelSize( 15 );
    f.setBold( true );
    return f;
}

void PinMarker::show( const void* owner, const QList<QPair<Pin*, QString>>& pins )
{
    while( !m_markers.isEmpty() ) delete m_markers.first();   // The destructor unlists it
    m_owner = owner;
    for( const QPair<Pin*, QString>& p : pins )
        if( p.first && p.first->scene() ) new PinMarker( p.first, p.second );
}

void PinMarker::clear( const void* owner )
{
    if( owner != m_owner ) return;
    while( !m_markers.isEmpty() ) delete m_markers.first();
    m_owner = nullptr;
}

int PinMarker::count() { return m_markers.size(); }

PinMarker::PinMarker( Pin* pin, const QString& tag )
         : QGraphicsItem()
{
    m_pin = pin;
    m_tag = tag;
    setFlag( QGraphicsItem::ItemIgnoresTransformations, true );   // The same size at any zoom
    setAcceptedMouseButtons( Qt::NoButton );
    setAcceptHoverEvents( false );
    setZValue( 1e6 );                                             // Above wires and parts
    QFontMetrics fm( tagFont() );
    m_tagRect = QRectF( 0, 0, tag.isEmpty() ? 0 : fm.horizontalAdvance( tag )+10, tag.isEmpty() ? 0 : fm.height()+4 );
    pin->scene()->addItem( this );
    m_anchor = new PinMarkerAnchor( pin, this );
    follow();
    m_markers.append( this );
}

PinMarker::~PinMarker()
{
    m_markers.removeOne( this );
    if( m_anchor ){ m_anchor->m_marker = nullptr; delete m_anchor; }
}

void PinMarker::follow()
{
    // Outwards: from the part's centre to the pin's end (scene = screen directions: the view is
    // not rotated). The wire leaves that way, so the tag goes to the side: outwards turned 90°
    QPointF out( 0, -1 );
    if( Component* c = m_pin->component() )
    {
        QPointF d = m_pin->scenePos()-c->sceneBoundingRect().center();
        double l = std::hypot( d.x(), d.y() );
        if( l > 0.5 ) out = d/l;
    }
    QPointF side( -out.y(), out.x() );
    if( side != m_side ){ prepareGeometryChange(); m_side = side; }
    setPos( m_pin->scenePos() );
}

QRectF PinMarker::boundingRect() const   // The tag may go to any side of the ring
{
    double e = R+6+std::max( m_tagRect.width(), m_tagRect.height() );
    return QRectF( -e, -e, 2*e, 2*e );
}

void PinMarker::paint( QPainter* p, const QStyleOptionGraphicsItem*, QWidget* )
{
    QColor accent( 255, 140, 0 );
    p->setRenderHint( QPainter::Antialiasing, true );
    p->setPen( QPen( accent, 3 ) );
    p->setBrush( Qt::NoBrush );
    p->drawEllipse( QPointF( 0, 0 ), R, R );
    if( m_tag.isEmpty() ) return;

    double w = m_tagRect.width(), h = m_tagRect.height();   // Beside the ring, its edge R+4 away
    QPointF c = m_side*( R+4 )+QPointF( m_side.x()*w/2, m_side.y()*h/2 );
    QRectF r( c.x()-w/2, c.y()-h/2, w, h );
    p->setPen( QPen( QColor( 0, 0, 0, 160 ), 1 ) );          // A thin dark edge: readable on any canvas
    p->setBrush( accent );
    p->drawRoundedRect( r, 4, 4 );
    p->setPen( Qt::black );
    p->setFont( tagFont() );
    p->drawText( r, Qt::AlignCenter, m_tag );
}

PinMarkerAnchor::PinMarkerAnchor( Pin* pin, PinMarker* marker )
               : QGraphicsItem( pin )
{
    m_marker = marker;
    setFlag( QGraphicsItem::ItemSendsScenePositionChanges, true );
    setFlag( QGraphicsItem::ItemHasNoContents, true );
    setAcceptedMouseButtons( Qt::NoButton );
}

PinMarkerAnchor::~PinMarkerAnchor()   // The pin went (its part deleted): the marker goes too
{
    if( m_marker ){ m_marker->m_anchor = nullptr; delete m_marker; }
}

QVariant PinMarkerAnchor::itemChange( GraphicsItemChange change, const QVariant& value )
{
    if( change == ItemScenePositionHasChanged && m_marker ) m_marker->follow();
    return QGraphicsItem::itemChange( change, value );
}
