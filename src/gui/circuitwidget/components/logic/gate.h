/***************************************************************************
 *   Copyright (C) 2010 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef GATE_H
#define GATE_H

#include "logiccomponent.h"

class IoPin;

class Gate : public LogicComponent
{
    public:
        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( Gate )
        Gate( QString type, QString id, int inputs );
        ~Gate();

        QList<ComProperty*> outputProps();

        virtual void stamp() override;
        virtual void voltChanged() override;
        virtual void startSettled() override;
        virtual void runEvent() override { IoComponent::runOutputs(); }

        bool initHigh() { return m_initState; }
        void setInitHigh( bool s) { m_initState = s; }

        void setNumInputs( int pins );

    protected:
        virtual bool calcOutput( int inputs );

        bool m_initState;
        bool m_settling;     // Waiting for its turn in the start settle: inputs not followed yet

        int m_minInputs;
};

#endif
