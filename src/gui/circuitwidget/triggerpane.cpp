/***************************************************************************
 *   Triggers pane (specs/triggers.html §3)                                *
 ***( see copyright.txt file at root folder )*******************************/

#include <QApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QDebug>

#include "triggerpane.h"
#include "triggereditor.h"
#include "triggers.h"
#include "circuit.h"
#include "circuitwidget.h"
#include "component.h"
#include "keyframes.h"
#include "simhistory.h"
#include "simskip.h"
#include "simulator.h"

TriggerPane* TriggerPane::m_pSelf = nullptr;

enum { cArmed = 0, cWatch, cCond, cAction, cMode, cHits, cLast, cCount };

TriggerPane::TriggerPane( QWidget* parent )
           : QWidget( parent )
{
    m_pSelf = this;
    setObjectName("TriggerPane");
    QVBoxLayout* lay = new QVBoxLayout( this );
    lay->setContentsMargins( 0, 0, 0, 0 );
    lay->setSpacing( 0 );

    QHBoxLayout* top = m_topRow = new QHBoxLayout();
    top->setContentsMargins( 6, 2, 4, 2 );
    top->setSpacing( 4 );
    m_title = new QLabel( tr("Triggers"), this );
    QFont tf = m_title->font();
    tf.setBold( true );
    m_title->setFont( tf );
    m_title->setVisible( false );
    top->addWidget( m_title );

    m_filter = new QLineEdit( this );
    m_filter->setPlaceholderText( tr("Filter: part, node, condition") );
    m_filter->setClearButtonEnabled( true );
    m_filter->installEventFilter( this );
    connect( m_filter, &QLineEdit::textChanged, this, [this](){ applyFilter(); } );
    top->addWidget( m_filter, 1 );

    QToolButton* addBtn = new QToolButton( this );
    addBtn->setText("+");
    addBtn->setToolTip( tr("New trigger…") );
    addBtn->setAutoRaise( true );
    addBtn->setFocusPolicy( Qt::NoFocus );
    connect( addBtn, &QToolButton::clicked, this, [this](){ addNew(); } );
    top->addWidget( addBtn );

    m_splitBtn = new QToolButton( this );
    m_splitBtn->setText("⧉");
    m_splitBtn->setAutoRaise( true );
    m_splitBtn->setFocusPolicy( Qt::NoFocus );
    connect( m_splitBtn, &QToolButton::clicked, this, [this](){ emit splitToggled( !m_split ); } );
    top->addWidget( m_splitBtn );
    lay->addLayout( top );
    m_filterRow = new QHBoxLayout();
    m_filterRow->setContentsMargins( 6, 0, 4, 2 );
    lay->addLayout( m_filterRow );

    m_empty = new QLabel( this );
    m_empty->setWordWrap( true );
    m_empty->setAlignment( Qt::AlignTop | Qt::AlignLeft );   // At the top, in the labels' colour
    m_empty->setContentsMargins( 8, 6, 8, 6 );
    m_empty->setText( tr("No triggers. A trigger watches a pin, a current or a part's state while the simulation runs, "
                         "and pauses, saves a key frame or counts when its condition is met. + makes one.") );
    lay->addWidget( m_empty );

    m_tree = new QTreeWidget( this );
    m_tree->setColumnCount( cCount );
    m_tree->setHeaderLabels( { "", tr("Watches"), tr("Condition"), tr("Action"), tr("Mode"), tr("Hits"), tr("Last") } );
    m_tree->setRootIsDecorated( false );
    m_tree->setUniformRowHeights( true );
    m_tree->setAlternatingRowColors( true );
    m_tree->setSelectionMode( QAbstractItemView::ExtendedSelection );
    m_tree->setEditTriggers( QAbstractItemView::NoEditTriggers );
    m_tree->setContextMenuPolicy( Qt::CustomContextMenu );
    m_tree->header()->setStretchLastSection( false );
    for( int c=0; c<cCount; ++c ) m_tree->header()->setSectionResizeMode( c, QHeaderView::ResizeToContents );
    m_tree->header()->setSectionResizeMode( cWatch, QHeaderView::Stretch );
    m_tree->headerItem()->setToolTip( cArmed, tr("Armed") );
    m_tree->installEventFilter( this );
    lay->addWidget( m_tree, 1 );

    connect( m_tree, &QTreeWidget::itemChanged, this, [this]( QTreeWidgetItem* it, int col )   // The armed box
    {
        if( m_filling || col != cArmed || !m_trig ) return;
        bool armed = it->checkState( cArmed ) == Qt::Checked;
        QList<int> ids = { it->data( cArmed, Qt::UserRole ).toInt() };
        if( it->isSelected() ) ids = selected();                 // Several selected: all of them
        m_trig->setArmed( ids, armed );
    });
    connect( m_tree, &QTreeWidget::itemDoubleClicked, this, [this]( QTreeWidgetItem* it, int col )
    {
        if( col == cArmed ) return;
        edit( it->data( cArmed, Qt::UserRole ).toInt() );
    });
    connect( m_tree, &QTreeWidget::itemClicked, this, [this]( QTreeWidgetItem* it, int col )
    {
        if( col != cLast || QApplication::keyboardModifiers() & ( Qt::ShiftModifier | Qt::ControlModifier | Qt::MetaModifier ) ) return;
        goToLast( it->data( cArmed, Qt::UserRole ).toInt() );
    });
    connect( m_tree, &QTreeWidget::itemSelectionChanged, this, [this](){ if( !m_filling ) highlightParts(); } );
    connect( m_tree, &QTreeWidget::customContextMenuRequested, this, &TriggerPane::contextMenu );
    follow();
}

