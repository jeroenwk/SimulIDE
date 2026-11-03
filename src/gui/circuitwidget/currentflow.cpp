/***************************************************************************
 *   Current flow: currents through the wires, shown as moving dots       *
 *   See specs/current-flow.md                                             *
 ***( see copyright.txt file at root folder )*******************************/

#include <QtMath>
#include <QHash>
#include <QSet>
#include <QSettings>
#include <QRegularExpression>
#include <QDebug>

#include "currentflow.h"
#include "mainwindow.h"
#include "circuit.h"
#include "circuitwidget.h"
#include "simulator.h"
#include "connector.h"
#include "connectorline.h"
#include "node.h"
#include "pin.h"

double CurrentFlow::m_range = 2;

CurrentFlow* CurrentFlow::self()
{
    static CurrentFlow* flow = new CurrentFlow();
    return flow;
}

CurrentFlow::CurrentFlow()
           : Updatable()
{
    QSettings* s = MainWindow::self()->settings();
    m_enabled   = s->value("Circuit/currentFlow", false ).toBool();
    m_threshold = s->value("Circuit/currentMin", 1e-6 ).toDouble();
    if( m_threshold <= 0 ) m_threshold = 1e-6;
    m_range = qBound( 1.0, s->value("Circuit/currentRange", 2.0 ).toDouble(), 6.0 );
    m_shown  = false;
    m_scale  = 0;
    m_lastNs = 0;
    m_clock.start();
}

void CurrentFlow::setEnabled( bool en )
{
    if( en == m_enabled ) return;
    m_enabled = en;
    CircuitWidget::self()->syncCurrentFlow( en );     // Toolbar button
    MainWindow::self()->settings()->setValue("Circuit/currentFlow", en );
    if( en ) attach();
    else     clear();
}

void CurrentFlow::setThreshold( double amps )
{
    if( amps <= 0 ) return;
    m_threshold = amps;
    MainWindow::self()->settings()->setValue("Circuit/currentMin", amps );
}

void CurrentFlow::setRange( double decades )
{
    m_range = qBound( 1.0, decades, 6.0 );
    MainWindow::self()->settings()->setValue("Circuit/currentRange", m_range );
}

void CurrentFlow::attach()
{
    if( Simulator::self() ) Simulator::self()->addToUpdateList( this ); // No-op if listed
    m_lastNs = m_clock.nsecsElapsed();
}

void CurrentFlow::clear()
{
    for( Connector* con : *Circuit::self()->conList() )
    {
        bool had = qAbs( con->flowCurrent ) > 0;
        con->flowCurrent = 0;
        con->flowFlag    = 0;
        if( had ) for( ConnectorLine* line : *con->lineList() ) line->update();
    }
    m_shown = false;
    m_scale = 0;
}

void CurrentFlow::whenIdle( std::function<void()> fn )
{
    attach();
    m_pending.append( fn );
}

void CurrentFlow::updateStep()
{
    if( !m_enabled && m_pending.isEmpty() ) return;

    qint64 now = m_clock.nsecsElapsed();
    double dt  = ( now-m_lastNs )*1e-9;
    m_lastNs   = now;

    Simulator* sim = Simulator::self();
    // Stopped: nothing to compute. At 1 ps (just started, before the first step)
    // nothing is solved yet: sources would show their stamp (9 V * 1e9 S = 9 GA)
    if( !sim || !sim->isRunning() || sim->circTime() <= 1 )
    {
        if( m_shown ) clear();
        QList<std::function<void()>> pending = m_pending;
        m_pending.clear();
        for( auto& fn : pending ) fn();
        return;
    }
    solve();

    QList<std::function<void()>> pending = m_pending;
    m_pending.clear();
    for( auto& fn : pending ) fn();

    if( !m_enabled ) return;
    bool moving = sim->simState() >= SIM_WAITING;      // Paused: dots stay where they are
    updateScale( moving ? dt : 0 );
    advance( moving ? dt : 0 );
}

