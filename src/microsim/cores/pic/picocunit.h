/***************************************************************************
 *   Copyright (C) 2021 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef PICOCUNIT_H
#define PICOCUNIT_H

#include "mcuocunit.h"

class PicPwmUnit;

class PicOcUnit : public McuOcUnit
{
        friend class PicCcpUnit;

    public:
        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( PicOcUnit )
        PicOcUnit( eMcu* mcu, QString name );
        ~PicOcUnit();

static PicPwmUnit* createPwmUnit( eMcu* mcu, QString name, int type );

        //virtual void initialize() override;
        virtual void runEvent();

        virtual void configure( uint8_t CCPxM ) override;
        virtual void ocrWriteL( uint8_t val ) override;
        virtual void ocrWriteH( uint8_t val ) override;

    protected:
        bool m_enhanced;
        bool m_specEvent;
        bool m_resetTimer;

        regBits_t m_GODO;
};

class PicPwmUnit : public McuOcUnit
{
        friend class PicCcpUnit;

    public:
        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( PicPwmUnit )
        PicPwmUnit( eMcu* mcu, QString name );
        ~PicPwmUnit();

        //virtual void initialize() override;
        virtual void runEvent();

        virtual void configure( uint8_t newCCPxCON ) override;

        virtual void sheduleEvents( uint32_t ovf, uint32_t countVal, int rot=0 ) override;

        virtual void ocrWriteL( uint8_t val ) override;

    protected:
        bool m_enhanced;

        uint8_t m_cLow;
        uint8_t m_CCPRxL;

        regBits_t m_PxM;
        regBits_t m_DCxB;
};

class PicPwmUnit00 : public PicPwmUnit
{
    public:
        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( PicPwmUnit00 )
        PicPwmUnit00( eMcu* mcu, QString name );
        ~PicPwmUnit00();
};

class PicPwmUnit01 : public PicPwmUnit
{
    public:
        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( PicPwmUnit01 )
        PicPwmUnit01( eMcu* mcu, QString name );
        ~PicPwmUnit01();
};

#endif
