/***************************************************************************
 *   Copyright (C) 2022 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef SCRIPTMODULE_H
#define SCRIPTMODULE_H

#include "angelscript.h"
#include "as_jit.h"

//#include "mcumodule.h"
#include "e-element.h"


class ScriptModule : public eElement
{
    public:
        ScriptModule( QString name );
        ~ScriptModule();

        // Keyframes (specs/sim-control.md §4.5): the script's global variables, read or written
        // through AngelScript's module API: numbers, bools, enums and strings; handles (IoPin@)
        // are left as they are (set once at load). unsupportedGlobals(): "name (type)" of the
        // others (arrays, objects): with any, the part has no history
        void scriptGlobals( StateIO& s );
        QStringList unsupportedGlobals();

        virtual int compileScript();

        virtual void setScriptFile( QString scriptFile, bool compile=true );
        virtual void setScript( QString script );

        asIScriptEngine*  engine()  { return m_aEngine; }
        asIScriptContext* context() { return m_context; }

        void callFunction( asIScriptFunction* func );
        int callFunction0( asIScriptFunction* func, asIScriptContext* context );
        inline void prepare( asIScriptFunction* func ) { m_context->Prepare( func ); }
        void execute();

    protected:
        void printError( asIScriptContext* context );
        int compileSection( QString sriptFile, QString text );

        int m_status;

        QString m_script;
        QString m_scriptFile;
        QString m_scriptFolder;

        asCJITCompiler* m_jit;
        asIScriptEngine* m_aEngine;
        asIScriptModule* m_asModule;
        asIScriptContext* m_context;
};
#endif
