/***************************************************************************
 *   Parts pane (specs/parts-pane.md)                                      *
 ***( see copyright.txt file at root folder )*******************************/

#include <QApplication>
#include <QClipboard>
#include <QCheckBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QIntValidator>
#include <QMenu>
#include <QScrollBar>
#include <QSettings>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QDebug>

#include "partspane.h"
#include "circuit.h"
#include "circuitview.h"
#include "circuitwidget.h"
#include "component.h"
#include "instrumentview.h"
#include "mainwindow.h"
#include "taborder.h"

PartsPane* PartsPane::m_pSelf = nullptr;
QList<PartsTable::SortKey> PartsPane::m_sort;

namespace {
enum { roleUid = Qt::UserRole+1 };

// BOM cells only: a line edit; its text goes to every selected row (PartsPane::editBom)
class BomDelegate : public QStyledItemDelegate
{
    public:
        BomDelegate( PartsPane* pane ) : QStyledItemDelegate( pane ), m_pane( pane ) {}

        QWidget* createEditor( QWidget* parent, const QStyleOptionViewItem& opt, const QModelIndex& idx ) const override
        {
            if( idx.column() == PartsTable::colTab )         // A place in the Tab walk (§12)
            {
                if( idx.data().toString().toInt() <= 0 ) return nullptr;   // Locked or hidden: not in it
                QLineEdit* e = new QLineEdit( parent );
                e->setValidator( new QIntValidator( 1, 99999, e ) );
                return e;
            }
            if( !PartsTable::isBomCol( idx.column() ) ) return nullptr;
            return new QLineEdit( parent );
        }
        void setEditorData( QWidget* editor, const QModelIndex& idx ) const override
        {
            if( QLineEdit* e = qobject_cast<QLineEdit*>( editor ) ) e->setText( idx.data().toString() );
        }
        void setModelData( QWidget* editor, QAbstractItemModel*, const QModelIndex& idx ) const override
        {
            QLineEdit* e = qobject_cast<QLineEdit*>( editor );
            if( !e ) return;
            QString uid = idx.sibling( idx.row(), 0 ).data( roleUid ).toString();
            if( e->text() == idx.data().toString() ) return;
            if( idx.column() == PartsTable::colTab ){ if( e->text().toInt() > 0 ) m_pane->editTab( uid, e->text().toInt() ); }
            else m_pane->editBom( uid, idx.column(), e->text() );
        }
    private:
        PartsPane* m_pane;
};

QToolButton* toggle( QWidget* parent, QString text, QString tip )
{
    QToolButton* b = new QToolButton( parent );
    b->setText( text );
    b->setToolTip( tip );
    b->setCheckable( true );
    b->setAutoRaise( true );
    b->setFocusPolicy( Qt::NoFocus );
    return b;
}
}

