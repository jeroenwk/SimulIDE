/***************************************************************************
 *   Keyboard input: walking the wire network of a pin (K9)                *
 ***( see copyright.txt file at root folder )*******************************/

#include <QJsonArray>
#include <QHash>
#include <algorithm>
#include <QtMath>

#include "networkwalk.h"
#include "connectorline.h"
#include "circuit.h"
#include "connector.h"
#include "component.h"
#include "node.h"
#include "pin.h"

NetworkWalk::NetworkWalk()
{
    m_active = false;
    m_index  = 0;
    m_seed   = Step{ stepWire, QString(), -1, QPoint(), 0 };
}

QString NetworkWalk::kindName( kind_t k )
{ return k == stepWire ? "wire" : k == stepCorner ? "corner" : "dot"; }

static QList<QPoint> points( Connector* con )
{
    QStringList l = con->pointList();
    QList<QPoint> pts;
    for( int i=0; i+1<l.size(); i+=2 ) pts.append( QPoint( l[i].toInt(), l[i+1].toInt() ) );
    return pts;
}

static double segLen( QPoint a, QPoint b ) { return qAbs( b.x()-a.x() ) + qAbs( b.y()-a.y() ); }

static bool isDot( Pin* p ) { return p && p->component() && p->component()->itemType() == "Node"; }

// Dijkstra over the wires: a wire is entered at one end with a path length, its corners
// and its far end follow along its points; a dot passes the walk on to its other wires,
// a part's pin ends it. Each wire counts once, from its nearest end. Seeds: wires entered
// at a pin or dot (queue), dots reached at a length (dotSeeds)
struct Entry { Connector* con; Pin* from; double len; };

static QList<NetworkWalk::Step> walkFrom( QList<Entry> queue, QList<QPair<Node*, double>> dotSeeds )
{
    typedef NetworkWalk::Step Step;
    QList<Step> steps;
    QHash<Connector*, bool> done;
    QHash<Node*, bool> dots;

    auto expandDot = [&]( Node* node, double len ){
        if( dots.value( node ) ) return;
        dots[ node ] = true;
        steps.append( { NetworkWalk::stepDot, node->getUid(), -1,
                        node->getPin( 0 )->scenePos().toPoint(), len } );
        for( int p=0; p<3; ++p )
        {
            Pin* np = node->getPin( p );
            Connector* c = np ? np->connector() : nullptr;
            if( c && !done.value( c ) ) queue.append( { c, np, len } );
        }
    };
    for( auto& d : dotSeeds ) expandDot( d.first, d.second );

    while( !queue.isEmpty() )
    {
        int best = 0;                                    // The nearest entry first
        for( int i=1; i<queue.size(); ++i ) if( queue[i].len < queue[best].len ) best = i;
        Entry e = queue.takeAt( best );
        if( done.value( e.con ) ) continue;
        done[ e.con ] = true;

        QList<QPoint> pts = points( e.con );
        if( pts.size() < 2 ) continue;
        bool reversed = e.con->startPin() != e.from;     // Entered at its end pin
        Pin* far = reversed ? e.con->startPin() : e.con->endPin();

        QPoint entry = reversed ? pts.last() : pts.first();
        steps.append( { NetworkWalk::stepWire, e.con->getUid(), -1, entry, e.len } );

        double len = e.len;
        int n = pts.size();
        for( int k=1; k<n; ++k )                         // Along the wire from its entry
        {
            int i = reversed ? n-1-k : k;                // Point index in the point list
            int prev = reversed ? i+1 : i-1;
            len += segLen( pts[prev], pts[i] );
            if( k < n-1 ) steps.append( { NetworkWalk::stepCorner, e.con->getUid(), i, pts[i], len } );
        }
        if( isDot( far ) ) expandDot( static_cast<Node*>( far->component() ), len );
    }                                                    // A part's pin: the walk ends there
    std::stable_sort( steps.begin(), steps.end(), []( const Step& a, const Step& b ){
        if( a.len != b.len ) return a.len < b.len;
        auto rank = []( NetworkWalk::kind_t k ){ return k == NetworkWalk::stepDot ? 0 : k == NetworkWalk::stepWire ? 1 : 2; };
        if( a.kind != b.kind ) return rank( a.kind ) < rank( b.kind );   // A dot before the
                                                                         // wires leaving it
        if( a.pos.y() != b.pos.y() ) return a.pos.y() < b.pos.y();
        return a.pos.x() < b.pos.x();
    });
    return steps;
}

QList<NetworkWalk::Step> NetworkWalk::build( Pin* pin )
{
    if( !pin || !pin->connector() ) return QList<Step>();
    return walkFrom( { { pin->connector(), pin, 0 } }, {} );
}

