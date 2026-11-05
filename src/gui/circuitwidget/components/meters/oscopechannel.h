/***************************************************************************
 *   Copyright (C) 2020 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef OSCOPECHANNEL_H
#define OSCOPECHANNEL_H

#include "datachannel.h"
#include "oscope.h"

class OscopeChannel : public DataChannel
{
        friend class Oscope;

    public:
        OscopeChannel( Oscope* oscope, QString id );
        ~OscopeChannel();

        virtual void initialize() override;
        virtual void updateStep() override;
        virtual void voltChanged() override;

        virtual void setFilter( double f ) override;

        // Fixed trigger level: forget the edges found (level or mode changed)
        void resetTrig() { m_trigReset = true; }
        // Latest rising crossing of the trigger level at or before t
        bool trigEdgeBefore( uint64_t t, uint64_t* edge );

    private:
        void updateValues();
        void findEdges( double data, uint64_t simTime );
        void findTrigEdge( double data, uint64_t simTime );

        static const int TRIG_EDGES = 64;
        uint64_t m_trigEdges[TRIG_EDGES]; // Ring of crossings of the fixed level, at least 1/16 screen apart
        int      m_trigCount;             // Crossings stored (index of the next one, modulo TRIG_EDGES)
        bool     m_trigArmed;             // Went below level - hysteresis since the last crossing
        bool     m_trigReset;
        double   m_trigPrevData;          // Previous settled step, to place the crossing between steps
        uint64_t m_trigPrevTime;

        double m_stepData;       // Latest value in the current time step (settled at its end)
        int    m_noWave;         // Frames without a wave

        uint64_t m_totalP;
        uint64_t m_lastMax;
        uint64_t m_numMax;       // Number of Maximum found
        uint64_t m_nCycles;
        uint64_t m_simTime;
        uint64_t m_slotStart;    // Time the current buffer slot was opened (decimation)

        double m_lastValue;
        double m_freq;
        double m_mid;

        double m_maxVal;
        double m_minVal;
        double m_midVal;
        double m_dispMax;
        double m_dispMin;
        double m_ampli;
        double m_filter;

        Oscope* m_oscope;
};

#endif