PartsPane::PartsPane( QWidget* parent )
         : QWidget( parent )
{
    m_pSelf = this;
    setObjectName("PartsPane");
    QVBoxLayout* lay = new QVBoxLayout( this );
    lay->setContentsMargins( 0, 0, 0, 0 );
    lay->setSpacing( 0 );

    QHBoxLayout* top = new QHBoxLayout();
    top->setContentsMargins( 6, 2, 4, 2 );
    top->setSpacing( 4 );
    m_search = new QLineEdit( this );
    m_search->setPlaceholderText( tr("Search: label, type, value (10k), model, MPN…") );
    m_search->setClearButtonEnabled( true );
    connect( m_search, &QLineEdit::textChanged, this, [this](){ fill(); } );
    top->addWidget( m_search, 1 );

    QSettings* set = MainWindow::self()->settings();
    QStringList fs = set->value("Circuit/partsFilters", "").toString().split(",", QString::SkipEmptyParts );
    for( QString f : fs )
    {
        QString k = f.section('=', 0, 0 ), v = f.section('=', 1 );
        if     ( k == "graphical" ) m_filter.graphical = v == "1";
        else if( k == "meters" )    m_filter.meters    = v == "1";
        else if( k == "sources" )   m_filter.sources   = v == "1";
        else if( k == "locked" )    m_filter.locked    = v == "1";
        else if( k == "off" ) for( QString c : v.split(";", QString::SkipEmptyParts ) ) m_filter.offCats.insert( c );
    }
    m_graphBtn   = toggle( this, tr("Graphical"), tr("Show graphical parts: images, text, rectangles, ellipses, lines") );
    m_metersBtn  = toggle( this, tr("Meters"),  tr("Show meters (probes, voltmeters, ammeters, frequency meters, scopes, logic analyzers) and tunnels and buses, which are not bought parts") );
    m_sourcesBtn = toggle( this, tr("Sources"), tr("Show sources: fixed voltages, clocks, generators, batteries, rails, grounds…") );
    m_lockedBtn  = toggle( this, tr("Locked"),  tr("Show locked parts") );
    m_graphBtn->setChecked( m_filter.graphical );
    m_metersBtn->setChecked( m_filter.meters );
    m_sourcesBtn->setChecked( m_filter.sources );
    m_lockedBtn->setChecked( m_filter.locked );
    auto onToggle = [this](){
        m_filter.graphical = m_graphBtn->isChecked();
        m_filter.meters    = m_metersBtn->isChecked();
        m_filter.sources   = m_sourcesBtn->isChecked();
        m_filter.locked    = m_lockedBtn->isChecked();
        saveSettings();
        fill();
    };
    for( QToolButton* b : { m_graphBtn, m_metersBtn, m_sourcesBtn, m_lockedBtn } )
    {
        connect( b, &QToolButton::toggled, this, onToggle );
        top->addWidget( b );
    }
    m_moreBtn = new QToolButton( this );
    m_moreBtn->setText( tr("More…") );
    m_moreBtn->setToolTip( tr("Every group of the component list, each shown or hidden") );
    m_moreBtn->setAutoRaise( true );
    m_moreBtn->setFocusPolicy( Qt::NoFocus );
    connect( m_moreBtn, &QToolButton::clicked, this, [this](){
        QMenu menu;
        for( QString cat : PartsTable::categories() )
        {
            if( cat == "Graphical" || cat == "Meters" || cat == "Sources" ) continue;   // Their own toggles
            QAction* a = menu.addAction( cat );
            a->setCheckable( true );
            a->setChecked( !m_filter.offCats.contains( cat ) );
            connect( a, &QAction::toggled, this, [this, cat]( bool on ){
                if( on ) m_filter.offCats.remove( cat ); else m_filter.offCats.insert( cat );
                saveSettings();
                fill();
            });
        }
        menu.exec( m_moreBtn->mapToGlobal( QPoint( 0, m_moreBtn->height() ) ) );
    });
    top->addWidget( m_moreBtn );

    m_perPart = new QCheckBox( tr("Per part"), this );
    m_perPart->setToolTip( tr("Export the table as shown, one line per part with its Tab place, instead of grouping identical parts") );
    m_perPart->setFocusPolicy( Qt::NoFocus );
    top->addWidget( m_perPart );
    QToolButton* exportBtn = new QToolButton( this );
    exportBtn->setText( tr("Export BOM…") );
    exportBtn->setToolTip( tr("Save the parts the filters let through as a BOM (CSV or Markdown), identical parts grouped") );
    exportBtn->setFocusPolicy( Qt::NoFocus );
    connect( exportBtn, &QToolButton::clicked, this, [this](){ exportBom(); } );
    top->addWidget( exportBtn );
    lay->addLayout( top );

    m_tree = new QTreeWidget( this );
    m_tree->setColumnCount( PartsTable::colCount );
    m_tree->setHeaderLabels( PartsTable::colTitles() );
    m_tree->setRootIsDecorated( false );
    m_tree->setUniformRowHeights( true );
    m_tree->setAlternatingRowColors( true );
    m_tree->setSelectionMode( QAbstractItemView::ExtendedSelection );
    m_tree->setEditTriggers( QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed );
    m_tree->setItemDelegate( new BomDelegate( this ) );
    m_tree->setContextMenuPolicy( Qt::CustomContextMenu );
    m_tree->setTextElideMode( Qt::ElideRight );
    QHeaderView* h = m_tree->header();
    h->setStretchLastSection( true );
    h->setSectionsMovable( true );
    h->setSectionsClickable( true );
    h->setSortIndicatorShown( true );
    h->setContextMenuPolicy( Qt::CustomContextMenu );
    h->resizeSection( PartsTable::colLock, 28 );
    h->resizeSection( PartsTable::colOther, 260 );
    h->resizeSection( PartsTable::colTab, 40 );
    h->setSectionHidden( PartsTable::colCategory, true );        // Hidden by default
    QByteArray st = QByteArray::fromBase64( set->value("Circuit/partsPaneColumns", "").toByteArray() );
    if( !st.isEmpty() ) h->restoreState( st );
    m_tree->headerItem()->setToolTip( PartsTable::colLock, tr("Locked: a click locks or unlocks (all selected rows when the row is one of them)") );
    m_tree->headerItem()->setToolTip( PartsTable::colTab, tr("Place in the canvas Tab walk (— : locked or hidden, Tab skips it). Double-click or Enter to type a new place: the others shift") );
    connect( h, &QHeaderView::sectionClicked, this, [this]( int col ){ headerClicked( col ); } );
    connect( h, &QHeaderView::customContextMenuRequested, this, [this]( const QPoint& p ){ headerMenu( p ); } );
    connect( h, &QHeaderView::sectionMoved,   this, [this](){ saveSettings(); } );
    connect( h, &QHeaderView::sectionResized, this, [this](){ if( !m_filling ) saveSettings(); } );
    m_tree->installEventFilter( this );
    m_tree->viewport()->installEventFilter( this );
    lay->addWidget( m_tree, 1 );

    connect( m_tree, &QTreeWidget::itemSelectionChanged, this, [this](){ if( !m_filling && !m_syncing ) toCanvas(); } );
    connect( m_tree, &QTreeWidget::itemDoubleClicked, this, [this]( QTreeWidgetItem* it, int col ){
        if( PartsTable::isBomCol( col ) || col == PartsTable::colLock || col == PartsTable::colTab ) return;
        QList<Component*> cs = compsOf( { it->data( 0, roleUid ).toString() } );
        if( !cs.isEmpty() ) cs.first()->openProperties();
    });
    connect( m_tree, &QTreeWidget::customContextMenuRequested, this, [this]( const QPoint& p ){ contextMenu( p ); } );

    m_count = new QLabel( this );
    m_count->setContentsMargins( 8, 2, 8, 2 );
    lay->addWidget( m_count );

    updtSortIndicator();
    m_timer.setInterval( 200 );                 // A rebuild at most every 200 ms, only when shown and changed
    connect( &m_timer, &QTimer::timeout, this, [this](){ poll(); } );
    m_timer.start();
}

