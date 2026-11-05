/***************************************************************************
 *   Gauge bar and live label beside a reactive part: capacitor charge,    *
 *   inductor and transformer-coil current.                                *
 *   See specs/capacitor-charge.md and specs/inductor-current.md           *
 ***( see copyright.txt file at root folder )*******************************/

#include <QPainter>
#include <QtMath>

#include "chargegauge.h"
#include "chargeview.h"
#include "component.h"
#include "label.h"
#include "theme.h"

ChargeGauge::ChargeGauge( Component* part, GaugeOwner* owner )
           : QGraphicsItem( part )
{
    m_part  = part;
    m_owner = owner;
    m_anchored = false;
    m_height   = 14;
    setAcceptedMouseButtons( Qt::NoButton );
    setAcceptHoverEvents( false );
    setZValue( 1 );
}

void ChargeGauge::setAnchor( QPointF at, QPointF out, double height )
{
    m_anchored = true;
    m_anchor = at;
    m_out    = out;
    m_height = qBound( 2.0, height, 14.0 );
}

void ChargeGauge::refresh()
{
    // Undo the parent's rotation and flip: draw in scene-aligned coordinates
    QTransform st = m_part->sceneTransform();
    QTransform lin( st.m11(), st.m12(), st.m21(), st.m22(), 0, 0 );
    bool ok = false;
    QTransform inv = lin.inverted( &ok );
    if( ok && inv != transform() ) setTransform( inv );

    QRectF bar;
    int textSide = 1;                       // Text right (1) or left (-1) of the bar

    if( m_anchored )
    {
        // Beside the anchor, away from the part: left/right of it, or above/below
        QPointF a = lin.map( m_anchor );
        QPointF o = lin.map( m_out );
        double h = m_height;
        if( qAbs( o.x() ) >= qAbs( o.y() ) )
        {
            textSide = ( o.x() >= 0 ) ? 1 : -1;
            double x = ( textSide > 0 ) ? a.x()+1 : a.x()-4;
            bar = QRectF( x, a.y()-h/2, 3, h );
        }
        else bar = ( o.y() >= 0 ) ? QRectF( a.x()-1.5, a.y()+1, 3, h )
                                  : QRectF( a.x()-1.5, a.y()-1-h, 3, h );
    }
    else
    {
        // Leads along screen x: horizontal part
        QPointF lead = lin.map( QPointF( 1, 0 ) );
        bool horizontal = qAbs( lead.x() ) > qAbs( lead.y() );

        // Side opposite the value label (if shown), else right / above
        int side = 1;
        Label* vl = m_part->valLabel();
        if( vl && vl->isVisible() && !vl->toPlainText().isEmpty() )
        {
            QPointF lc = vl->mapToScene( vl->boundingRect().center() ) - m_part->scenePos();
            if( horizontal ) side = ( lc.y() < 0 ) ? -1 : 1;   // Label above: gauge below
            else             side = ( lc.x() > 0 ) ? -1 : 1;   // Label right: gauge left
        }
        if( horizontal ) bar = ( side > 0 ) ? QRectF(-1.5,-23, 3, 14 ) : QRectF(-1.5, 9, 3, 14 );
        else             bar = ( side > 0 ) ? QRectF( 9, -7, 3, 14 )   : QRectF(-12, -7, 3, 14 );
        if( !horizontal && side < 0 ) textSide = -1;
    }

    m_level = m_owner->gaugeLevel();
    m_tick  = m_owner->gaugeTick();
    m_warn  = m_owner->gaugeWarn();

    // Label text
    int mode = ChargeView::self()->labelMode();
    QString text = ( mode == ChargeView::labelOff ) ? "" : m_owner->gaugeText( mode );

    QFont font = m_part->valLabel() ? m_part->valLabel()->font() : QFont();
    font.setPixelSize( 7 );
    QFontMetricsF fm( font );
    double tw = text.isEmpty() ? 0 : fm.horizontalAdvance( text );
    double ty = bar.center().y()+fm.ascent()/2-1;
    QPointF tp = ( textSide > 0 ) ? QPointF( bar.right()+2, ty ) : QPointF( bar.left()-2-tw, ty );

    QRectF bound = bar.adjusted(-1,-5,1,1 );   // Overflow triangle above
    if( tw > 0 ) bound = bound.united( QRectF( tp.x(), tp.y()-fm.ascent(), tw, fm.height() ) );
    bound.adjust(-1,-1, 1, 1 );

    if( bound != m_bound ) prepareGeometryChange();
    m_bar = bar; m_bound = bound; m_textPos = tp; m_text = text;
    if( !isVisible() ) setVisible( true );
    update();
}

void ChargeGauge::paint( QPainter* p, const QStyleOptionGraphicsItem*, QWidget* )
{
    ChargeView* cv = ChargeView::self();
    double lvl  = m_level;              // Snapshot of the last refresh()
    double tick = m_tick;
    bool   warn = m_warn;

    p->setRenderHint( QPainter::Antialiasing, true );

    // Outline
    QColor outline = Theme::line();
    outline.setAlphaF( 0.6 );
    p->setPen( QPen( outline, 0.5 ) );
    p->setBrush( Qt::NoBrush );
    p->drawRect( m_bar );

    // Fill, from the bottom up
    double h = qMin( lvl, 1.0 )*m_bar.height();
    if( h > 0 )
    {
        QColor fill = warn ? QColor( 230, 40, 40 ) : Theme::flow();
        if( warn && !cv->blinkOn() ) fill.setAlphaF( 0.35 );
        p->setPen( Qt::NoPen );
        p->setBrush( fill );
        p->drawRect( QRectF( m_bar.left(), m_bar.bottom()-h, m_bar.width(), h ) );
    }
    // Overflow: a small triangle on top
    if( lvl > 1 )
    {
        QPolygonF tri;
        tri << QPointF( m_bar.left()-0.5, m_bar.top()-0.5 )
            << QPointF( m_bar.right()+0.5, m_bar.top()-0.5 )
            << QPointF( m_bar.center().x(), m_bar.top()-3.5 );
        p->setPen( Qt::NoPen );
        p->setBrush( warn ? QColor( 230, 40, 40 ) : Theme::flow() );
        p->drawPolygon( tri );
    }
    // Rating tick
    if( tick >= 0 && tick < 1 )
    {
        double y = m_bar.bottom() - tick*m_bar.height();
        p->setPen( QPen( QColor( 230, 40, 40 ), 0.8 ) );
        p->drawLine( QPointF( m_bar.left()-1, y ), QPointF( m_bar.right()+1, y ) );
    }
    // Label
    if( !m_text.isEmpty() )
    {
        QFont font = m_part->valLabel() ? m_part->valLabel()->font() : QFont();
        font.setPixelSize( 7 );
        p->setFont( font );
        p->setPen( warn ? QColor( 230, 40, 40 ) : Theme::valLabel() );
        p->drawText( m_textPos, m_text );
    }
}
