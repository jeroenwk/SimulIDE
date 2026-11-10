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
#include <QDebug>
#include <QDialog>
#include <QApplication>

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
#include "propdialog.h"
#include "componentselector.h"
#include "exportdialog.h"
#include "circuitexport.h"
#include "instrumentview.h"

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

    add("export", "export [--file F.png|F.svg] [--format png|svg] [--theme current|Classic|Light|Dark] "
        "[--meters show|hide] [--grid show|hide] [--scale S] [--margin PX] [--no-embed-fonts]",
        "Export the whole circuit as a PNG or SVG image, as the toolbar's Export button does. The format comes from "
        "--format or the file extension (default png). --theme renders with a colour preset without changing the "
        "setting (default: the current colours). --meters: instruments with their leads (default: as on the canvas). "
        "--grid: default hide. --scale: PNG pixels per unit (default 2). SVG embeds the bundled fonts it uses "
        "(Inter, JetBrains Mono) unless --no-embed-fonts. Without --file: a temp file. Works headless.",
        []( CliCtx& c ) -> QJsonValue
    {
        auto showOpt = [&c]( QString key, bool def ) -> bool {
            if( !c.has( key ) ) return def;
            QString v = c.opt( key ).toLower();
            if( v == "" || v == "show" || v == "on" || v == "true" || v == "yes" || v == "1" ) return true;
            if( v == "hide" || v == "off" || v == "false" || v == "no" || v == "0" ) return false;
            throw CliError("--"+key+" expects show or hide");
        };
        ExportOptions opt;
        QString file = c.opt("file");
        QString format = c.opt("format").toLower();
        if( format.isEmpty() && !file.isEmpty() ) format = CircuitExport::formatOf( file );
        if( format.isEmpty() ) format = "png";
        if( format != "png" && format != "svg" ) throw CliError("--format must be png or svg");
        opt.format = format;

        if( file.isEmpty() ){
            static int count = 0;
            QDir dir( QDir::tempPath()+"/simulide-capture" );
            dir.mkpath(".");
            file = dir.filePath( QString("export-%1-%2.%3").arg( QCoreApplication::applicationPid() ).arg( ++count ).arg( format ) );
        }else{
            file = CliApi::self()->resolvePath( file );
            if( QFileInfo( file ).suffix().isEmpty() ) file += "."+format;
        }
        QString theme = c.opt("theme");
        if( !theme.isEmpty() && theme.toLower() != "current" )
        {
            for( QString p : Theme::presets() ) if( p.compare( theme, Qt::CaseInsensitive ) == 0 ) theme = p;
            opt.theme = theme;
        }
        opt.meters = showOpt("meters", InstrumentView::self()->shown() );
        opt.grid   = showOpt("grid", false );
        opt.scale  = Cli::parseNum( c.opt("scale","2"), "scale");
        opt.margin = Cli::parseNum( c.opt("margin","24"), "margin");
        opt.embedFonts = !c.has("no-embed-fonts");

        ExportResult r;
        QString error;
        if( !CircuitExport::write( file, opt, &r, &error ) ) throw CliError( error );
        return QJsonObject{
            {"file",   r.file },
            {"format", format },
            {"width",  r.size.width() },
            {"height", r.size.height() },
            {"scale",  r.scale },
            {"theme",  opt.theme.isEmpty() ? Theme::currentPreset() : opt.theme },
            {"meters", opt.meters },
            {"grid",   opt.grid },
            {"fonts",  QJsonArray::fromStringList( r.fonts ) },
            {"area",   QJsonArray{ r.area.x(), r.area.y(), r.area.width(), r.area.height() } } };
    });

    add("capture window", "capture window [main|view|circuit|editor|settings|library|props <comp>|dialog <title>] [--scale S] [--file F.png]",
        "Grab what the window shows: the whole main window (default), the circuit view as the user sees it "
        "(zoom and scroll included), the circuit panel with its toolbar, the code editor, the Settings dialog, "
        "a component's property dialog (view props opens it) or any open dialog whose title contains <title>. "
        "Returns the file path.",
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
        else if( part == "library" ) w = ComponentSelector::self();
        else if( part == "props" ){
            Component* comp = CliRefs::comp( c.arg( 1, "comp") );
            w = comp->propDialog();
            if( !w || !w->isVisible() ) throw CliError("the property dialog of "+comp->getUid()+" is not open (view props opens it)");
        }
        else if( part == "dialog" ){
            QString title = c.arg( 1, "title");
            QStringList open;
            for( QWidget* tw : QApplication::topLevelWidgets() )
            {
                QDialog* d = qobject_cast<QDialog*>( tw );
                if( !d || !d->isVisible() ) continue;
                open << d->windowTitle();
                if( !w && d->windowTitle().contains( title, Qt::CaseInsensitive ) ) w = d;
            }
            qDebug() << "[CLI] capture window dialog" << title << "open dialogs:" << open;
            if( !w ) throw CliError("no open dialog titled \""+title+"\" (open: "+open.join(", ")+")");
        }
        else throw CliError("unknown part: "+part+" (main, view, circuit, editor, settings, library, props, dialog)");

        if( !w->isVisible() && !CliApi::self()->headless() )
            CliApi::self()->addMessage( part+" is not visible: the image may be empty");
        QImage img = w->grab().toImage();
        double scale = Cli::parseNum( c.opt("scale","1"), "scale");   // 1 = logical pixels (half size on HiDPI)
        if( scale <= 0 ) throw CliError("scale must be > 0");
        QSize size( qRound( w->width()*scale ), qRound( w->height()*scale ) );
        if( size != img.size() ) img = img.scaled( size, Qt::KeepAspectRatio, Qt::SmoothTransformation );
        return saveImage( img, outFile( c, "window-"+part ) );
    });

    add("messages", "messages [--last N] [--grep TEXT]",
        "Text of the Messages pane, one line per entry (with Log: Verbose off, debug logs are not in it; "
        "settings set logVerbose=true shows them). --last keeps the last N lines, --grep the lines that contain TEXT.",
        []( CliCtx& c ) -> QJsonValue
    {
        QStringList lines = CircuitWidget::self()->outPane()->allText().split('\n');
        while( !lines.isEmpty() && lines.last().isEmpty() ) lines.removeLast();
        if( c.has("grep") ){
            QString g = c.opt("grep");
            QStringList kept;
            for( const QString& l : lines ) if( l.contains( g ) ) kept.append( l );
            lines = kept;
        }
        if( c.has("last") ){
            bool ok = false;
            int n = c.opt("last").toInt( &ok );
            if( !ok || n < 0 ) throw CliError("--last takes a line count");
            if( lines.size() > n ) lines = lines.mid( lines.size()-n );
        }
        qDebug() << "[cli:messages]" << lines.size() << "line(s)";
        return QJsonObject{ {"lines", QJsonArray::fromStringList( lines )}, {"count", lines.size()} };
    });

    add("view", "view fit|selected|reset|zoom <factor>|center <comp|x,y>|show <comp ...>|raise|settings [app|circuit|simulation|close]|props <comp> [--tab N|NAME] [--close]|export",
        "Control what the circuit view shows: fit all, fit selection, 100%, zoom by a factor, center on a component "
        "or point, show (select + fit) components, bring the window to the front, open the Settings dialog, or open "
        "(or --close) a component's property dialog, on a tab given by number or name, or show the Export dialog "
        "(capture window dialog Export grabs it).",
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
        else if( verb == "props" )                // Open a component's property dialog (on a tab), or close it
        {
            Component* comp = CliRefs::comp( c.arg( 1, "comp") );
            if( c.has("close") ){
                if( comp->propDialog() ) comp->propDialog()->close();
                qDebug() << "[CLI] view props: closed" << comp->getUid();
            }else{
                comp->openProperties();
                PropDialog* d = comp->propDialog();
                QString tab = c.opt("tab");
                if( d && !tab.isEmpty() )
                {
                    bool num = false;
                    int i = tab.toInt( &num );
                    if( !num ){
                        i = -1;
                        for( int k=0; k<d->tabCount(); ++k )
                            if( d->tabName( k ).compare( tab, Qt::CaseInsensitive ) == 0 ) i = k;
                    }
                    if( i < 0 || i >= d->tabCount() ){
                        QStringList names;
                        for( int k=0; k<d->tabCount(); ++k ) names << d->tabName( k );
                        throw CliError("unknown tab: "+tab+" ("+names.join(", ")+")");
                    }
                    d->showTab( i );
                }
                qDebug() << "[CLI] view props: opened" << comp->getUid() << "tab" << tab << "size" << ( d ? d->size() : QSize() );
            }
        }
        else if( verb == "export" ) ExportDialog::showNonModal( MainWindow::self() );
        else if( verb == "raise" )
        {
            MainWindow* mw = MainWindow::self();
            if( mw->isMinimized() ) mw->showNormal();
            mw->raise();
            mw->activateWindow();
        }
        else throw CliError("unknown view action: "+verb+" (fit, selected, reset, zoom, center, show, raise, settings, props, export)");

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
