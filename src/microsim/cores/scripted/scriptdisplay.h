/***************************************************************************
 *   Copyright (C) 2023 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef SCRIPTDISPLAY_H
#define SCRIPTDISPLAY_H

#include "scriptperif.h"
#include "display.h"

class asIScriptFunction;

class ScriptDisplay : public Display, public ScriptPerif
{
    public:
        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( ScriptDisplay )
        ScriptDisplay( int width, int height, QString name, QWidget* parent );
        ~ScriptDisplay();

        virtual void initialize() override;

        virtual void registerScript( ScriptCpu* cpu ) override;
        virtual void startScript() override;

        void setData( CScriptArray* d );
        void setPalette( CScriptArray* p );

    private:

        asIScriptFunction* m_clear;

        std::vector<int> m_palette;

 static void registerScriptMetods( asIScriptEngine* engine );
};

#endif
