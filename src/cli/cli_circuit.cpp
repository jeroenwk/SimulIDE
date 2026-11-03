/***************************************************************************
 *   SimulIDE command line control interface                              *
 *   Circuit, component and wire commands                                  *
 ***( see copyright.txt file at root folder )*******************************/

#include <QApplication>
#include <QFileInfo>
#include <QDir>
#include <QTimer>

#include "cliapi.h"
#include "clirefs.h"
#include "mainwindow.h"
#include "circuitwidget.h"
#include "circuit.h"
#include "component.h"
#include "connector.h"
#include "node.h"
#include "simulator.h"

namespace
{
bool isModified() { return MainWindow::self()->windowTitle().endsWith('*'); }

void clearModified()
{
    QString title = MainWindow::self()->windowTitle();
    if( title.endsWith('*') ) MainWindow::self()->setWindowTitle( title.left( title.size()-1 ) );
}

// newCircuit() asks with a modal dialog when there are unsaved changes: avoid it
void allowDiscard( CliCtx& c )
{
    if( !isModified() ) return;
    if( !c.flag("discard") )
        throw CliError("circuit has unsaved changes: save it first or pass --discard");
    clearModified();
}

QString absPath( QString path ) { return CliApi::self()->resolvePath( path ); }

QJsonObject circuitInfo()
{
    Circuit* circ = Circuit::self();
    Simulator* sim = Simulator::self();
    return QJsonObject{
        {"file",       circ->getFilePath() },
        {"modified",   isModified() },
        {"components", circ->compList()->size() },
        {"wires",      circ->conList()->size() },
        {"nodes",      circ->nodeList()->size() },
        {"stepSize",   QString::number( sim->stepSize() ) },
        {"stepsPS",    QString::number( sim->stepsPerSec() ) },
        {"NLsteps",    (int)sim->maxNlSteps() },
        {"reaStep",    QString::number( sim->reactStep() ) },
        {"animate",    circ->animate() } };
}
}

void CliApi::registerCircuit()
{
    //--- Circuit -------------------------------------------------------------

    add("quit", "quit [--discard]", "Close SimulIDE (headless or GUI).",
        [this]( CliCtx& c ) -> QJsonValue
    {
        allowDiscard( c );
        bool headless = m_headless;
        QTimer::singleShot( 50, [headless]()   // Let the reply go out first
        {
            if( headless ) qApp->quit();       // Don't save window settings of a hidden window
            else           MainWindow::self()->close();
        });
        return "bye";
    });

    add("circuit info", "circuit info", "File, modified state, item counts and simulation settings.",
        []( CliCtx& ) -> QJsonValue { return circuitInfo(); });

    add("circuit new", "circuit new [--discard]", "Start an empty circuit.",
        []( CliCtx& c ) -> QJsonValue
    {
        allowDiscard( c );
        if( !CircuitWidget::self()->newCircuit() ) throw CliError("could not create a new circuit");
        return circuitInfo();
    });

    add("circuit open", "circuit open <file.sim1> [--discard]", "Open a circuit file.",
        []( CliCtx& c ) -> QJsonValue
    {
        QString file = absPath( c.arg( 0, "file") );
        if( !QFileInfo::exists( file ) ) throw CliError("file not found: "+file );
        if( !file.endsWith(".sim1") && !file.endsWith(".simu") ) throw CliError("not a circuit file (.sim1/.simu): "+file );
        allowDiscard( c );
        CircuitWidget::self()->loadCirc( file );
        if( Circuit::self()->getFilePath() != file ) throw CliError("could not load circuit: "+file );
        clearModified();
        return circuitInfo();
    });

    add("circuit save", "circuit save [file.sim1]", "Save the circuit (to its current file, or to a new one).",
        []( CliCtx& c ) -> QJsonValue
    {
        QString file = c.argOr( 0, Circuit::self()->getFilePath() );
        if( file.isEmpty() ) throw CliError("circuit has no file yet: give a file name");
        file = absPath( file );
        if( !file.endsWith(".sim1") ) file.append(".sim1");
        CircuitWidget::self()->saveCirc( file );
        if( !QFileInfo::exists( file ) ) throw CliError("could not save circuit: "+file );
        return circuitInfo();
    });

    add("circuit export", "circuit export", "Circuit contents in SimulIDE's .sim1 XML format.",
        []( CliCtx& ) -> QJsonValue
    {
        return Circuit::self()->toXml();
    });

    //--- Components ----------------------------------------------------------

    add("comp list", "comp list [--type TYPE]", "List components in the circuit.",
        []( CliCtx& c ) -> QJsonValue
    {
        QString type = c.opt("type");
        QJsonArray list;
        for( Component* comp : *Circuit::self()->compList() )
        {
            if( !type.isEmpty() && comp->itemType().compare( type, Qt::CaseInsensitive ) != 0 ) continue;
            list.append( CliRefs::compJson( comp, false ) );
        }
        return list;
    });

    add("comp get", "comp get <comp>", "Component details: properties (with units) and pins (with connections).",
        []( CliCtx& c ) -> QJsonValue
    {
        return CliRefs::compJson( CliRefs::comp( c.arg( 0, "component") ), true );
    });

    //--- Wires ---------------------------------------------------------------

    add("wire list", "wire list", "List wires (connectors) and the pins they join.",
        []( CliCtx& ) -> QJsonValue
    {
        QJsonArray list;
        for( Connector* con : *Circuit::self()->conList() ) list.append( CliRefs::wireJson( con ) );
        return list;
    });
}
