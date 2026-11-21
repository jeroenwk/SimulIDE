/***************************************************************************
 *   Copyright (C) 2023 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef MCS65INTERFACE_H
#define MCS65INTERFACE_H

#include "cpubase.h"
#include "e-element.h"

class Mcs65Interface : public CpuBase, public eElement
{
    public:
        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( Mcs65Interface )
        Mcs65Interface( eMcu* mcu );
        ~Mcs65Interface();

        virtual int getCpuReg( QString reg ) override;


    protected:
        QString getStrInst( uint8_t IR );
};

#endif
