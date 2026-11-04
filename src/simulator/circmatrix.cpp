/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <iostream>
#include <QtMath>
#include <QHash>
//#include <iomanip> // setw()

#include "circmatrix.h"
#include "simulator.h"

CircMatrix* CircMatrix::m_pSelf = 0l;

CircMatrix::CircMatrix()
{
    m_pSelf = this;
    m_numEnodes = 0;
}
CircMatrix::~CircMatrix(){}

void CircMatrix::createMatrix( QList<eNode*> &eNodeList )
{
    m_eNodeList = &eNodeList;
    m_numEnodes = eNodeList.size();

    m_circMatrix.clear();
    m_coefVect.clear();

    m_circMatrix.resize( m_numEnodes , d_vector_t( m_numEnodes , 0 ) );
    m_coefVect.resize( m_numEnodes , 0 );

    /// qDebug() <<"\n  Initializing Matrix: "<< m_numEnodes << " eNodes";
    analyze();
}

void CircMatrix::addConnections( int enodNum, QList<int>* nodeGroup, QList<int>* allNodes )
{
    nodeGroup->append( enodNum );
    allNodes->removeOne( enodNum );

    eNode* enod = m_eNodeList->at( enodNum-1 );
    enod->setSingle( false );

    QList<int> cons = enod->getConnections();

    for( int nodeNum : cons )
    {
        if( nodeNum == 0 ) continue;
        if( !nodeGroup->contains( nodeNum ) ) addConnections( nodeNum, nodeGroup, allNodes );
    }
}

void CircMatrix::analyze()
{
    QList<int> allNodes;
    for( int i=0; i<m_numEnodes; i++ ) allNodes.append( i+1 );

    m_aList.clear();
    m_aFaList.clear();
    m_bList.clear();
    m_eNodeActList.clear();
    m_lowerList.clear();
    m_upperList.clear();
    int group = 0;
    int singleNode = 0;

    while( !allNodes.isEmpty() ) // Get a list of groups of nodes interconnected
    {
        QList<int> nodeGroup;
        addConnections( allNodes.first(), &nodeGroup, &allNodes ); // Get a group of nodes interconnected
        if( nodeGroup.size() > 1 ) nodeGroup = orderGroup( nodeGroup );

        int numEnodes = nodeGroup.size();
        if( numEnodes==1 )           // Sigle nodes do by themselves
        {
            eNode* enod = m_eNodeList->at( nodeGroup[0]-1 );
            enod->setSingle( true );
            singleNode++;
        }else{
            dp_matrix_t a;
            d_matrix_t ap;
            dp_vector_t b;
            QList<eNode*> eNodeActive;

            a.resize( numEnodes , dp_vector_t( numEnodes , 0 ) );
            ap.resize( numEnodes , d_vector_t( numEnodes , 0 ) );
            b.resize( numEnodes , 0 );

            for( int ny=0; ny<numEnodes; ++ny )   // Copy data to reduced Matrix, in elimination order
            {
                int y = nodeGroup[ny]-1;
                for( int nx=0; nx<numEnodes; ++nx )
                {
                    int x = nodeGroup[nx]-1;
                    a[nx][ny] = &(m_circMatrix[x][y]);
                }
                b[ny] = &(m_coefVect[y]);
                eNode* node = m_eNodeList->at(y);
                node->setNodeGroup( group );
                eNodeActive.append( node );
            }
            symbolic( nodeGroup );
            m_aList.append( a );
            m_aFaList.append( ap );
            m_bList.append( b );
            m_eNodeActList.append( eNodeActive );
            group++;
        }
    }
    m_admitChanged.resize( group, true );
    m_currChanged.resize(  group, true );
    m_factorCount.assign( group, 0 );
    m_solveCount.assign(  group, 0 );

    /// qDebug() <<"CircMatrix::solveMatrix"<<group<<"Circuits";
    /// qDebug() <<"CircMatrix::solveMatrix"<<singleNode<<"Single Nodes\n";
}

// Circuit matrices are sparse: each node connects to a few others. Eliminating the nodes in a
// good order (minimum degree) keeps the factors sparse, and the factorization below only visits
// the entries that can be nonzero. No pivoting, as before: the node admittance matrix is
// diagonally dominant, and a symmetric reordering keeps it so.
QList<int> CircMatrix::orderGroup( const QList<int>& nodeGroup )
{
    int n = nodeGroup.size();
    QHash<int,int> local;
    for( int i=0; i<n; ++i ) local[ nodeGroup[i] ] = i;

    std::vector<std::vector<bool>> adj( n, std::vector<bool>( n, false ) );
    for( int i=0; i<n; ++i )
        for( int node : m_eNodeList->at( nodeGroup[i]-1 )->getConnections() )
        {
            if( !local.contains( node ) ) continue;
            int j = local[node];
            if( j != i ) adj[i][j] = adj[j][i] = true;
        }
    std::vector<bool> done( n, false );
    QList<int> order;
    for( int step=0; step<n; ++step )        // Greedy minimum degree
    {
        int best = -1, bestDeg = n+1;
        for( int i=0; i<n; ++i )
        {
            if( done[i] ) continue;
            int deg = 0;
            for( int j=0; j<n; ++j ) if( adj[i][j] && !done[j] ) deg++;
            if( deg < bestDeg ) { bestDeg = deg; best = i; }
        }
        done[best] = true;
        order.append( nodeGroup[best] );
        std::vector<int> nb;                 // Eliminating it joins its neighbours (fill-in)
        for( int j=0; j<n; ++j ) if( adj[best][j] && !done[j] ) nb.push_back( j );
        for( int p : nb ) for( int q : nb ) if( p != q ) adj[p][q] = true;
    }
    return order;
}

