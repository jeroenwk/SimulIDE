/***************************************************************************
 *   Copyright (C) 2016 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef PUSHBASE_H
#define PUSHBASE_H

#include "switch_base.h"

class PushBase : public SwitchBase
{
    public:
        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( PushBase )
        PushBase( QString type, QString id );
        ~PushBase();

        virtual void stamp() override;

    public slots:
        void onbuttonPressed();
        void onbuttonReleased();
        virtual void keyEvent( QString key, bool pressed );
};

#endif
