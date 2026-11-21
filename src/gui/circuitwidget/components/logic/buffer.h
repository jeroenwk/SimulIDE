/***************************************************************************
 *   Copyright (C) 2010 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef BUFFER_H
#define BUFFER_H

#include "gate.h"

class LibraryItem;

class Buffer : public Gate
{
    public:
        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( Buffer )
        Buffer( QString type, QString id );
        ~Buffer();

        static Component* construct( QString type, QString id );
        static LibraryItem *libraryItem();

        virtual void setTristate( bool t ) override;

        virtual QPainterPath shape() const;
        virtual void paint( QPainter* p, const QStyleOptionGraphicsItem* option, QWidget* widget );
};

#endif
