/***************************************************************************
 *   Copyright (C) 2021 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef PICCOMPARATOR_H
#define PICCOMPARATOR_H

#include "mcucomparator.h"

class PicComp : public McuComp
{
    public:
        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( PicComp )
        PicComp( eMcu* mcu, QString name );
        ~PicComp();

        virtual void initialize() override;
        virtual void voltChanged() override;

 static PicComp* createComparator( eMcu* mcu, QString name, int type );

    protected:
        void connect( McuPin* pinN, McuPin* pinP=NULL, McuPin* pinOut=NULL );

        bool m_inv;

        regBits_t m_COUT;
        regBits_t m_CINV;
};

class PicComp0 : public PicComp
{
    public:
        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( PicComp0 )
        PicComp0( eMcu* mcu, QString name );
        ~PicComp0();

        virtual void configureA( uint8_t newCMCON ) override;

    protected:
        bool m_cis;

        regBits_t m_CM;
        regBits_t m_CIS;
};

class PicComp01 : public PicComp0
{
    public:
        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( PicComp01 )
        PicComp01( eMcu* mcu, QString name );
        ~PicComp01();

    protected:
        virtual void setMode( uint8_t mode );
};

class PicComp02 : public PicComp0
{
    public:
        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( PicComp02 )
        PicComp02( eMcu* mcu, QString name );
        ~PicComp02();

    protected:
        virtual void setMode( uint8_t mode );
};

class PicComp03 : public PicComp0
{
    public:
        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( PicComp03 )
        PicComp03( eMcu* mcu, QString name );
        ~PicComp03();

    protected:
        virtual void setMode( uint8_t mode );
};


class PicComp1 : public PicComp
{
    public:
        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( PicComp1 )
        PicComp1( eMcu* mcu, QString name );
        ~PicComp1();
        virtual void configureA( uint8_t newCMxCON0 ) override;

    protected:
        regBits_t m_CxON;
        regBits_t m_CxOE;
        regBits_t m_CxR;
        regBits_t m_CxCH;
};

class PicComp11 : public PicComp1
{
    public:
        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( PicComp11 )
        PicComp11( eMcu* mcu, QString name );
        ~PicComp11();

        virtual void configureC( uint8_t newSRCON ) override;

    protected:
};

class PicComp12 : public PicComp1
{
    public:
        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( PicComp12 )
        PicComp12( eMcu* mcu, QString name );
        ~PicComp12();

        virtual void configureB( uint8_t newCM2CON1 ) override;
        virtual void configureC( uint8_t newSRCON ) override;

    protected:
};

class PicComp20 : public PicComp
{
    public:
        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( PicComp20 )
        PicComp20( eMcu* mcu, QString name );
        ~PicComp20();

        virtual void configureA( uint8_t newCMxCON0 ) override;
        virtual void configureB( uint8_t newCMxCON1 ) override;

    protected:
};
#endif