void CircMatrix::symbolic( const QList<int>& nodeGroup ) // nodeGroup in elimination order
{
    int n = nodeGroup.size();
    QHash<int,int> local;
    for( int i=0; i<n; ++i ) local[ nodeGroup[i] ] = i;

    std::vector<std::vector<bool>> nz( n, std::vector<bool>( n, false ) );
    for( int i=0; i<n; ++i )                 // Structure, made symmetric
        for( int node : m_eNodeList->at( nodeGroup[i]-1 )->getConnections() )
        {
            if( !local.contains( node ) ) continue;
            int j = local[node];
            if( j != i ) nz[i][j] = nz[j][i] = true;
        }
    pattern_t lower( n ), upper( n );
    for( int k=0; k<n; ++k )                 // Elimination: fill-in among the later neighbours
    {
        for( int j=k+1; j<n; ++j ) if( nz[k][j] ) upper[k].push_back( j );
        for( int p : upper[k] ) for( int q : upper[k] ) if( p != q ) nz[p][q] = true;
    }
    for( int k=0; k<n; ++k ) for( int i : upper[k] ) lower[i].push_back( k ); // k < i, ascending
    m_lowerList.append( lower );
    m_upperList.append( upper );
}

QString CircMatrix::stats() const
{
    QStringList groups;
    for( int i=0; i<m_eNodeActList.size(); ++i )
        groups.append( QString("%1 nodes: %2 factor, %3 solve").arg( m_eNodeActList[i].size() )
                       .arg( i < (int)m_factorCount.size() ? m_factorCount[i] : 0 )
                       .arg( i < (int)m_solveCount.size()  ? m_solveCount[i]  : 0 ) );
    return groups.join("; ");
}

bool CircMatrix::solveMatrix()
{
    bool ok = true;
    for( int i=0; i<m_bList.size(); ++i )
    {
        if( !m_admitChanged[i] && !m_currChanged[i] ) continue;

        m_eNodeActive = &(m_eNodeActList[i]);
        int n = m_eNodeActive->size();

        if( m_admitChanged[i] ) { factorMatrix( n, i ); m_factorCount[i]++; }
        if( !luSolve( n, i ) ) ok = false;
        m_solveCount[i]++;

        m_currChanged[i]  = false;
        m_admitChanged[i] = false;
    }
    return ok;
}

void CircMatrix::factorMatrix( int n, int group ) // Factor matrix into Lower/Upper triangular
{
    const dp_matrix_t& ap = m_aList[group];
    d_matrix_t&         a = m_aFaList[group];
    const pattern_t& lower = m_lowerList[group];
    const pattern_t& upper = m_upperList[group];

    for( int i=0; i<n; ++i )                 // Load the admittances (fill-in positions read 0)
    {
        a[i][i] = *(ap[i][i]);
        for( int j : lower[i] ) a[i][j] = *(ap[i][j]);
        for( int j : upper[i] ) a[i][j] = *(ap[i][j]);
    }
    for( int k=0; k<n; ++k )                 // Doolittle: L unit diagonal, U holds the diagonal
    {
        double div = a[k][k];
        if( div == 0 ) continue;
        const std::vector<int>& cols = upper[k];  // Symmetric pattern: also the rows below k
        for( int i : cols )
        {
            double l = a[i][k] / div;
            a[i][k] = l;
            if( l == 0 ) continue;
            double* ai = a[i].data();
            const double* ak = a[k].data();
            for( int j : cols ) ai[j] -= l*ak[j];
        }
    }
}

bool CircMatrix::luSolve( int n, int group ) // Solves the system to get voltages for each node
{
    const d_matrix_t&  a  = m_aFaList[group];
    const dp_vector_t& bp = m_bList[group];
    const pattern_t& lower = m_lowerList[group];
    const pattern_t& upper = m_upperList[group];

    d_vector_t& b = m_work;
    b.resize( n );

    for( int i=0; i<n; ++i )                 // Forward substitution from lower triangular matrix
    {
        double tot = *(bp[i]);
        const double* ai = a[i].data();
        for( int j : lower[i] ) tot -= ai[j]*b[j];
        b[i] = tot;
    }
    bool isOk = true;

    for( int i=n-1; i>=0; --i )              // Back substitution from upper triangular matrix
    {
        double tot = b[i];
        const double* ai = a[i].data();
        for( int j : upper[i] ) tot -= ai[j]*b[j];

        double div = a[i][i];
        double volt = 0;
        if( div != 0 ) volt = tot/div;
        else isOk = false;

        b[i] = volt;
        m_eNodeActive->at(i)->setVolt( volt );         // Set Node Voltages
    }
    return isOk;
}