void PartsPane::saveSettings()
{
    QSettings* set = MainWindow::self()->settings();
    set->setValue("Circuit/partsPaneColumns", QString( m_tree->header()->saveState().toBase64() ) );
    QStringList off = m_filter.offCats.values();
    off.sort();
    set->setValue("Circuit/partsFilters", QString("graphical=%1,meters=%2,sources=%3,locked=%4,off=%5")
        .arg( m_filter.graphical ).arg( m_filter.meters ).arg( m_filter.sources ).arg( m_filter.locked ).arg( off.join(";") ) );
}

void PartsPane::follow()
{
    Circuit* c = Circuit::self();
    if( c != m_circ )
    {
        if( m_selConn ) disconnect( m_selConn );
        m_circ = c;
        if( c ) m_selConn = connect( c, &QGraphicsScene::selectionChanged, this, [this](){
            if( m_syncing || m_fromCanvasQueued ) return;
            m_fromCanvasQueued = true;
            QTimer::singleShot( 0, this, [this](){ m_fromCanvasQueued = false; fromCanvas(); } );
        });
    }
    m_sig.clear();
    if( isVisible() ) fill();
}

void PartsPane::showEvent( QShowEvent* e )
{
    QWidget::showEvent( e );
    if( signature() != m_sig ) fill();
}

QString PartsPane::signature()
{
    Circuit* c = Circuit::self();
    if( !c ) return "";
    double pos = 0;
    int locked = 0;
    for( Component* comp : *c->compList() )
    {
        QPointF p = comp->pos();
        pos += p.x()*7.0+p.y()*13.0+comp->rotation();
        if( comp->isLocked() ) locked++;
    }
    return QString("%1|%2|%3|%4|%5|%6").arg( quintptr( c ) ).arg( c->editCount() ).arg( c->compList()->size() )
          .arg( InstrumentView::self() ? InstrumentView::self()->shown() : true ).arg( pos, 0, 'g', 15 ).arg( locked );
}

