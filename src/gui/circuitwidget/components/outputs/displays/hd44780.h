/***************************************************************************
 *   Copyright (C) 2016 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef HD44780_H
#define HD44780_H

#include "hd44780_base.h"
#include "e-element.h"

class IoPin;
class LibraryItem;

class Hd44780 : public Hd44780_Base, public eElement
{
    public:
        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( Hd44780 )
        Hd44780( QString type, QString id );
        ~Hd44780();
        
        static Component* construct( QString type, QString id );
        static LibraryItem* libraryItem();

        virtual void initialize() override;
        virtual void stamp() override;
        virtual void updateStep() override;
        virtual void voltChanged() override;

        void showPins( bool show );

    private:
        void readData();
        void readBusy();

        void initPuPin( int n, QString l, IoPin* pin );
        IoPin* m_pinRS;
        IoPin* m_pinRW;
        IoPin* m_pinEn;
        std::vector<IoPin*> m_dataPin;
};

#endif
