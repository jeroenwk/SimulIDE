/***************************************************************************
 *   Copyright (C) 2022 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef HEADER_H
#define HEADER_H

#include "connbase.h"

class LibraryItem;

class Header : public ConnBase
{
    public:
        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( Header )
        Header( QString type, QString id );
        ~Header();

 static Component* construct( QString type, QString id );
 static LibraryItem* libraryItem();

        bool pointDown() { return m_pointDown; }
        void setPointDown( bool down );

        virtual void updatePins() override;

    private:
        virtual void updatePixmap() override;

        bool m_pointDown;
};

#endif