// From a wire, corner or dot (K9c): lengths from the dot, or from the wire's start
QList<NetworkWalk::Step> NetworkWalk::buildFrom( const Step& seed )
{
    if( seed.kind == stepDot )
    {
        Node* node = dot( seed );
        return node ? walkFrom( {}, { { node, 0 } } ) : QList<Step>();
    }
    Connector* con = wire( seed );
    if( !con || !con->startPin() ) return QList<Step>();
    QList<QPair<Node*, double>> dotSeeds;
    if( isDot( con->startPin() ) ) dotSeeds.append( { static_cast<Node*>( con->startPin()->component() ), 0 } );
    return walkFrom( { { con, con->startPin(), 0 } }, dotSeeds );
}

// The wire, corner or dot nearest to p: by distance, a dot preferred to a corner and a
// corner to a wire when about as near (a dot or corner next to p beats the wire it is
// on, which is always at least as near). false: no wire in the circuit
bool NetworkWalk::nearestItem( QPointF p, Step* out )
{
    double best = 1e300;
    auto offer = [&]( kind_t kind, QString uid, int index, QPoint pos, double d ){
        double score = d + ( kind == stepDot ? 0 : kind == stepCorner ? 4 : 8 );
        if( score < best ){ best = score; *out = { kind, uid, index, pos, 0 }; }
    };
    for( Node* n : *Circuit::self()->nodeList() )
    {
        if( !n->isVisible() ) continue;
        QPointF d = n->getPin( 0 )->scenePos()-p;
        offer( stepDot, n->getUid(), -1, n->getPin( 0 )->scenePos().toPoint(), qSqrt( QPointF::dotProduct( d, d ) ) );
    }
    for( Connector* c : *Circuit::self()->conList() )
    {
        if( !c->endPin() ) continue;                     // A wire being drawn
        QList<QPoint> pts = points( c );
        for( int i=0; i+1<pts.size(); ++i )
        {
            QPointF a = pts[i], b = pts[i+1];            // Distance to the segment
            QPointF ab = b-a;
            double l2 = QPointF::dotProduct( ab, ab );
            double t = l2 > 0 ? qBound( 0.0, QPointF::dotProduct( p-a, ab )/l2, 1.0 ) : 0;
            QPointF q = a+ab*t, d = q-p;
            offer( stepWire, c->getUid(), -1, q.toPoint(), qSqrt( QPointF::dotProduct( d, d ) ) );
            if( i > 0 )
            {
                QPointF dc = a-p;
                offer( stepCorner, c->getUid(), i, pts[i], qSqrt( QPointF::dotProduct( dc, dc ) ) );
            }
        }
    }
    return best < 1e300;
}

bool NetworkWalk::nearestWire( QPointF p, Step* out, double* dist )
{
    double best = 1e300;
    for( Connector* c : *Circuit::self()->conList() )
    {
        if( !c->endPin() ) continue;                     // A wire being drawn
        if( c->lineList()->isEmpty() || !c->lineList()->first()->isVisible() ) continue;   // A hidden lead
        QList<QPoint> pts = points( c );
        for( int i=0; i+1<pts.size(); ++i )
        {
            QPointF a = pts[i], b = pts[i+1], ab = b-a;
            double l2 = QPointF::dotProduct( ab, ab );
            double t = l2 > 0 ? qBound( 0.0, QPointF::dotProduct( p-a, ab )/l2, 1.0 ) : 0;
            QPointF q = a+ab*t, d = q-p;
            double dd = qSqrt( QPointF::dotProduct( d, d ) );
            if( dd < best ){ best = dd; *out = { stepWire, c->getUid(), -1, q.toPoint(), 0 }; }
        }
    }
    *dist = best;
    return best < 1e300;
}

bool NetworkWalk::startFrom( const Step& seed )
{
    QList<Step> steps = buildFrom( seed );
    if( steps.isEmpty() ) return false;
    m_steps   = steps;
    m_pinId.clear();
    m_partUid.clear();
    m_seed    = seed;
    m_index   = 0;
    for( int i=0; i<steps.size(); ++i )                  // The seed itself selected
    {
        const Step& s = steps[i];
        if( s.kind == seed.kind && s.uid == seed.uid && ( s.kind != stepCorner || s.index == seed.index ) )
        { m_index = i; break; }
    }
    m_active  = true;
    return true;
}

bool NetworkWalk::start( Pin* pin )
{
    QList<Step> steps = build( pin );
    if( steps.isEmpty() ) return false;
    m_steps   = steps;
    m_index   = 0;                                       // The pin's own wire
    m_pinId   = pin->pinId();
    m_seed    = Step{ stepWire, QString(), -1, QPoint(), 0 };
    m_partUid = pin->component() ? pin->component()->getUid() : QString();
    m_active  = true;
    return true;
}

void NetworkWalk::stop()
{
    m_active = false;
    m_steps.clear();
    m_index = 0;
}

NetworkWalk::Step NetworkWalk::current() { return m_steps.at( m_index ); }

QStringList NetworkWalk::wireUids()
{
    QStringList uids;
    for( const Step& s : m_steps ) if( s.kind == stepWire && !uids.contains( s.uid ) ) uids.append( s.uid );
    return uids;
}

