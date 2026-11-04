/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef EELEMENT_H
#define EELEMENT_H

#include <vector>
#include <QString>

class ePin;

class eElement
{
    public:
        eElement( QString id );
        virtual ~eElement();

        virtual void initialize(){;}
        virtual void stamp(){;}

        virtual void runEvent(){;}
        virtual void voltChanged(){;}

        // Called once a time step has converged, with its settled node voltages, if the element
        // asked for it during that step (Simulator::addToSettled, e.g. from voltChanged()). Part
        // states are taken here: the values inside the non-linear iterations are not final
        virtual void stepSettled(){;}

        virtual void setNumEpins( int n );

        virtual ePin* getEpin( int num );
        virtual void setEpin( int num, ePin* pin );

        QString getId(){ return m_elmId; }

        void pauseEvents();
        void resumeEvents();

        static constexpr double cero_doub = 1e-9;
        static constexpr double high_imp  = 1e9;

        // Simulator engine
        eElement* nextChanged;
        bool added;

        eElement* nextEvent;
        uint64_t eventTime;

        eElement* nextSettled = nullptr;
        uint64_t  settledGen  = 0;     // Simulator::m_settledGen when last added

    protected:
        uint64_t m_pendingTime;

        std::vector<ePin*> m_ePin;

        QString m_elmId;

        bool m_changed;
        double m_step;
};
#endif