void PartsPane::poll()
{
    if( !isVisible() ) return;
    if( Circuit::self() != m_circ ) follow();
    Circuit* c = Circuit::self();
    if( !c || c->loading() || c->isBusy() ) return;
    if( editing() ) return;   // Not under the cell editor
    if( signature() != m_sig ) fill();
}

bool PartsPane::editing()                  // A BOM cell's editor is open
{
    QWidget* f = QApplication::focusWidget();
    return f && qobject_cast<QLineEdit*>( f ) && m_tree->viewport()->isAncestorOf( f );
}

void PartsPane::syncNow()
{
    if( Circuit::self() != m_circ ) follow();
    if( signature() != m_sig ) fill();
    else fromCanvas();
}

void PartsPane::fill()
{
    Circuit* circ = Circuit::self();
    if( !circ ) return;
    m_sig = signature();
    m_rows = PartsTable::rows();
    QList<PartRow> vis;
    QString search = m_search->text();
    for( const PartRow& r : m_rows )
        if( PartsTable::passes( r, m_filter ) && PartsTable::matches( r, search ) ) vis << r;
    PartsTable::sort( vis, m_sort );

    m_filling = true;
    int scroll = m_tree->verticalScrollBar()->value();
    m_tree->clear();
    QList<QTreeWidgetItem*> items;
    for( const PartRow& r : vis )
    {
        QStringList texts;
        for( int col=0; col<PartsTable::colCount; ++col ) texts << PartsTable::text( r, col );
        QTreeWidgetItem* it = new QTreeWidgetItem( texts );
        it->setData( 0, roleUid, r.uid );
        it->setFlags( it->flags() | Qt::ItemIsEditable );
        it->setTextAlignment( PartsTable::colTab, Qt::AlignRight | Qt::AlignVCenter );
        it->setTextAlignment( PartsTable::colLock, Qt::AlignCenter );
        if( !r.other.isEmpty() ) it->setToolTip( PartsTable::colOther, r.other );
        if( r.locked ) it->setToolTip( PartsTable::colLock, tr("Locked: a click unlocks") );
        if( r.hidden )                                   // An instrument hidden by Show Instruments
            for( int col=0; col<PartsTable::colCount; ++col ) it->setForeground( col, QColor( 128, 128, 128 ) );
        items << it;
    }
    m_tree->addTopLevelItems( items );
    m_tree->verticalScrollBar()->setValue( scroll );
    m_shown = vis.size();
    m_filling = false;
    fromCanvas();
}

QList<Component*> PartsPane::compsOf( QStringList uids )
{
    QList<Component*> out;
    if( !Circuit::self() ) return out;
    for( Component* c : *Circuit::self()->compList() ) if( uids.contains( c->getUid() ) ) out << c;
    return out;
}

QStringList PartsPane::selectedUids()
{
    QStringList uids;
    for( int i=0; i<m_tree->topLevelItemCount(); ++i )
    {
        QTreeWidgetItem* it = m_tree->topLevelItem( i );
        if( it->isSelected() ) uids << it->data( 0, roleUid ).toString();
    }
    return uids;
}

void PartsPane::fromCanvas()
{
    Circuit* circ = Circuit::self();
    if( !circ || m_filling ) return;
    QSet<QString> sel;
    for( Component* c : circ->selectedParts() ) sel.insert( c->getUid() );
    m_syncing = true;
    m_tree->blockSignals( true );
    QTreeWidgetItem* first = nullptr;
    for( int i=0; i<m_tree->topLevelItemCount(); ++i )
    {
        QTreeWidgetItem* it = m_tree->topLevelItem( i );
        bool s = sel.contains( it->data( 0, roleUid ).toString() );
        if( it->isSelected() != s ) it->setSelected( s );
        if( s && !first ) first = it;
    }
    m_tree->blockSignals( false );
    m_tree->viewport()->update();
    if( first ) m_tree->scrollToItem( first );
    m_syncing = false;
    updtCount();
}

void PartsPane::toCanvas()
{
    Circuit* circ = Circuit::self();
    if( !circ ) return;
    QList<Component*> comps = compsOf( selectedUids() );
    QList<Component*> added;                        // Rows just added to the selection flash on the canvas
    for( Component* c : comps ) if( !c->isSelected() ) added << c;
    m_syncing = true;
    circ->selectParts( comps );
    if( added.isEmpty() && comps.size() == 1 ) added = comps;   // A click on the one selected row: again
    circ->flashParts( added );
    m_syncing = false;
    if( !comps.isEmpty() )                           // Off screen: brought in sight
    {
        QRectF r;
        for( Component* c : comps ) r = r.isNull() ? c->sceneBoundingRect() : r.united( c->sceneBoundingRect() );
        if( CircuitView* v = CircuitView::self() ) v->ensureVisible( comps.first()->sceneBoundingRect(), 32, 32 );
    }
    circ->update();
    updtCount();
}

