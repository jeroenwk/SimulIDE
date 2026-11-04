/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef CIRCMATRIX_H
#define CIRCMATRIX_H

#include <vector>
#include <QList>

#include "e-node.h"

class CircMatrix
{
    typedef std::vector<double>      d_vector_t;
    typedef std::vector<double*>     dp_vector_t;
    typedef std::vector<d_vector_t>  d_matrix_t;
    typedef std::vector<dp_vector_t> dp_matrix_t;

    public:
        CircMatrix();
        ~CircMatrix();

 static CircMatrix* self() { return m_pSelf; }

        void createMatrix( QList<eNode*> &eNodeList );
        bool solveMatrix();

        QString stats() const;            // Node groups (size, factorizations, solves) since analyze()

        inline void stampDiagonal( int group, int n, double value ){
            m_admitChanged[group] = true;
            m_circMatrix[n-1][n-1] = value;      // eNode numbers start at 1
        }
        inline void stampMatrix( int row, int col, double value ){
            m_circMatrix[row-1][col-1] = value;      // eNode numbers start at 1
        }
        inline void stampCoef( int group, int row, double value ){
            m_currChanged[group] = true;
            m_coefVect[row-1] = value;
        }

    private:
 static CircMatrix* m_pSelf;

        void analyze();
        void addConnections( int enodNum, QList<int>* nodeGroup, QList<int>* allNodes );
        QList<int> orderGroup( const QList<int>& nodeGroup );       // Elimination order: less fill-in
        void symbolic( const QList<int>& nodeGroup );                // Fill-in pattern of a group

        inline void factorMatrix( int n, int group );
        inline bool luSolve( int n, int group );

        int m_numEnodes;
        QList<eNode*>* m_eNodeList;

        QList<dp_matrix_t> m_aList;
        QList<d_matrix_t>  m_aFaList;
        QList<dp_vector_t> m_bList;

        std::vector<bool>    m_admitChanged;
        std::vector<bool>    m_currChanged;
        QList<eNode*>*       m_eNodeActive;
        QList<QList<eNode*>> m_eNodeActList;

        typedef std::vector<std::vector<int>> pattern_t;   // Per row: columns structurally nonzero
        QList<pattern_t> m_lowerList;   // j < i, in L (unit diagonal)
        QList<pattern_t> m_upperList;   // j > i, in U (also the rows below the diagonal in column i)

        std::vector<uint64_t> m_factorCount;
        std::vector<uint64_t> m_solveCount;

        d_vector_t m_work;

        d_matrix_t m_circMatrix;
        d_vector_t m_coefVect;

        //bool m_admitChanged;
        //bool m_currChanged;
};
 #endif


