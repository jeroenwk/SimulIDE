/***************************************************************************
 *   Copyright (C) 2020 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef THERMISTORBASE_H
#define THERMISTORBASE_H

#include "varresbase.h"

class ThermistorBase : public VarResBase
{
    public:
        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( ThermistorBase )
        ThermistorBase( QString type, QString id );
        ~ThermistorBase();

        bool ptc()  { return m_ptc; }
        void setPtc( bool ptc );

        virtual void setMaxVal( double max ) override;
        virtual void setMinVal( double min ) override;

    protected:
        bool m_ptc;
};

#endif