QString PartsPane::countText()
{
    QString t = tr("%1 of %2 parts shown").arg( m_shown ).arg( m_rows.size() );
    Circuit* circ = Circuit::self();
    if( !circ ) return t;
    QList<Component*> sel = circ->selectedParts();
    if( sel.isEmpty() ) return t;
    QSet<QString> shown;
    for( int i=0; i<m_tree->topLevelItemCount(); ++i ) shown.insert( m_tree->topLevelItem( i )->data( 0, roleUid ).toString() );
    int hidden = 0;
    for( Component* c : sel ) if( !shown.contains( c->getUid() ) ) hidden++;
    t += QString::fromUtf8(" · ")+tr("%1 selected").arg( sel.size() );
    if( hidden ) t += " "+tr("(%1 hidden by filters)").arg( hidden );
    return t;
}

void PartsPane::updtCount() { m_count->setText( countText() ); }

void PartsPane::setSearch( QString text ) { m_search->setText( text ); }

void PartsPane::setFilter( PartsTable::Filter f )
{
    m_filter = f;
    QList<QToolButton*> btns = { m_graphBtn, m_metersBtn, m_sourcesBtn, m_lockedBtn };
    for( QToolButton* b : btns ) b->blockSignals( true );
    m_graphBtn->setChecked( f.graphical );
    m_metersBtn->setChecked( f.meters );
    m_sourcesBtn->setChecked( f.sources );
    m_lockedBtn->setChecked( f.locked );
    for( QToolButton* b : btns ) b->blockSignals( false );
    saveSettings();
    fill();
}

void PartsPane::reveal( QString uid )
{
    syncNow();
    bool found = false;
    for( int i=0; i<m_tree->topLevelItemCount() && !found; ++i )
        found = m_tree->topLevelItem( i )->data( 0, roleUid ).toString() == uid;
    if( !found )                                     // Filtered out: show everything
    {
        m_search->clear();
        for( QToolButton* b : { m_graphBtn, m_metersBtn, m_sourcesBtn, m_lockedBtn } ) b->setChecked( true );
        m_filter.offCats.clear();
        fill();
    }
    QList<Component*> cs = compsOf( { uid } );
    if( !cs.isEmpty() ){ Circuit::self()->selectParts( cs ); Circuit::self()->flashParts( cs ); }
    fromCanvas();
}

// ----- Sorting -------------------------------------------------------------------

void PartsPane::headerClicked( int col )
{
    bool shift = QApplication::keyboardModifiers() & Qt::ShiftModifier;
    if( shift && !m_sort.isEmpty() )                 // A secondary key (or reversed)
    {
        bool found = false;
        for( PartsTable::SortKey& k : m_sort ) if( k.col == col ){ k.desc = !k.desc; found = true; }
        if( !found ) m_sort.append( { col, false } );
    }
    else if( !m_sort.isEmpty() && m_sort.first().col == col ) m_sort = { { col, !m_sort.first().desc } };
    else if( m_sort.isEmpty() && col == PartsTable::colLabel ) m_sort = { { col, true } };   // From BOM order: reversed
    else m_sort = { { col, false } };
    updtSortIndicator();
    fill();
}

void PartsPane::updtSortIndicator()
{
    QHeaderView* h = m_tree->header();
    h->blockSignals( true );
    if( m_sort.isEmpty() ) h->setSortIndicator( PartsTable::colLabel, Qt::AscendingOrder );
    else h->setSortIndicator( m_sort.first().col, m_sort.first().desc ? Qt::DescendingOrder : Qt::AscendingOrder );
    h->blockSignals( false );
}

