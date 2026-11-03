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
    m_shown  = false;
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
    if( !sim || !sim->isRunning() )   // Stopped: nothing to compute
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
    advance( moving ? dt : 0 );
}

void CurrentFlow::advance( double dt )
{
    double maxStep = 0.45*dotSpacing;                  // Never jump more than half a spacing per frame
    for( Connector* con : *Circuit::self()->conList() )
    {
        double v = speed( con->flowCurrent, m_threshold );
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
            double j = -enode->pinCurrent( p );
            inj[ v ] = j;
            sum += j;
            maxJ = qMax( maxJ, qAbs( j ) );
        }
        int flag = 0;
        if( !passive.isEmpty() )
        {
            for( void* v : passive ) inj[ v ] = -sum/passive.size();
            if( passive.size() > 1 && qAbs( sum ) > 1e-12 ) flag = 1;     // Split guessed: approximate
        }
        else if( qAbs( sum ) > 0.01*maxJ+1e-9 ) flag = 2;               // Should not happen: inconsistent

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

double CurrentFlow::speed( double amps, double threshold )
{
    double a = qAbs( amps );
    if( a < threshold || threshold <= 0 ) return 0;
    double v = 8*( 1+log10( a/threshold ) );           // 8 px/s at the threshold, +8 per decade
    if( v > 64 ) v = 64;
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