void TriggerPane::follow()
{
    Triggers* t = Triggers::self();
    if( t == m_trig ){ fill(); return; }
    if( m_trig ) disconnect( m_trig, nullptr, this, nullptr );
    m_trig = t;
    if( t ){
        connect( t, &Triggers::changed, this, [this](){ fill(); } );
        connect( t, &Triggers::fired, this, [this]( int id ){ selectId( id ); emit wantShown(); } );
    }
    fill();
}

void TriggerPane::setSplit( bool split )
{
    m_split = split;
    m_title->setVisible( split );
    if( split ){ m_topRow->removeWidget( m_filter ); m_topRow->insertStretch( 1, 1 ); m_filterRow->addWidget( m_filter ); }
    else if( m_filterRow->indexOf( m_filter ) >= 0 )
    {
        m_filterRow->removeWidget( m_filter );
        QLayoutItem* st = m_topRow->takeAt( 1 );
        delete st;
        m_topRow->insertWidget( 1, m_filter, 1 );
    }
    m_splitBtn->setToolTip( split ? tr("Put Triggers back in the tabs") : tr("Show Triggers beside the tabs") );
}

static bool canGoTo( uint64_t t )   // The history (or a key frame) still holds that time
{
    Simulator* sim = Simulator::self();
    if( !sim || !sim->isRunning() || t > sim->circTime() ) return false;
    for( const KeyFrame& k : KeyFrames::self()->list() ) if( k.time == t ) return true;
    SimHistory* h = SimHistory::self();
    return h->usable() && h->count() && t >= h->firstTime();
}

void TriggerPane::fill()
{
    if( !m_trig ){ m_tree->clear(); return; }
    QList<int> sel = selected();
    m_filling = true;
    m_tree->clear();
    QColor dim = palette().color( QPalette::Disabled, QPalette::Text );
    for( const Trigger& t : m_trig->list() )
    {
        QTreeWidgetItem* it = new QTreeWidgetItem( m_tree );
        it->setFlags( it->flags() | Qt::ItemIsUserCheckable );
        it->setData( cArmed, Qt::UserRole, t.id );
        it->setCheckState( cArmed, t.armed ? Qt::Checked : Qt::Unchecked );
        QString watch = m_trig->watchText( t );
        if( !t.name.isEmpty() ) watch += "  — "+t.name;
        it->setText( cWatch, watch );
        it->setText( cCond, m_trig->condText( t ) );
        it->setText( cAction, m_trig->actionText( t ) );
        it->setText( cMode, m_trig->modeText( t ) );
        it->setText( cHits, QString::number( t.hits.size() ) );
        it->setTextAlignment( cHits, Qt::AlignRight | Qt::AlignVCenter );
        uint64_t last = t.hits.isEmpty() ? 0 : t.hits.last();
        it->setText( cLast, last ? KeyFrames::timeText( last ) : "–" );
        it->setTextAlignment( cLast, Qt::AlignRight | Qt::AlignVCenter );
        if( last && canGoTo( last ) ){
            QFont f = it->font( cLast );
            f.setUnderline( true );
            it->setFont( cLast, f );
            it->setToolTip( cLast, tr("Go there") );
        }
        QString warn = m_trig->check( t );
        if( warn.isEmpty() && t.keyframe && KeyFrames::self()->folder().isEmpty() )
            warn = tr("A key frame needs the circuit saved to a file: it pauses instead");
        if( !warn.isEmpty() ){
            it->setText( cWatch, "⚠ "+it->text( cWatch ) );
            it->setToolTip( cWatch, warn );
        }else it->setToolTip( cWatch, tr("Trigger %1").arg( t.id ) );
        if( !t.armed ) for( int c=1; c<cCount; ++c ) it->setForeground( c, dim );
        if( sel.contains( t.id ) ) it->setSelected( true );
    }
    m_filling = false;
    bool none = m_trig->list().isEmpty();
    m_empty->setVisible( none );
    m_tree->setVisible( !none );
    applyFilter();
}