void PartsPane::headerMenu( const QPoint& pos )
{
    QMenu menu;
    QHeaderView* h = m_tree->header();
    QStringList titles = PartsTable::colTitles();
    titles[PartsTable::colLock] = tr("Locked (🔒)");
    for( int col=0; col<PartsTable::colCount; ++col )
    {
        if( col == PartsTable::colLabel ) continue;      // Always there
        QAction* a = menu.addAction( titles.at( col ) );
        a->setCheckable( true );
        a->setChecked( !h->isSectionHidden( col ) );
        connect( a, &QAction::toggled, this, [this, col]( bool on ){
            m_tree->header()->setSectionHidden( col, !on );
            saveSettings();
        });
    }
    menu.addSeparator();
    QAction* bom = menu.addAction( tr("BOM order (by label)") );
    connect( bom, &QAction::triggered, this, [this](){ m_sort.clear(); updtSortIndicator(); fill(); } );
    menu.exec( h->mapToGlobal( pos ) );
}

// ----- Editing -------------------------------------------------------------------

void PartsPane::editBom( QString uid, int col, QString text )
{
    if( !PartsTable::isBomCol( col ) ) return;
    QStringList uids = { uid };
    QStringList sel = selectedUids();
    if( sel.contains( uid ) ) uids = sel;            // Several selected: all of them
    int n = PartsTable::setBom( compsOf( uids ), { { PartsTable::bomProps().at( col-PartsTable::colMfr ), text } } );
    qDebug() << "[parts] BOM" << PartsTable::colKeys().at( col ) << "=" << text << "on" << n << "parts";
    QTimer::singleShot( 0, this, [this](){ m_sig.clear(); fill(); } );   // Not under the editor's commit
}

void PartsPane::editTab( QString uid, int place )
{
    QStringList uids = { uid };
    QStringList sel = selectedUids();
    if( sel.contains( uid ) ) uids = sel;            // Several selected: they move there as a block
    TabOrder::moveTo( compsOf( uids ), place );
    QTimer::singleShot( 0, this, [this](){ m_sig.clear(); fill(); } );
}

void PartsPane::toggleLock( QTreeWidgetItem* it )
{
    if( !it || !Circuit::self() ) return;
    QString uid = it->data( 0, roleUid ).toString();
    QList<Component*> one = compsOf( { uid } );
    if( one.isEmpty() ) return;
    bool lock = !one.first()->isLocked();
    QList<Component*> parts = it->isSelected() ? compsOf( selectedUids() ) : one;
    Circuit::self()->lockParts( parts, lock );
    QTimer::singleShot( 0, this, [this](){ fill(); } );
}

void PartsPane::contextMenu( const QPoint& pos )
{
    QTreeWidgetItem* at = m_tree->itemAt( pos );
    QList<Component*> sel = compsOf( selectedUids() );
    if( at && !at->isSelected() )
    {
        m_tree->clearSelection();
        at->setSelected( true );
        sel = compsOf( selectedUids() );
    }
    bool anyLocked = false, anyFree = false, circLocked = false;
    for( Component* c : sel ){ if( c->isLocked() ) anyLocked = true; else anyFree = true; }
    if( Circuit::self() ) for( Component* c : *Circuit::self()->compList() ) if( c->isLocked() ) circLocked = true;

    QMenu menu;
    if( anyFree ){
        QAction* a = menu.addAction( tr("Lock")+"\t"+QKeySequence( Qt::CTRL | Qt::SHIFT | Qt::Key_K ).toString( QKeySequence::NativeText ) );
        connect( a, &QAction::triggered, this, [this, sel](){ Circuit::self()->lockParts( sel, true ); fill(); } );
    }
    if( anyLocked ){
        QAction* a = menu.addAction( tr("Unlock")+"\t"+QKeySequence( Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_K ).toString( QKeySequence::NativeText ) );
        connect( a, &QAction::triggered, this, [this, sel](){ Circuit::self()->lockParts( sel, false ); fill(); } );
    }
    if( circLocked ){
        QAction* a = menu.addAction( tr("Unlock all") );
        connect( a, &QAction::triggered, this, [this](){
            QList<Component*> all;
            for( Component* c : *Circuit::self()->compList() ) if( c->isLocked() ) all << c;
            Circuit::self()->lockParts( all, false );
            fill();
        });
    }
    if( !menu.isEmpty() ) menu.addSeparator();
    if( !sel.isEmpty() ){
        TabOrder::addMenu( &menu, nullptr );         // On the selection (rows = canvas selection)
        connect( &menu, &QMenu::triggered, this, [this](){ QTimer::singleShot( 0, this, [this](){ fill(); } ); } );
        menu.addSeparator();
    }
    if( sel.size() == 1 ){
        Component* c = sel.first();
        QString uid = c->getUid();
        QAction* a = menu.addAction( QIcon(":/properties.svg"), tr("Properties") );
        connect( a, &QAction::triggered, this, [this, uid](){
            QList<Component*> cs = compsOf( { uid } );
            if( !cs.isEmpty() ) cs.first()->openProperties();
        });
    }
    QModelIndex idx = m_tree->indexAt( pos );
    if( idx.isValid() && !idx.data().toString().isEmpty() ){
        QString text = idx.data().toString();
        QAction* a = menu.addAction( QIcon(":/copy.svg"), tr("Copy \"%1\"").arg( text.size() > 30 ? text.left( 28 )+"…" : text ) );
        connect( a, &QAction::triggered, this, [text](){ QApplication::clipboard()->setText( text ); } );
    }
    if( !menu.isEmpty() ) menu.exec( m_tree->viewport()->mapToGlobal( pos ) );
}

