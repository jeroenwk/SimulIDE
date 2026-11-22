/***************************************************************************
 *   Copyright (C) 2023 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef SCRIPTSPI_H
#define SCRIPTSPI_H

#include "scriptperif.h"
#include "mcuspi.h"

class ScriptCpu;
class asIScriptFunction;

class ScriptSpi : public McuSpi, public ScriptPerif
{
    public:
        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( ScriptSpi )
        ScriptSpi( eMcu* mcu, QString name );
        ~ScriptSpi();

        void byteReceived( uint8_t data );
        void sendByte( uint8_t data );

        virtual void reset() override;

        virtual void endTransaction() override;

        virtual void registerScript( ScriptCpu* cpu ) override;
        virtual void startScript() override;

    private:

        asIScriptFunction* m_byteReceived;
};

#endif
