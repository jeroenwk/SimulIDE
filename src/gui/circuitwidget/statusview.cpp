/***************************************************************************
 *   Part state display: the states parts report (setStatus), as badges   *
 *   above each part and its outline in the most severe state's colour     *
 ***( see copyright.txt file at root folder )*******************************/

#include <QPainter>
#include <QSettings>
#include <QFontMetricsF>

#include "statusview.h"
#include "appfonts.h"
#include "statelog.h"
#include "mainwindow.h"
#include "circuit.h"
#include "circuitwidget.h"
#include "simulator.h"
#include "component.h"
#include "subcircuit.h"
#include "theme.h"
#include "mcu.h"
#include "scriptcpu.h"

StatusView* StatusView::self()
{
    static StatusView* view = new StatusView();
    return view;
}

StatusView::StatusView()
          : Updatable()
{
    m_enabled = MainWindow::self()->settings()->value("Circuit/partStatus", true ).toBool();
}

void StatusView::setEnabled( bool en )
{
    if( en == m_enabled ) return;
    m_enabled = en;
    CircuitWidget::self()->syncPartStatus( en );       // Toolbar button
    CircuitWidget::self()->syncEventLog();              // Messages checkbox
    MainWindow::self()->settings()->setValue("Circuit/partStatus", en );
    if( en ){ attach(); updateStep(); }
    else      clear();
}

void StatusView::attach()
{
    if( Simulator::self() ) Simulator::self()->addToUpdateList( this ); // No-op if listed
}

static StatusBadge* badgeOf( Component* comp )
{
    for( QGraphicsItem* child : comp->childItems() )
        if( child->type() == StatusBadge::Type ) return static_cast<StatusBadge*>( child );
    return nullptr;
}

void StatusView::clear()
{
    for( Component* comp : *Circuit::self()->compList() )
        if( StatusBadge* badge = badgeOf( comp ) ) delete badge;
}

QString StatusView::blockName( Component* inner )
{
    QString name = inner->getUid().section('-', 0, -2 );   // Inside a subcircuit the label is the uid
    if( name.contains('_') ) name = name.section('_', -1 );
    return name;
}

QList<QPair<QString, ScriptCpu*>> StatusView::statusCores( Component* comp )
{
    QList<QPair<QString, ScriptCpu*>> list;
    auto add = [&list]( Component* c, QString name ){
        if( Mcu* mcu = dynamic_cast<Mcu*>( c ) )
            if( ScriptCpu* cpu = dynamic_cast<ScriptCpu*>( mcu->cpu() ) )
                if( cpu->status().level >= 0 || !cpu->statusHistory().isEmpty() ) list.append({ name, cpu });
    };
    add( comp, "" );
    if( SubCircuit* sub = dynamic_cast<SubCircuit*>( comp ) )
        for( Component* inner : *sub->compList() ) add( inner, blockName( inner ) );
    return list;
}

QList<StatusView::Block> StatusView::blocks( Component* comp )
{
    QList<Block> list;
    if( StateLog* log = comp->stateLog() )                  // A built-in part's operating state
    {
        StateLog::Event e = log->shown();
        if( e.level >= 0 ) list.append({ "", e.text, e.level });
    }
    else if( comp->statusLevel() >= 0 )                     // The part itself reports
        list.append({ "", comp->statusText(), comp->statusLevel() });

    if( SubCircuit* sub = dynamic_cast<SubCircuit*>( comp ) )   // Its cores report
        for( Component* inner : *sub->compList() )
            if( inner->statusLevel() >= 0 )
                list.append({ blockName( inner ), inner->statusText(), inner->statusLevel() });
    return list;
}

QColor StatusView::levelColor( int level ) { return levelColor( level, Theme::canvas().lightness() < 128 ); }

QColor StatusView::levelColor( int level, bool dark )
{
    switch( level ){
        case Component::stOn:    return dark ? QColor( 63, 180, 106) : QColor( 31, 154,  76);
        case Component::stWait:  return dark ? QColor(227, 161,  47) : QColor(194, 124,   0);
        case Component::stFault: return dark ? QColor(229,  72,  77) : QColor(210,  45,  51);
    }
    return dark ? QColor(138, 144, 153) : QColor(123, 129, 138);   // Off
}