bool PartsPane::eventFilter( QObject* obj, QEvent* ev )
{
    if( obj == m_tree->viewport() && ( ev->type() == QEvent::MouseButtonPress || ev->type() == QEvent::MouseButtonDblClick ) )
    {
        QMouseEvent* me = static_cast<QMouseEvent*>( ev );
        QModelIndex idx = m_tree->indexAt( me->pos() );
        if( me->button() == Qt::LeftButton && idx.isValid() && idx.column() == PartsTable::colLock
         && !( me->modifiers() & ( Qt::ShiftModifier | Qt::ControlModifier ) ) )
        {
            if( ev->type() == QEvent::MouseButtonPress ) toggleLock( m_tree->topLevelItem( idx.row() ) );
            return true;                                 // The selection stays as it is
        }
    }
    if( obj == m_tree && ev->type() == QEvent::KeyPress && !editing() )
    {
        QKeyEvent* ke = static_cast<QKeyEvent*>( ev );
        if( ke->matches( QKeySequence::Copy ) )          // The current cell's text
        {
            QModelIndex idx = m_tree->currentIndex();
            if( idx.isValid() ) QApplication::clipboard()->setText( idx.data().toString() );
            return true;
        }
        if( ke->key() == Qt::Key_Delete || ke->key() == Qt::Key_Backspace ) return true;   // Nothing destructive here
        Qt::KeyboardModifiers m = ke->modifiers() & ~Qt::KeypadModifier;
        bool k = ke->key() == Qt::Key_K;
#if defined( Q_OS_MACOS )
        if( ke->nativeVirtualKey() == 0x28 ) k = true;
#endif
        if( k && ( m & Qt::ControlModifier ) && ( m & Qt::ShiftModifier ) )   // ⇧⌘K / ⌥⇧⌘K on the rows
        {
            Circuit::self()->lockParts( compsOf( selectedUids() ), !( m & Qt::AltModifier ) );
            fill();
            return true;
        }
    }
    return QWidget::eventFilter( obj, ev );
}

// ----- Export --------------------------------------------------------------------

void PartsPane::exportBom()
{
    QString circ = CircuitWidget::self()->curCirc();
    QFileInfo fi( circ );
    QString base = circ.isEmpty() ? QDir::homePath()+"/circuit_bom.csv"
                                  : fi.absolutePath()+"/"+fi.completeBaseName()+"_bom.csv";
    QString path = QFileDialog::getSaveFileName( this, tr("Export BOM"), base,
                                                 tr("CSV (*.csv);;Markdown (*.md)") );
    if( path.isEmpty() ) return;
    QString format = path.endsWith(".md", Qt::CaseInsensitive ) ? "md" : "csv";
    QList<PartRow> rows;
    for( const PartRow& r : PartsTable::rows() ) if( PartsTable::passes( r, m_filter ) ) rows << r;   // Search ignored
    int lines = 0;
    QString text = PartsTable::bom( rows, m_perPart->isChecked(), format, circ.isEmpty() ? "" : fi.completeBaseName(), &lines );
    QString err;
    if( !PartsTable::writeBom( path, text, format, &err ) ){ qWarning().noquote() << "[parts] "+tr("BOM not saved: ")+err; return; }
    qInfo().noquote() << tr("BOM: %1 lines, %2 parts → %3").arg( lines ).arg( rows.size() ).arg( path );
}