void CurrentFlow::advance( double dt )
{
    double maxStep = 0.45*dotSpacing;                  // Never jump more than half a spacing per frame
    for( Connector* con : *Circuit::self()->conList() )
    {
        double v = speed( con->flowCurrent, m_threshold, m_scale );
        bool active = ( v != 0 );
        if( active )
        {
            double step = v*dt;
            if( step >  maxStep ) step =  maxStep;
            if( step < -maxStep ) step = -maxStep;
            con->flowPhase = fmod( con->flowPhase+step, dotSpacing );
            if( con->flowPhase < 0 ) con->flowPhase += dotSpacing;
        }
        if( active || con->flowShown )                 // Moving, or dots to remove
            for( ConnectorLine* line : *con->lineList() ) line->update();
        con->flowShown = active;
    }
    m_shown = true;
}

// Wire currents for every net drawn on screen.
// Vertices: component pins (leaves, with the current their element injects) and
// junctions (Node, 3 pins at one point). Edges: connectors. In each connected piece
// the current through a tree edge is the sum of the injections below it.
void CurrentFlow::solve()
{
    struct Edge { Connector* con; void* to; bool fromStart; };
    QHash<void*, QList<Edge>> adj;
    QHash<void*, Pin*> leaf;                           // Vertex -> component pin

    auto vertex = [&]( Pin* p ) -> void* {
        Component* comp = p->component();
        if( comp && dynamic_cast<Node*>( comp ) ) return comp;
        leaf[ p ] = p;
        return p;
    };
    QList<Connector*>* conList = Circuit::self()->conList();
    for( Connector* con : *conList )
    {
        con->flowCurrent = 0;
        con->flowFlag    = 0;
        Pin* sp = con->startPin();
        Pin* ep = con->endPin();
        if( !sp || !ep || sp->isBus() ) continue;
        void* a = vertex( sp );
        void* b = vertex( ep );
        adj[ a ].append( { con, b, true } );
        adj[ b ].append( { con, a, false } );
    }
    QSet<void*> visited;
    for( auto it = adj.begin(); it != adj.end(); ++it )
    {
        if( visited.contains( it.key() ) ) continue;

        // Collect this piece
        QList<void*> piece{ it.key() };
        visited.insert( it.key() );
        for( int i=0; i<piece.size(); ++i )
            for( const Edge& e : adj.value( piece[i] ) )
                if( !visited.contains( e.to ) ) { visited.insert( e.to ); piece.append( e.to ); }

        // Injections into the net: -I(pin). Passive pins take what is missing.
        QHash<void*, double> inj;
        QList<void*> passive;
        double sum = 0, maxJ = 0;
        for( void* v : piece )
        {
            Pin* p = leaf.value( v );
            if( !p ) continue;
            eNode* enode = p->getEnode();
            if( !enode || !enode->hasStamps( p ) ) { passive.append( v ); continue; }
            double j = -p->getCurrent();      // True current if the model published one
            inj[ v ] = j;
            sum += j;
            maxJ = qMax( maxJ, qAbs( j ) );
        }
        int flag = 0;
        if( !passive.isEmpty() )
        {
            // The residual flows through the pins that stamped nothing (tunnels,
            // subcircuit package pins, comparator inputs): it is their current
            for( void* v : passive ) inj[ v ] = -sum/passive.size();
            if( passive.size() > 1 && qAbs( sum ) > 0.01*maxJ+1e-9 ) flag = 1;   // Guessed split, and it matters
        }
        else if( qAbs( sum ) > 0.03*maxJ+1e-8 )
            flag = 2;    // Large imbalance with no pin to take it: bug signal
        else if( sum != 0 )
        {
            // Small imbalance: currents published by element models (eBJT) lag the
            // final solve by one non-linear iteration. Spread it over the pins.
            double share = sum/piece.size();
            for( void* v : piece ) inj[ v ] -= share;
        }

        // Spanning tree from a root (a passive pin if any), then sums from the leaves up
        void* root = passive.isEmpty() ? piece.first() : passive.first();
        QHash<void*, Edge> parent;                     // Vertex -> edge to its parent
        QList<void*> order{ root };
        QSet<void*> seen{ root };
        for( int i=0; i<order.size(); ++i )
            for( const Edge& e : adj.value( order[i] ) )
                if( !seen.contains( e.to ) ) {
                    seen.insert( e.to );
                    parent[ e.to ] = { e.con, order[i], !e.fromStart };   // fromStart: seen from the child
                    order.append( e.to );
                }
        QHash<void*, double> subtree;
        for( int i=order.size()-1; i>0; --i )
        {
            void* v = order[i];
            double s = subtree.value( v ) + inj.value( v );
            Edge& pe = parent[ v ];
            // s flows from v up to its parent: start->end if v is the connector's start
            pe.con->flowCurrent = pe.fromStart ? s : -s;
            subtree[ pe.to ] += s;
        }
        for( const Edge& e : adj.value( root ) ) e.con->flowFlag = flag;
        for( void* v : piece ) for( const Edge& e : adj.value( v ) ) e.con->flowFlag = flag;
    }
}