void NetworkWalk::stepBy( int dir )
{
    int n = m_steps.size();
    if( n ) m_index = ( ( m_index+dir ) % n + n ) % n;
}

bool NetworkWalk::rebuild( QPoint pos, kind_t kind )
{
    QList<Step> steps;
    if( !m_pinId.isEmpty() ) steps = build( Circuit::self()->pinById( m_pinId ) );
    else                                                 // From a seed (K9c): the same one, or
    {                                                    // when it is gone the item nearest pos
        if( !( m_seed.kind == stepDot ? (bool)dot( m_seed ) : (bool)wire( m_seed ) ) )
            if( !nearestItem( pos, &m_seed ) ) m_seed.uid.clear();
        if( !m_seed.uid.isEmpty() ) steps = buildFrom( m_seed );
    }
    m_steps = steps;
    m_index = 0;
    if( steps.isEmpty() ) return false;

    int best = -1;
    double bestD = 1e300;
    for( int pass=0; pass<2 && best < 0; ++pass )        // Same kind first, then any
        for( int i=0; i<steps.size(); ++i )
        {
            if( pass == 0 && steps[i].kind != kind ) continue;
            double d = segLen( steps[i].pos, pos );
            if( d < bestD ){ bestD = d; best = i; }
        }
    m_index = best;
    return true;
}

Connector* NetworkWalk::wire( const Step& s )
{
    if( s.kind == stepDot ) return nullptr;
    for( Connector* c : *Circuit::self()->conList() ) if( c->getUid() == s.uid ) return c;
    return nullptr;
}

Node* NetworkWalk::dot( const Step& s )
{
    if( s.kind != stepDot ) return nullptr;
    for( Node* n : *Circuit::self()->nodeList() ) if( n->getUid() == s.uid ) return n;
    return nullptr;
}

// The corner moves by delta; its two segments move with it so the wire stays H/V:
// the neighbour point shares the moved coordinate. A neighbour that is the wire's end
// (a pin or a dot) cannot move: a point is added there, as the mouse segment drag does.
// Points that coincide or line up are merged afterwards
QStringList NetworkWalk::cornerMoved( const Step& s, QPoint delta )
{
    Connector* con = wire( s );
    if( !con || s.kind != stepCorner ) return QStringList();
    QList<QPoint> pts = points( con );
    int k = s.index;
    if( k <= 0 || k >= pts.size()-1 ) return QStringList();

    QPoint old = pts[k];
    QPoint now = old+delta;
    auto follow = [&]( int nb, int& k ){                 // Keep segment (nb, k) straight
        QPoint q = pts[nb];
        bool horizontal = q.y() == old.y() && q.x() != old.x();
        bool vertical   = q.x() == old.x() && q.y() != old.y();
        if( !horizontal && !vertical ) return;           // Diagonal or null: just the corner
        if( nb == 0 || nb == pts.size()-1 )              // An end: a new point next to it
        {
            int at = ( nb == 0 ) ? 1 : nb;
            pts.insert( at, q );
            if( nb == 0 ) k++;
            nb = ( nb == 0 ) ? 1 : nb;
        }
        if( horizontal ) pts[nb].ry() = now.y();
        else             pts[nb].rx() = now.x();
    };
    follow( k-1, k );
    follow( k+1, k );
    pts[k] = now;

    for( int i=1; i<pts.size(); )                        // Merge coinciding points
    {
        if( pts[i] == pts[i-1] && i != 0 && pts.size() > 2 ) pts.removeAt( i == pts.size()-1 ? i-1 : i );
        else ++i;
    }
    for( int i=1; i+1<pts.size(); )                      // and points on a straight line
    {
        QPoint a = pts[i-1], b = pts[i], c = pts[i+1];
        bool line = ( a.x() == b.x() && b.x() == c.x() ) || ( a.y() == b.y() && b.y() == c.y() );
        if( line ) pts.removeAt( i );
        else ++i;
    }
    QStringList out;
    for( QPoint p : pts ){ out.append( QString::number( p.x() ) ); out.append( QString::number( p.y() ) ); }
    return out;
}

QJsonObject NetworkWalk::state()
{
    QJsonObject o{ {"pin", m_pinId}, {"steps", m_steps.size()}, {"index", m_index} };
    if( m_index < m_steps.size() )
    {
        Step s = m_steps.at( m_index );
        QJsonObject st{ {"kind", kindName( s.kind )}, {"uid", s.uid},
                        {"pos", QJsonArray{ s.pos.x(), s.pos.y() }}, {"len", s.len} };
        if( s.kind == stepCorner ) st["index"] = s.index;
        o["step"] = st;
    }
    QJsonArray all;
    for( const Step& s : m_steps )
        all.append( kindName( s.kind )+" "+s.uid+( s.kind == stepCorner ? "#"+QString::number( s.index ) : "" ) );
    o["walk"] = all;
    return o;
}
