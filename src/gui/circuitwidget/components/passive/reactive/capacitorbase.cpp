/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QPainter>
#include <QtMath>

#include "capacitorbase.h"
#include "chargeview.h"
#include "simulator.h"
#include "theme.h"
#include "label.h"
#include "pin.h"

#include "doubleprop.h"
#include "boolprop.h"

#define tr(str) simulideTr("CapacitorBase",str)

CapacitorBase::CapacitorBase( QString type, QString id )
             : Reactive( type, id )
{
    m_area = QRectF(-10,-8, 20, 16 );

    m_pin[0]->setLength( 12 );
    m_pin[1]->setLength( 12 );

    m_value = m_capacitance = 0.00001; // Farads
    m_maxVolt = 0;
    m_logCharge = false;                     // Rated voltage: 0 = not set

    addPropGroup( { tr("Main"), {
        new DoubProp<CapacitorBase>("Capacitance", tr("Capacitance"), "µF"
                                   , this, &CapacitorBase::value, &CapacitorBase::setValue ),

        new DoubProp<CapacitorBase>("Resistance" , tr("Resistance"), "µΩ"
                                   , this, &CapacitorBase::resist , &CapacitorBase::setResist ),

        new DoubProp<CapacitorBase>("InitVolt"   , tr("Initial Voltage"), "V"
                                   , this, &CapacitorBase::initVolt, &CapacitorBase::setInitVolt ),

        new DoubProp<CapacitorBase>("MaxVolt"    , tr("Rated Voltage (0 = not set)"), "V"
                                   , this, &CapacitorBase::maxVolt, &CapacitorBase::setMaxVolt ),

        new BoolProp<CapacitorBase>("LogCharge", tr("Log charge in Messages"), ""
                                   , this, &CapacitorBase::logCharge, &CapacitorBase::setLogCharge ),

        new DoubProp<CapacitorBase>("ReaStep", tr("Reactive Step"), "ns"
                                   , this, &CapacitorBase::reaStep , &CapacitorBase::setReaStep,0 )
    },0 } );

    setShowProp("Capacitance");
    setPropStr( "Capacitance", "10 µF" );

    m_gauge = new ChargeGauge( this );
    m_gauge->setVisible( false );
}
CapacitorBase::~CapacitorBase(){}

void CapacitorBase::setCurrentValue( double c )
{
    m_capacitance = c;
    m_changed = true;
}

void CapacitorBase::setMaxVolt( double v )
{
    m_maxVolt = ( v > 0 ) ? v : 0;
    update();
}

void CapacitorBase::updateStep()
{
    Reactive::updateStep();                 // Repaints the part (plate colours)
    if( chargeShown() ) m_gauge->refresh();
    else if( m_gauge->isVisible() ) m_gauge->setVisible( false );
}

bool CapacitorBase::chargeShown()
{
    return ChargeView::self()->active() && m_pin[0]->isConnected() && m_pin[1]->isConnected();
}

void CapacitorBase::chargeDisplayChanged()
{
    if( chargeShown() ) m_gauge->refresh();
    else                m_gauge->setVisible( false );
    update();
}

double CapacitorBase::capVolt()
{
    if( !m_pin[0]->isConnected() || !m_pin[1]->isConnected() ) return 0;
    return m_pin[0]->getVoltage() - m_pin[1]->getVoltage();
}

double CapacitorBase::chargeLevel()
{
    double ref = ChargeView::self()->vref();
    if( ref <= 0 ) return 0;
    return qAbs( capVolt() )/ref;
}

QStringList CapacitorBase::chargeFlags()
{
    QStringList flags;
    double v = capVolt();
    if( m_maxVolt > 0 && qAbs( v ) > m_maxVolt ) flags << "overvoltage";
    if( isReversed() )                         flags << "reversed";
    if( chargeLevel() > 1 )                    flags << "overflow";
    return flags;
}

void CapacitorBase::setPlatePen( QPainter* p, int side )
{
    if( !chargeShown() ) return;
    QPen pen = p->pen();
    pen.setColor( ChargeView::self()->plateColor( m_pin[side]->getVoltage() ) );
    p->setPen( pen );
}

