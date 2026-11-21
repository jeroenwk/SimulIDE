/***************************************************************************
 *   Copyright (C) 2023 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef COREBASE_H
#define COREBASE_H

#include <QStringList>
#include <QHash>

#include "simstate.h"

class Display;

class CoreBase
{
    public:
        // Keyframes (specs/sim-control.md §4.5): the core's own fields; its registers are in the
        // MCU's data memory, saved by eMcu
        virtual void coreState( StateIO& ) {}

        CoreBase();
        virtual ~CoreBase();

        virtual void reset(){;}
        virtual void runStep(){;}
        virtual void extClock( bool clkState ){;}
        virtual void updateStep(){;}

        virtual void command( QString c ){;}

        virtual int getCpuReg( QString reg );
        virtual QString getStrReg( QString ){ return "";}

    protected:
        QHash<QString, uint8_t*> m_cpuRegs;

        Display* m_display; // Find a place for this
};

#endif
