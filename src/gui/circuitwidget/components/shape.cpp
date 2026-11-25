/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include "shape.h"
#include "circuit.h"
#include "simulator.h"

#include "stringprop.h"
#include "doubleprop.h"
#include "intprop.h"
#include "zorder.h"
#include "keytext.h"
#include "circuitwidget.h"

#include <QMenu>

#define tr(str) simulideTr("Shape",str)

Shape::Shape( QString type, QString id )
     : Component( type, id )
{
    m_graphical = true;
    
    m_hSize  = 50;
    m_vSize  = 30;
    m_border = 2;
    m_opac   = 1;
    m_color  = QColor( Qt::gray );
    m_area   = QRectF( -m_hSize/2, -m_vSize/2, m_hSize, m_vSize );
    setZValue( -1 );

    m_changed = true;

    Simulator::self()->addToUpdateList( this );

    addPropGroup( { tr("Main"), {
        new IntProp <Shape>("H_size", tr("Size X"), "_px"
                           , this, &Shape::hSize, &Shape::setHSize,0,"uint" ),

        new IntProp <Shape>("V_size", tr("Size Y"), "_px"
                           , this, &Shape::vSize, &Shape::setVSize,0,"uint" ),

        new IntProp <Shape>("Border", tr("Border"), "_px"
                           , this, &Shape::border, &Shape::setBorder ),

        new StrProp <Shape>("Border_Style", tr("Line Type"), BorderStyle::enums()
                           , this, &Shape::borderStyle, &Shape::setBorderStyle,0,"enum" ),

        new DoubProp<Shape>("Z_Value", tr("Z Value"), ""
                           , this, &Shape::zVal, &Shape::setZVal )
            },0} );

            addPropGroup( { tr("Color"), {
        new StrProp<Shape>("Color", tr("Color"), ""
                          , this, &Shape::colorStr, &Shape::setColorStr,0,"color" ),

        new StrProp<Shape>("Border_Color", tr("Border Color"), ""
                          , this, &Shape::borderColorStr, &Shape::setBorderColorStr,0,"color" ),

        new DoubProp<Shape>("Opacity", tr("Opacity"), ""
                           , this, &Shape::opac, &Shape::setOpac )
    },0} );
}
Shape::~Shape(){}

void Shape::updateStep()
{
    if( !m_changed ) return;
    m_changed = false;

    update();

    m_area = QRectF( -m_hSize/2, -m_vSize/2, m_hSize, m_vSize );
    Circuit::self()->update();
}

void Shape::setHSize( int size )
{
    if( size < 1 ) size = 1;
    m_hSize = size;
    changed();
}

void Shape::setVSize( int size )
{
    if( size < 1 ) size = 1;

    m_vSize = size;
    changed();
}

void Shape::setBorder( int border ) 
{ 
    if( border < 0 ) border = 0;
    m_border = border;
    changed();
}

void Shape::setBorderColor( QColor color )
{
    m_bs.setColor( color );
    changed();
}

void Shape::setBorderColorStr( QString n )
{
    if( m_bs.setColorStr( n ) ) changed();   // Not a colour (junk in the file): keep this one
}

void Shape::setBorderStyle( QString style )
{
    m_bs.setStyle( style );
    changed();
}

void Shape::setColor( QColor color )
{
    m_color = color;
    changed();
}

void Shape::setOpac( qreal op )
{
    if     ( op > 1 ) op = 1;
    else if( op < 0 ) op = 0;
    m_opac = op;
   changed();
}

void Shape::changed()
{
    m_changed = true;
    if( !Simulator::self()->isRunning() ) updateStep();
}

// ----- Corner handles (specs/gfx-arrange.md) ---------------------------------

QPointF Shape::cornerLocal( int corner )
{
    double l = -m_hSize/2, t = -m_vSize/2;                  // As m_area (integer halves)
    double r = l+m_hSize, b = t+m_vSize;
    switch( corner & 3 ){
        case 0: return QPointF( l, t );
        case 1: return QPointF( r, t );
        case 2: return QPointF( r, b );
        default: return QPointF( l, b );
    }
}

QPointF Shape::cornerScene( int corner ) { return mapToScene( cornerLocal( corner ) ); }

void Shape::setCornerScene( int corner, QPointF scenePos )
{
    corner &= 3;
    int opp = ( corner+2 ) & 3;
    QPointF oppScene = cornerScene( opp );
    QPointF p = mapFromScene( scenePos ), o = cornerLocal( opp );
    double sx = ( corner == 1 || corner == 2 ) ? 1 : -1;   // The corner's side of the opposite one
    double sy = ( corner == 2 || corner == 3 ) ? 1 : -1;
    int w = qRound( ( p.x()-o.x() )*sx );
    int h = qRound( ( p.y()-o.y() )*sy );
    if( cornerFlips() )                                     // Past the other end: mirror the part
    {
        if( w < 0 ){ setHflip(-hflip() ); w = -w; }
        if( h < 0 ){ setVflip(-vflip() ); h = -h; }
    }
    prepareGeometryChange();
    m_hSize = qMax( 1, w );
    m_vSize = qMax( 1, h );
    m_area  = QRectF( -m_hSize/2, -m_vSize/2, m_hSize, m_vSize );
    setPos( pos()+oppScene-cornerScene( opp ) );           // The opposite corner stays
    changed();
}

void Shape::saveCornerUndo()
{
    Circuit* circ = Circuit::self();
    circ->beginCircuitBatch();
    for( QString prop : { "Pos", "H_size", "V_size", "hflip", "vflip" } )
        circ->addCompChange( getUid(), prop, getPropStr( prop ) );
    circ->saveChanges();
}

void Shape::contextMenu( QGraphicsSceneContextMenuEvent* event, QMenu* menu )
{
    if( event )
    {
        for( int op : { ZOrder::toFront, ZOrder::forward, ZOrder::backward, ZOrder::toBack } )
        {
            ZOrder::op_t o = (ZOrder::op_t)op;
            QAction* a = menu->addAction( ZOrder::opName( o )+"\t"+KeyText::native( ZOrder::opKeys( o ) ) );
            QObject::connect( a, &QAction::triggered, [=](){
                if( !isSelected() ){ Circuit::self()->clearSelection(); setSelected( true ); }
                QString msg = ZOrder::apply( o, ZOrder::selectedShapes() );
                if( !msg.isEmpty() ) CircuitWidget::self()->eventMessage( msg, 0 );
            });
        }
        menu->addSeparator();
    }
    Component::contextMenu( event, menu );
}
