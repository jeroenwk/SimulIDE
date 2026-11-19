/***************************************************************************
 *   Keyboard input: pin geometry for building chains of parts             *
 ***( see copyright.txt file at root folder )*******************************/

#include <vector>

#include "pinchain.h"
#include "component.h"
#include "pin.h"

int PinChain::cwQuarter( QString side )
{ return side == "up" ? 0 : side == "right" ? 1 : side == "down" ? 2 : 3; }

QPointF PinChain::dir( Pin* pin )
{
    QString s = pin->side();
    if( s == "right" ) return QPointF( 1, 0 );
    if( s == "left"  ) return QPointF(-1, 0 );
    if( s == "down"  ) return QPointF( 0, 1 );
    return QPointF( 0,-1 );
}

Pin* PinChain::nearest( Component* comp, QPointF to )
{
    Pin* near = nullptr;
    double best = 1e300;
    for( Pin* q : comp->pinsClockwise() )
    {
        QPointF d = q->scenePos()-to;
        double dd = QPointF::dotProduct( d, d );
        if( dd < best-1e-9 ){ best = dd; near = q; }
    }
    return near;
}

Pin* PinChain::opposite( Component* comp, Pin* sel )
{
    QPointF d0 = dir( sel );
    Pin* best = nullptr;
    double bestDot = 0, bestDown = 0, bestDist = 0;
    for( Pin* p : comp->pinsClockwise() )
    {
        if( p == sel ) continue;
        QPointF d = dir( p );
        double dot  = QPointF::dotProduct( d, d0 );       // -1 = most opposite
        double down = d.y();                              // +1: points down
        QPointF dv  = p->scenePos()-sel->scenePos();
        double dist = QPointF::dotProduct( dv, dv );
        bool better = false;
        if( !best || dot < bestDot ) better = true;       // More opposite
        else if( dot == bestDot )
        {
            if( down > bestDown ) better = true;          // Ties: pointing more downwards
            else if( down == bestDown && dist < bestDist ) better = true;   // Then the nearest
        }
        if( better ){ best = p; bestDot = dot; bestDown = down; bestDist = dist; }
    }
    return best;
}

void PinChain::faceBack( Component* comp, QPointF outDir )
{
    std::vector<Pin*> pins = comp->getPins();
    if( pins.empty() || !pins.front() ) return;

    QString target;                          // The side that faces the pin: opposite of outDir
    if     ( outDir.x() > 0 ) target = "left";
    else if( outDir.x() < 0 ) target = "right";
    else if( outDir.y() > 0 ) target = "up";
    else                      target = "down";

    int q = ( cwQuarter( target ) - cwQuarter( pins.front()->side() ) + 4 ) % 4;
    if( q >= 2 )
    {   // The same orientation, reached as a flip plus a 0/90° rotation instead of
        q = ( q+2 ) % 4;                     // 180/270°: a label counter-flips a flip, so it
        if( target == "left" || target == "right" ) // stays upright. The flip mirrors the
             comp->setHflip( -1 );           // side the rotation lands on
        else comp->setVflip( -1 );
    }
    comp->setRotation( q*90 );
}

QPointF PinChain::leavePos( Component* comp, bool opposite, double step, Pin** used )
{
    if( used ) *used = nullptr;
    Pin* pin = comp->selectedPin();
    if( !pin ) return comp->pos() + QPointF( step, 0 );   // No pins: right of it (D12)

    if( opposite )
        if( Pin* op = PinChain::opposite( comp, pin ) ) pin = op;
    if( used ) *used = pin;
    return pin->scenePos() + dir( pin )*step;
}