void TriggerPane::applyFilter()
{
    QString f = m_filter->text().trimmed();
    for( int i=0; i<m_tree->topLevelItemCount(); ++i )
    {
        QTreeWidgetItem* it = m_tree->topLevelItem( i );
        bool show = f.isEmpty();
        for( int c=cWatch; c<=cMode && !show; ++c ) show = it->text( c ).contains( f, Qt::CaseInsensitive );
        it->setHidden( !show );
    }
}

QList<int> TriggerPane::selected()
{
    QList<int> ids;
    for( QTreeWidgetItem* it : m_tree->selectedItems() ) ids << it->data( cArmed, Qt::UserRole ).toInt();
    return ids;
}

void TriggerPane::selectId( int id )
{
    for( int i=0; i<m_tree->topLevelItemCount(); ++i )
    {
        QTreeWidgetItem* it = m_tree->topLevelItem( i );
        if( it->data( cArmed, Qt::UserRole ).toInt() != id ) continue;
        m_tree->clearSelection();
        m_tree->setCurrentItem( it );
        it->setSelected( true );
        m_tree->scrollToItem( it );
        return;
    }
}

void TriggerPane::edit( int id )
{
    if( !m_trig ) return;
    Trigger* t = m_trig->find( id );
    if( !t ) return;
    TriggerDialog* d = new TriggerDialog( this, *t, false );   // Not modal for the CLI: open(), applied on OK
    d->setAttribute( Qt::WA_DeleteOnClose );
    QPointer<Triggers> trig = m_trig;
    connect( d, &QDialog::accepted, this, [d, trig, id](){
        if( !trig ) return;
        Trigger n = d->editor()->trigger();
        n.id = id;
        trig->set( n );
    });
    d->open();
}

void TriggerPane::addNew()
{
    if( !m_trig ) return;
    Trigger t;
    t.pause = true;                       // The usual first wish: stop there
    t.cond.comp.clear();
    QList<int> sel = selected();          // A selected row's part as a start
    if( sel.size() == 1 ) if( Trigger* s = m_trig->find( sel.first() ) ) t.cond.comp = s->cond.comp;
    TriggerDialog* d = new TriggerDialog( this, t, true );
    d->setAttribute( Qt::WA_DeleteOnClose );
    QPointer<Triggers> trig = m_trig;
    connect( d, &QDialog::accepted, this, [this, d, trig](){
        if( !trig ) return;
        selectId( trig->add( d->editor()->trigger() ) );
    });
    d->open();
}

void TriggerPane::askDelete()
{
    QList<int> ids = selected();
    if( ids.isEmpty() || !m_trig ) return;
    QString what = ( ids.size() == 1 ) ? tr("Delete the trigger %1?").arg( m_tree->selectedItems().first()->text( cWatch ) )
                                       : tr("Delete these %1 triggers?").arg( ids.size() );
    QMessageBox::StandardButton b = QMessageBox::question( this, tr("Delete triggers"), what+"\n\n"+tr("Edit → Undo brings them back."),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel );
    if( b != QMessageBox::Yes ) return;
    m_trig->remove( ids );
}

void TriggerPane::armSelected( bool armed )
{
    if( m_trig ) m_trig->setArmed( selected(), armed );
}