void StatusView::updateStep()
{
    if( !m_enabled ) return;
    Simulator* sim = Simulator::self();
    if( !sim || sim->simState() < SIM_PAUSED ) return;

    for( Component* comp : *Circuit::self()->compList() )
    {
        QList<Block> list = blocks( comp );
        StatusBadge* badge = badgeOf( comp );
        if( list.isEmpty() ){ delete badge; continue; }
        if( !badge ) badge = new StatusBadge( comp );
        badge->setBlocks( list );
    }
}

//------------------------------------------------------------------------------

StatusBadge::StatusBadge( Component* comp )
           : QGraphicsItem( comp )
{
    m_comp  = comp;
    m_worst = -1;
    setZValue( 100 );
    setAcceptedMouseButtons( Qt::NoButton );
    setAcceptHoverEvents( false );
}

static QFont badgeFont()
{
    QFont font( AppFonts::mono() );
    font.setStyleHint( QFont::TypeWriter );
    font.setPixelSize( 7 );
    font.setBold( true );
    return font;
}

void StatusBadge::setBlocks( const QList<StatusView::Block>& blocks )
{
    // Draw in scene coordinates whatever the part's rotation or flip: cancel its transform
    setTransform( m_comp->sceneTransform().inverted() );

    QRectF body = m_comp->mapRectToScene( m_comp->boundingRect() ).adjusted( 2, 2,-2,-2 );

    QFontMetricsF fm( badgeFont() );
    const double h = 10, pad = 3.5, gap = 3, rowGap = 2;
    double maxRow = qMax( body.width()+48, 96.0 );

    QList<QList<Pill>> rows;             // Pills in rows, wrapped at maxRow
    QList<double> widths;
    m_worst = -1;
    for( const StatusView::Block& b : blocks )
    {
        QString text = b.name.isEmpty() ? b.text : b.name+" "+b.text;
        double w = fm.horizontalAdvance( text )+2*pad;
        if( rows.isEmpty() || widths.last()+gap+w > maxRow ) { rows.append( QList<Pill>() ); widths.append( -gap ); }
        rows.last().append({ QRectF( 0, 0, w, h ), text, b.level });
        widths.last() += gap+w;
        if( b.level > m_worst ) m_worst = b.level;
    }
    m_pills.clear();
    double bottom = body.top()-16;        // Above the part's name label
    for( int r=0; r<rows.size(); ++r )
    {
        double y = bottom-(rows.size()-r)*(h+rowGap);
        double x = body.center().x()-widths[r]/2;
        for( Pill& p : rows[r] )
        {
            p.rect.moveTo( x, y );
            x += p.rect.width()+gap;
            m_pills.append( p );
        }
    }
    QRectF bounds = body.adjusted(-3,-3, 3, 3 );
    for( const Pill& p : m_pills ) bounds |= p.rect.adjusted(-1,-1, 1, 1 );

    if( bounds != m_bounds ){ prepareGeometryChange(); m_bounds = bounds; }
    m_body = body;
    update();
}

void StatusBadge::paint( QPainter* p, const QStyleOptionGraphicsItem*, QWidget* )
{
    if( m_worst < 0 ) return;
    p->setRenderHint( QPainter::Antialiasing );

    QPen pen( StatusView::levelColor( m_worst ), 2 );     // Outline: the most severe state
    p->setPen( pen );
    p->setBrush( Qt::NoBrush );
    p->drawRoundedRect( m_body.adjusted(-1.5,-1.5, 1.5, 1.5 ), 2.5, 2.5 );

    p->setFont( badgeFont() );
    for( const Pill& pill : m_pills )
    {
        QColor color = StatusView::levelColor( pill.level );
        bool filled = pill.level >= Component::stWait;     // Waiting and faults stand out
        p->setPen( QPen( color, 1.2 ) );
        p->setBrush( filled ? color : Theme::canvas() );
        p->drawRoundedRect( pill.rect, pill.rect.height()/2, pill.rect.height()/2 );
        p->setPen( filled ? QColor( Qt::white ) : color );
        p->drawText( pill.rect, Qt::AlignCenter, pill.text );
    }
}
