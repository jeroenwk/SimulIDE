/***************************************************************************
 *   SimulIDE command line control interface                              *
 *   Circuit, component and wire commands                                  *
 ***( see copyright.txt file at root folder )*******************************/

#include <QApplication>
#include <QFileInfo>
#include <QDir>
#include <QTimer>

#include <QDomDocument>
#include <QFile>

#include "cliapi.h"
#include "circmatrix.h"
#include "clirefs.h"
#include "currentflow.h"
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
        {"animate",    circ->animate() },
        {"matrix",     CircMatrix::self() ? CircMatrix::self()->stats() : QString() } };
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
            if( headless ){                    // Don't save window settings of a hidden window
                CircuitWidget::self()->newCircuit(); // Tear the circuit down cleanly, as closeEvent() does
                qApp->quit();
            }
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
        {
            QFile f( file );
            QDomDocument doc;
            QString err;
            int line = 0;
            if( !f.open( QIODevice::ReadOnly ) ) throw CliError("cannot read "+file );
            if( !doc.setContent( &f, &err, &line ) )
                throw CliError( QString("not a valid circuit file: %1 (line %2: %3)").arg( file ).arg( line ).arg( err ) );
        }
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

    add("circuit description", "circuit description [--set TEXT | --file F.md | --clear]",
        "The circuit's description (Markdown), saved in the circuit file and shown in the bottom pane when it is loaded. "
        "No option: read it. --set / --file replace it, --clear removes it (the circuit becomes modified: save it).",
        []( CliCtx& c ) -> QJsonValue
    {
        Circuit* circ = Circuit::self();
        bool change = true;
        if     ( c.flag("clear") ) circ->setDescription("");
        else if( c.has("set")    ) circ->setDescription( c.opt("set") );
        else if( c.has("file")   )
        {
            QFile f( absPath( c.opt("file") ) );
            if( !f.open( QIODevice::ReadOnly | QIODevice::Text ) ) throw CliError("cannot read "+f.fileName() );
            circ->setDescription( QString::fromUtf8( f.readAll() ) );
        }
        else change = false;
        if( change ) CircuitWidget::self()->showDescription();
        return QJsonObject{ {"description", circ->description() }, {"modified", isModified() } };
    });

    add("circuit fonts", "circuit fonts", "Convert Text components to the default fonts "
        "(Inter; JetBrains Mono for monospace names). One undo step.",
        []( CliCtx& ) -> QJsonValue
    {
        int n = Circuit::self()->convertFontsToDefault();
        return QJsonObject{ {"converted", n } };
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

    add("comp get", "comp get <comp> [--all]", "Component details: properties (with units or options) and pins (with connections). "
        "--all also lists hidden properties (e.g. scope settings), which comp set can change.",
        []( CliCtx& c ) -> QJsonValue
    {
        return CliRefs::compJson( CliRefs::comp( c.arg( 0, "component") ), true, c.flag("all") );
    });

    //--- Wires ---------------------------------------------------------------

    add("wire list", "wire list", "List wires (connectors) and the pins they join.",
        []( CliCtx& ) -> QJsonValue
    {
        QJsonArray list;
        for( Connector* con : *Circuit::self()->conList() ) list.append( CliRefs::wireJson( con ) );
        return list;
    });

    add("wire current", "wire current [wire ...]",
        "Current through wires (A), signed: positive flows from the 'from' pin to the 'to' pin. "
        "Computed from the solved circuit; works paused. flag: approximate = shared between passive pins "
        "(tunnels, subcircuit pins), inconsistent = current does not add up (bug). "
        "level (with currentFlow on): how the dots show it, 1 = as the largest current, 0 = crawling, -1 = below currentMin.",
        []( CliCtx& c ) -> QJsonValue
    {
        if( !Simulator::self()->isRunning() ) throw CliError("simulation is not running");
        QList<Connector*> wires;
        for( QString ref : c.pos ) wires.append( CliRefs::wire( ref ) );   // Resolve all first
        CliPendingPtr reply = c.defer();
        CurrentFlow::self()->whenIdle( [wires, reply]()
        {
            QJsonArray rows;
            QList<Connector*> list = wires.isEmpty() ? *Circuit::self()->conList() : wires;
            for( Connector* con : list )
            {
                if( !Circuit::self()->conList()->contains( con ) ) continue;
                QJsonObject o = CliRefs::wireJson( con );
                o.remove("points");
                o["current"] = con->flowCurrent;
                CurrentFlow* flow = CurrentFlow::self();
                if( flow->enabled() )   // How the dots show it: 1 = like the largest current, 0 = crawl, -1 = none
                    o["level"] = qRound( 100*flow->level( con->flowCurrent, flow->threshold(), flow->scale() ) )/100.0;
                if( con->flowFlag == 1 ) o["flag"] = "approximate";
                if( con->flowFlag == 2 ) o["flag"] = "inconsistent";
                rows.append( o );
            }
            reply->ok( rows );
        });
        return QJsonValue();
    });
}