//--- ChargeGauge -----------------------------------------------------------

ChargeGauge::ChargeGauge( CapacitorBase* cap )
           : QGraphicsItem( cap )
{
    m_cap = cap;
    setAcceptedMouseButtons( Qt::NoButton );
    setAcceptHoverEvents( false );
    setZValue( 1 );
}

void ChargeGauge::refresh()
{
    // Undo the parent's rotation and flip: draw in scene-aligned coordinates
    QTransform st = m_cap->sceneTransform();
    QTransform lin( st.m11(), st.m12(), st.m21(), st.m22(), 0, 0 );
    bool ok = false;
    QTransform inv = lin.inverted( &ok );
    if( ok && inv != transform() ) setTransform( inv );

    // Leads along screen x: horizontal part
    QPointF lead = lin.map( QPointF( 1, 0 ) );
    bool horizontal = qAbs( lead.x() ) > qAbs( lead.y() );

    // Side opposite the value label (if shown), else right / above
    int side = 1;
    Label* vl = m_cap->m_valLabel;
    if( vl && vl->isVisible() && !vl->toPlainText().isEmpty() )
    {
        QPointF lc = vl->mapToScene( vl->boundingRect().center() ) - m_cap->scenePos();
        if( horizontal ) side = ( lc.y() < 0 ) ? -1 : 1;   // Label above: gauge below
        else             side = ( lc.x() > 0 ) ? -1 : 1;   // Label right: gauge left
    }
    QRectF bar;
    if( horizontal ) bar = ( side > 0 ) ? QRectF(-1.5,-23, 3, 14 ) : QRectF(-1.5, 9, 3, 14 );
    else             bar = ( side > 0 ) ? QRectF( 9, -7, 3, 14 )   : QRectF(-12, -7, 3, 14 );

    // Label text
    ChargeView* cv = ChargeView::self();
    QString text;
    int mode = cv->labelMode();
    if( mode != ChargeView::labelOff )
    {
        double v = m_cap->capVolt();
        double lvl = m_cap->chargeLevel();
        int pct = qRound( lvl*100 );
        bool zero = qAbs( v ) < 1e-6;
        QString pctStr = ( !zero && pct < 1 ) ? "<1 %" : QString::number( zero ? 0 : pct )+" %";
        if     ( mode == ChargeView::labelVolt    ) text = ChargeView::voltStr( v );
        else if( mode == ChargeView::labelPercent ) text = pctStr;
        else                                        text = ChargeView::voltStr( v )+" · "+pctStr;
    }
    QFont font = m_cap->m_valLabel->font();
    font.setPixelSize( 7 );
    QFontMetricsF fm( font );
    double tw = text.isEmpty() ? 0 : fm.horizontalAdvance( text );
    QPointF tp;
    if( horizontal ) tp = QPointF( bar.right()+2, bar.center().y()+fm.ascent()/2-1 );
    else if( side > 0 ) tp = QPointF( bar.right()+2, bar.center().y()+fm.ascent()/2-1 );
    else                tp = QPointF( bar.left()-2-tw, bar.center().y()+fm.ascent()/2-1 );

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
    double ref = cv->vref();
    double lvl = m_cap->chargeLevel();
    double maxV = m_cap->maxVolt();
    bool over = maxV > 0 && qAbs( m_cap->capVolt() ) > maxV;
    bool warn = over || m_cap->isReversed();

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
    if( maxV > 0 && ref > 0 && maxV < ref )
    {
        double y = m_bar.bottom() - maxV/ref*m_bar.height();
        p->setPen( QPen( QColor( 230, 40, 40 ), 0.8 ) );
        p->drawLine( QPointF( m_bar.left()-1, y ), QPointF( m_bar.right()+1, y ) );
    }
    // Label
    if( !m_text.isEmpty() )
    {
        QFont font = m_cap->m_valLabel->font();
        font.setPixelSize( 7 );
        p->setFont( font );
        p->setPen( warn ? QColor( 230, 40, 40 ) : Theme::valLabel() );
        p->drawText( m_textPos, m_text );
    }
}