void TriggerPane::goToLast( int id )   // To the key frame it saved there, else with the history
{
    Trigger* t = m_trig ? m_trig->find( id ) : nullptr;
    if( !t || t->hits.isEmpty() ) return;
    uint64_t when = t->hits.last();
    if( !canGoTo( when ) ) return;
    for( const KeyFrame& k : KeyFrames::self()->list() )
        if( k.time == when ){
            QString err = KeyFrames::self()->open( k.n );
            if( !err.isEmpty() ) qWarning().noquote() << "[triggers] "+err;
            return;
        }
    Simulator* sim = Simulator::self();
    if( when == sim->circTime() ) return;
    QString err = SimSkip::self()->startBack( sim->circTime()-when );
    if( !err.isEmpty() ) qWarning().noquote() << "[triggers] "+err;
}

void TriggerPane::highlightParts()   // Selected rows: their parts selected on the canvas
{
    if( !m_trig || !Circuit::self() ) return;
    QList<Component*> parts;
    for( int id : selected() )
    {
        Trigger* t = m_trig->find( id );
        if( !t ) continue;
        QString uid = t->cond.node.isEmpty() ? t->cond.comp : t->cond.node.left( t->cond.node.lastIndexOf(':') );
        for( QString u : { uid, t->cond.refComp } )
            if( Component* c = Triggers::comp( u ) ) if( !parts.contains( c ) ) parts.append( c );
    }
    if( parts.isEmpty() ) return;
    Circuit::self()->clearSelection();
    for( Component* c : parts ) c->setSelected( true );
}

void TriggerPane::contextMenu( const QPoint& pos )
{
    QTreeWidgetItem* it = m_tree->itemAt( pos );
    if( it && !it->isSelected() ){ m_tree->clearSelection(); it->setSelected( true ); m_tree->setCurrentItem( it ); }
    QList<int> ids = selected();
    QMenu menu( this );
    if( ids.size() == 1 ){
        int id = ids.first();
        menu.addAction( tr("Edit…"), this, [this, id](){ edit( id ); } );
    }
    if( !ids.isEmpty() )
    {
        int armed = 0;
        for( int id : ids ) if( m_trig->find( id ) && m_trig->find( id )->armed ) armed++;
        QString n = ids.size() > 1 ? " "+QString::number( ids.size() )+" "+tr("triggers") : "";
        if( armed < ids.size() ) menu.addAction( tr("Arm")+n, this, [this](){ armSelected( true ); } );
        if( armed > 0 ) menu.addAction( tr("Disarm")+n, this, [this](){ armSelected( false ); } );
        menu.addAction( tr("Duplicate"), this, [this, ids](){
            for( int id : ids ) if( Trigger* t = m_trig->find( id ) ) m_trig->add( *t );
        });
        menu.addAction( tr("Clear hits"), this, [this, ids](){ m_trig->clearHits( ids ); } );
        menu.addSeparator();
        QAction* del = menu.addAction( ids.size() > 1 ? tr("Delete %1 triggers…").arg( ids.size() ) : tr("Delete…"),
                                       this, [this](){ askDelete(); } );
        del->setShortcut( QKeySequence( Qt::Key_Backspace ) );
        menu.addSeparator();
    }
    menu.addAction( tr("New trigger…"), this, [this](){ addNew(); } );
    menu.exec( m_tree->viewport()->mapToGlobal( pos ) );
}

bool TriggerPane::eventFilter( QObject* obj, QEvent* ev )
{
    if( ev->type() != QEvent::KeyPress ) return QWidget::eventFilter( obj, ev );
    QKeyEvent* ke = static_cast<QKeyEvent*>( ev );
    if( obj == m_tree )
    {
        if( ke->key() == Qt::Key_Backspace || ke->key() == Qt::Key_Delete ){ askDelete(); return true; }
        if( ke->key() == Qt::Key_Space && !( ke->modifiers() & ~Qt::KeypadModifier ) )   // Play / pause as elsewhere (D23)
        {
            if( !ke->isAutoRepeat() ) CircuitWidget::self()->togglePlay();
            return true;
        }
        if( ( ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter ) && m_tree->currentItem() )
        {
            edit( m_tree->currentItem()->data( cArmed, Qt::UserRole ).toInt() );
            return true;
        }
    }
    if( obj == m_filter && ke->key() == Qt::Key_Down && m_tree->topLevelItemCount() )
    {
        m_tree->setFocus();
        return true;
    }
    return QWidget::eventFilter( obj, ev );
}
