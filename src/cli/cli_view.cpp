/***************************************************************************
 *   SimulIDE command line control interface                              *
 *   Images of the circuit/window, view control and selection              *
 ***( see copyright.txt file at root folder )*******************************/

#include <QJsonArray>
#include <QImage>
#include <QtMath>
#include <QPainter>
#include <QDir>
#include <QFileInfo>
#include <QCoreApplication>
#include <QGraphicsItem>

#include "cliapi.h"
#include "clirefs.h"
#include "mainwindow.h"
#include "circuitwidget.h"
#include "circuitview.h"
#include "circuit.h"
#include "component.h"
#include "connector.h"
#include "editorwindow.h"
#include "theme.h"
#include "appdialog.h"

namespace
{
// Output file: --file (relative to the client's folder) or a fresh temp file
QString outFile( CliCtx& c, QString kind )
{
    QString file = c.opt("file");
    if( !file.isEmpty() )
    {
        file = CliApi::self()->resolvePath( file );
        if( QFileInfo( file ).suffix().isEmpty() ) file += ".png";
        return file;
    }
    static int count = 0;
    QDir dir( QDir::tempPath()+"/simulide-capture" );
    dir.mkpath(".");
    return dir.filePath( QString("%1-%2-%3.png").arg( kind ).arg( QCoreApplication::applicationPid() ).arg( ++count ) );
}

QJsonObject saveImage( QImage img, QString file )
{
    if( img.isNull() ) throw CliError("nothing to capture");
    if( !img.save( file ) ) throw CliError("could not write "+file );
    return QJsonObject{
        {"file",   file },
        {"width",  img.width() },
        {"height", img.height() } };
}

QRectF compsRect( QStringList refs )
{
    QRectF rect;
    for( QString ref : refs )
    {
        Component* comp = CliRefs::comp( ref );
        rect = rect.united( comp->sceneBoundingRect() );
    }
    return rect;
}

QList<Component*> selectedComps()
{
    QList<Component*> list;
    for( Component* c : *Circuit::self()->compList() ) if( c->isSelected() ) list.append( c );
    return list;
}
}