void CurrentFlow::updateScale( double dt )
{
    double maxI = 0;
    for( Connector* con : *Circuit::self()->conList() )
        if( con->flowFlag != 2 )             // Inconsistent currents are not a measure
            maxI = qMax( maxI, qAbs( con->flowCurrent ) );

    if( maxI >= m_scale ) m_scale = maxI;                // Rise at once
    else m_scale = qMax( maxI, m_scale*exp( -dt ) );    // Fall in about 1 s (time constant)
}

// Position of a current in the shown range: 1 for the largest current, 0 for
// 10^-range of it or less (but still above the threshold), -1 below the threshold
double CurrentFlow::level( double amps, double threshold, double scale )
{
    double a = qAbs( amps );
    if( a < threshold || threshold <= 0 ) return -1;
    if( scale < a ) scale = a;
    double l = 1 + log10( a/scale )/m_range;
    return qBound( 0.0, l, 1.0 );
}

// Brighter dots for more current, like EveryCircuit
double CurrentFlow::opacity( double amps, double threshold, double scale )
{
    double l = level( amps, threshold, scale );
    if( l < 0 ) return 0;
    return 0.15 + 0.85*l;
}

double CurrentFlow::speed( double amps, double threshold, double scale )
{
    double l = level( amps, threshold, scale );
    if( l < 0 ) return 0;
    double v = 4 + 60*l;                               // 4 px/s crawl .. 64 px/s for the largest
    return amps > 0 ? v : -v;
}

bool CurrentFlow::parseCurrent( QString s, double& amps )
{
    static QRegularExpression re("^\\s*([-+]?[0-9]*\\.?[0-9]+(?:[eE][-+]?[0-9]+)?)\\s*([pnuµm]?)\\s*A?\\s*$");
    QRegularExpressionMatch m = re.match( s );
    if( !m.hasMatch() ) return false;
    double v = m.captured( 1 ).toDouble();
    QString pre = m.captured( 2 );
    if     ( pre == "p" ) v *= 1e-12;
    else if( pre == "n" ) v *= 1e-9;
    else if( pre == "u" || pre == "µ" ) v *= 1e-6;
    else if( pre == "m" ) v *= 1e-3;
    amps = v;
    return true;
}

QString CurrentFlow::currentStr( double amps )
{
    double a = qAbs( amps );
    QString unit = "A";
    double v = amps;
    if     ( a == 0    ) { v = 0; }
    else if( a < 1e-9  ) { v = amps*1e12; unit = "pA"; }
    else if( a < 1e-6  ) { v = amps*1e9;  unit = "nA"; }
    else if( a < 1e-3  ) { v = amps*1e6;  unit = "µA"; }
    else if( a < 1     ) { v = amps*1e3;  unit = "mA"; }
    return QString::number( v, 'g', 4 )+" "+unit;
}