void CliApi::registerView()
{
    add("capture circuit", "capture circuit [comp ...] [--area x,y,w,h] [--margin PX] [--scale S] [--file F.png]",
        "Render the circuit (all of it, some components, or an area) to a PNG image. "
        "Works headless and does not depend on what the window shows. Returns the file path.",
        []( CliCtx& c ) -> QJsonValue
    {
        Circuit* circ = Circuit::self();
        QRectF rect;
        if( c.has("area") )
        {
            QStringList v = c.opt("area").split(',');
            if( v.size() != 4 ) throw CliError("--area expects x,y,w,h");
            rect = QRectF( v[0].toDouble(), v[1].toDouble(), v[2].toDouble(), v[3].toDouble() );
        }
        else if( !c.pos.isEmpty() ) rect = compsRect( c.pos );
        else{
            if( !circ->compList()->isEmpty() ) rect = circ->itemsBoundingRect();  // Same as "zoom to fit"
            if( rect.isEmpty() ) rect = QRectF( -200, -150, 400, 300 );   // Empty circuit: show the origin
        }
        double margin = Cli::parseNum( c.opt("margin", c.pos.isEmpty() ? "24" : "16"), "margin");
        rect.adjust( -margin, -margin, margin, margin );

        double scale = Cli::parseNum( c.opt("scale","2"), "scale");
        if( scale <= 0 ) throw CliError("scale must be > 0");
        double maxSide = 4000;
        if( rect.width()*scale > maxSide || rect.height()*scale > maxSide )
            scale = maxSide/qMax( rect.width(), rect.height() );

        QImage img( QSize( qCeil( rect.width()*scale ), qCeil( rect.height()*scale ) ), QImage::Format_ARGB32 );
        img.fill( Theme::canvas() );
        QPainter painter( &img );
        painter.setRenderHints( QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform );
        circ->render( &painter, QRectF( QPointF(0,0), QSizeF( img.size() ) ), rect, Qt::KeepAspectRatio );
        painter.end();

        QJsonObject o = saveImage( img, outFile( c, "circuit") );
        o["area"] = QJsonArray{ rect.x(), rect.y(), rect.width(), rect.height() };
        o["scale"] = scale;
        return o;
    });

    add("capture window", "capture window [main|view|circuit|editor|settings] [--scale S] [--file F.png]",
        "Grab what the window shows: the whole main window (default), the circuit view as the user sees it "
        "(zoom and scroll included), the circuit panel with its toolbar, or the code editor. Returns the file path.",
        []( CliCtx& c ) -> QJsonValue
    {
        QString part = c.argOr( 0, "main").toLower();
        QWidget* w = nullptr;
        if     ( part == "main"    ) w = MainWindow::self();
        else if( part == "view"    ) w = CircuitView::self()->viewport();
        else if( part == "circuit" ) w = CircuitWidget::self();
        else if( part == "editor"  ) w = EditorWindow::self();
        else if( part == "settings" ){
            w = CircuitWidget::self()->appDialog();
            if( !w || !w->isVisible() ) throw CliError("the Settings dialog is not open (view settings opens it)");
        }
        else throw CliError("unknown part: "+part+" (main, view, circuit, editor, settings)");

        if( !w->isVisible() && !CliApi::self()->headless() )
            CliApi::self()->addMessage( part+" is not visible: the image may be empty");
        QImage img = w->grab().toImage();
        double scale = Cli::parseNum( c.opt("scale","1"), "scale");   // 1 = logical pixels (half size on HiDPI)
        if( scale <= 0 ) throw CliError("scale must be > 0");
        QSize size( qRound( w->width()*scale ), qRound( w->height()*scale ) );
        if( size != img.size() ) img = img.scaled( size, Qt::KeepAspectRatio, Qt::SmoothTransformation );
        return saveImage( img, outFile( c, "window-"+part ) );
    });

    add("view", "view fit|selected|reset|zoom <factor>|center <comp|x,y>|show <comp ...>|raise|settings [app|circuit|simulation|close]",
        "Control what the circuit view shows: fit all, fit selection, 100%, zoom by a factor, center on a component "
        "or point, show (select + fit) components, or bring the window to the front.",
        []( CliCtx& c ) -> QJsonValue
    {
        CircuitView* view = CircuitView::self();
        QString verb = c.arg( 0, "action").toLower();

        if     ( verb == "fit"      ) view->zoomToFit();
        else if( verb == "selected" ) view->zoomSelected();
        else if( verb == "reset"    ) view->zoomOne();
        else if( verb == "zoom" )
        {
            double f = Cli::parseNum( c.arg( 1, "factor"), "factor");
            if( f <= 0 ) throw CliError("factor must be > 0");
            view->zoomBy( f );
        }
        else if( verb == "center" )
        {
            QString target = c.arg( 1, "component or x,y");
            if( target.contains(',') ) view->centerOn( CliRefs::parsePoint( target ) );
            else                       view->centerOn( CliRefs::comp( target ) );
        }
        else if( verb == "show" )
        {
            if( c.pos.size() < 2 ) throw CliError("missing component");
            QRectF rect = compsRect( c.pos.mid( 1 ) );
            Circuit::self()->clearSelection();
            for( QString ref : c.pos.mid( 1 ) ) CliRefs::comp( ref )->setSelected( true );
            rect.adjust( -48, -48, 48, 48 );
            view->fitInView( rect, Qt::KeepAspectRatio );
            if( view->transform().m11() > 3 ) { view->resetMatrix(); view->zoomBy( 3 ); view->centerOn( rect.center() ); }
        }
        else if( verb == "settings" )             // Open the Settings dialog (on a tab), or close it
        {
            QString tab = c.argOr( 1, "circuit").toLower();
            if( tab == "close" ){
                if( CircuitWidget::self()->appDialog() ) CircuitWidget::self()->appDialog()->close();
            }else{
                int i = QStringList{"app","circuit","simulation"}.indexOf( tab );
                if( i < 0 ) throw CliError("unknown settings tab: "+tab+" (app, circuit, simulation, close)");
                CircuitWidget::self()->settApp();
                CircuitWidget::self()->appDialog()->showTab( i );
                CircuitWidget::self()->appDialog()->raise();
            }
        }
        else if( verb == "raise" )
        {
            MainWindow* mw = MainWindow::self();
            if( mw->isMinimized() ) mw->showNormal();
            mw->raise();
            mw->activateWindow();
        }
        else throw CliError("unknown view action: "+verb+" (fit, selected, reset, zoom, center, show, raise, settings)");

        QRectF visible = view->mapToScene( view->viewport()->rect() ).boundingRect();
        return QJsonObject{
            {"zoom",    view->transform().m11() },
            {"visible", QJsonArray{ qRound( visible.x() ), qRound( visible.y() ), qRound( visible.width() ), qRound( visible.height() ) } } };
    });

    add("select", "select [comp ...] [--add] [--none]",
        "Without arguments: list what is selected (e.g. what the user selected in the GUI). "
        "With components: select them (highlighted in the GUI). --add keeps the current selection, --none clears it.",
        []( CliCtx& c ) -> QJsonValue
    {
        Circuit* circ = Circuit::self();
        if( c.flag("none") ) circ->clearSelection();
        if( !c.pos.isEmpty() )
        {
            QList<Component*> comps;
            for( QString ref : c.pos ) comps.append( CliRefs::comp( ref ) );   // Resolve all first
            if( !c.flag("add") ) circ->clearSelection();
            for( Component* comp : comps ) comp->setSelected( true );
        }
        QJsonArray rows;
        for( Component* comp : selectedComps() ) rows.append( CliRefs::compJson( comp, false ) );
        int wires = 0;
        for( QGraphicsItem* item : circ->selectedItems() )
            if( !dynamic_cast<Component*>( item ) ) wires++;
        if( wires && rows.isEmpty() ) CliApi::self()->addMessage( QString("%1 wire segments selected").arg( wires ) );
        return rows;
    });
}
