/***************************************************************************
 *   Key frames pane (specs/sim-control.md §5.1)                           *
 ***( see copyright.txt file at root folder )*******************************/

#include <QAction>
#include <QApplication>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QDebug>

#include "keyframepane.h"
#include "keyframes.h"
#include "mainwindow.h"
#include "circuitwidget.h"

KeyFramePane* KeyFramePane::m_pSelf = nullptr;

enum { cTime = 0, cNote, cSaved, cSize };

KeyFramePane::KeyFramePane( QWidget* parent, QAction* saveAct )
            : QWidget( parent )
{
    m_pSelf = this;
    setObjectName("KeyFramePane");
    QVBoxLayout* lay = new QVBoxLayout( this );
    lay->setContentsMargins( 0, 0, 0, 0 );
    lay->setSpacing( 0 );

    QHBoxLayout* top = m_topRow = new QHBoxLayout();
    top->setContentsMargins( 6, 2, 4, 2 );
    top->setSpacing( 4 );
    m_title = new QLabel( tr("Key frames"), this );   // Only in the split: the tab says it otherwise
    QFont tf = m_title->font();
    tf.setBold( true );
    m_title->setFont( tf );
    m_title->setVisible( false );
    top->addWidget( m_title );

    m_filter = new QLineEdit( this );
    m_filter->setPlaceholderText( tr("Filter: time or description") );
    m_filter->setClearButtonEnabled( true );
    m_filter->installEventFilter( this );
    connect( m_filter, &QLineEdit::textChanged, this, [this](){ applyFilter(); } );
    top->addWidget( m_filter, 1 );

    QToolButton* saveBtn = new QToolButton( this );
    saveBtn->setDefaultAction( saveAct );
    saveBtn->setAutoRaise( true );
    saveBtn->setFocusPolicy( Qt::NoFocus );
    top->addWidget( saveBtn );

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
    m_empty->setContentsMargins( 8, 6, 8, 6 );
    m_empty->setStyleSheet("QLabel { color: palette(mid); }");
    lay->addWidget( m_empty );

    m_tree = new QTreeWidget( this );
    m_tree->setColumnCount( 4 );
    m_tree->setHeaderLabels( { tr("Time"), tr("Description"), tr("Saved"), tr("Size") } );
    m_tree->setRootIsDecorated( false );
    m_tree->setUniformRowHeights( true );
    m_tree->setAlternatingRowColors( true );
    m_tree->setSelectionMode( QAbstractItemView::ExtendedSelection );
    m_tree->setEditTriggers( QAbstractItemView::NoEditTriggers );   // Double-click / Enter, on the description only
    m_tree->setContextMenuPolicy( Qt::CustomContextMenu );
    m_tree->header()->setStretchLastSection( false );
    m_tree->header()->setSectionResizeMode( cTime, QHeaderView::ResizeToContents );
    m_tree->header()->setSectionResizeMode( cNote, QHeaderView::Stretch );
    m_tree->header()->setSectionResizeMode( cSaved, QHeaderView::ResizeToContents );
    m_tree->header()->setSectionResizeMode( cSize, QHeaderView::ResizeToContents );
    m_tree->installEventFilter( this );
    lay->addWidget( m_tree, 1 );

    // A click goes there (not with Shift / ⌘: that selects); the arrow keys go along the list
    connect( m_tree, &QTreeWidget::itemClicked, this, [this]( QTreeWidgetItem* it, int )
    {
        if( QApplication::keyboardModifiers() & ( Qt::ShiftModifier | Qt::ControlModifier | Qt::MetaModifier ) ) return;
        if( m_tree->selectedItems().size() == 1 ) openItem( it );
    });
    connect( m_tree, &QTreeWidget::currentItemChanged, this, [this]( QTreeWidgetItem* it, QTreeWidgetItem* )
    {
        if( m_filling || !it || QApplication::mouseButtons() != Qt::NoButton ) return;   // A click: itemClicked
        if( QApplication::keyboardModifiers() & Qt::ShiftModifier ) return;
        if( m_tree->selectedItems().size() == 1 ) openItem( it );
    });
    connect( m_tree, &QTreeWidget::itemDoubleClicked, this, [this]( QTreeWidgetItem* it, int ){ m_tree->editItem( it, cNote ); } );
    connect( m_tree, &QTreeWidget::itemChanged, this, [this]( QTreeWidgetItem* it, int col )
    {
        if( m_filling || col != cNote ) return;
        QString err = KeyFrames::self()->setNote( it->data( cTime, Qt::UserRole ).toInt(), it->text( cNote ) );
        if( !err.isEmpty() ) qWarning().noquote() << "[keyframes] "+err;
    });
    connect( m_tree, &QTreeWidget::customContextMenuRequested, this, &KeyFramePane::contextMenu );

    connect( KeyFrames::self(), &KeyFrames::changed, this, [this](){ fill(); } );
    connect( KeyFrames::self(), &KeyFrames::noteChanged, this, [this]( int n, QString note )
    {
        for( int i=0; i<m_tree->topLevelItemCount(); ++i )
        {
            QTreeWidgetItem* it = m_tree->topLevelItem( i );
            if( it->data( cTime, Qt::UserRole ).toInt() != n || it->text( cNote ) == note ) continue;
            m_filling = true;
            it->setText( cNote, note );
            m_filling = false;
        }
        applyFilter();
    });
    connect( KeyFrames::self(), &KeyFrames::opened,  this, [this]( int n ){ markCurrent( n ); } );
    fill();
}

void KeyFramePane::setSplit( bool split )
{
    m_split = split;
    m_title->setVisible( split );
    if( split ){ m_topRow->removeWidget( m_filter ); m_topRow->insertStretch( 1, 1 ); m_filterRow->addWidget( m_filter ); }
    else if( m_filterRow->indexOf( m_filter ) >= 0 )
    {
        m_filterRow->removeWidget( m_filter );
        QLayoutItem* st = m_topRow->takeAt( 1 );         // The stretch
        delete st;
        m_topRow->insertWidget( 1, m_filter, 1 );
    }
    m_splitBtn->setToolTip( split ? tr("Put Key frames back in the tabs") : tr("Show Key frames beside the tabs (next to Messages)") );
}

void KeyFramePane::fill()
{
    QList<int> sel = selected();
    m_filling = true;
    m_tree->clear();
    QFont bold = m_tree->font();
    bold.setBold( true );
    for( const KeyFrame& k : KeyFrames::self()->list() )
    {
        QTreeWidgetItem* it = new QTreeWidgetItem( m_tree );
        it->setText( cTime, KeyFrames::timeText( k.time ) );
        it->setData( cTime, Qt::UserRole, k.n );
        it->setTextAlignment( cTime, Qt::AlignRight | Qt::AlignVCenter );
        it->setText( cNote, k.note );
        it->setText( cSaved, k.saved.toString("yyyy-MM-dd HH:mm") );
        it->setText( cSize, QString::number( ( k.size+512 )/1024 )+" KB" );
        it->setTextAlignment( cSize, Qt::AlignRight | Qt::AlignVCenter );
        it->setToolTip( cTime, tr("Key frame %1 at %2 ps").arg( k.n ).arg( k.time ) );
        it->setFlags( it->flags() | Qt::ItemIsEditable );
        if( k.n == m_current ) for( int c=0; c<4; ++c ) it->setFont( c, bold );
        if( sel.contains( k.n ) ) it->setSelected( true );
    }
    m_filling = false;
    bool none = KeyFrames::self()->list().isEmpty();
    if( none )
        m_empty->setText( KeyFrames::self()->folder().isEmpty()
            ? tr("Key frames are kept next to the circuit file: save the circuit, then pause the simulation and press ⌘K to keep its state.")
            : tr("No key frames yet. Pause the simulation where you want to come back and press ⌘K (or the ◆+ button): its state is kept with a description.") );
    m_empty->setVisible( none );
    m_tree->setVisible( !none );
    applyFilter();
}

void KeyFramePane::applyFilter()
{
    QString f = m_filter->text().trimmed();
    for( int i=0; i<m_tree->topLevelItemCount(); ++i )
    {
        QTreeWidgetItem* it = m_tree->topLevelItem( i );
        bool show = f.isEmpty() || it->text( cTime ).contains( f, Qt::CaseInsensitive )
                 || it->text( cNote ).contains( f, Qt::CaseInsensitive )
                 || it->text( cTime ).remove(' ').contains( QString( f ).remove(' '), Qt::CaseInsensitive );
        it->setHidden( !show );
    }
}

QList<int> KeyFramePane::selected()
{
    QList<int> ns;
    for( QTreeWidgetItem* it : m_tree->selectedItems() ) ns << it->data( cTime, Qt::UserRole ).toInt();
    return ns;
}

bool KeyFramePane::rangeSelection( int* from, int* to )
{
    QList<QTreeWidgetItem*> sel = m_tree->selectedItems();
    if( sel.size() < 2 ) return false;
    const KeyFrame* first = nullptr;
    const KeyFrame* last  = nullptr;
    for( QTreeWidgetItem* it : sel )
    {
        const KeyFrame* k = KeyFrames::self()->find( it->data( cTime, Qt::UserRole ).toInt() );
        if( !k ) continue;
        if( !first || k->time < first->time ) first = k;
        if( !last  || k->time > last->time  ) last  = k;
    }
    if( !first || !last || first == last ) return false;
    *from = first->n;
    *to   = last->n;
    return true;
}

void KeyFramePane::openItem( QTreeWidgetItem* it )
{
    int n = it->data( cTime, Qt::UserRole ).toInt();
    QString err = KeyFrames::self()->open( n );
    if( !err.isEmpty() ) qWarning().noquote() << "[keyframes] "+err;
}

void KeyFramePane::markCurrent( int n )
{
    m_current = n;
    QFont normal = m_tree->font();
    QFont bold = normal;
    bold.setBold( true );
    m_filling = true;
    for( int i=0; i<m_tree->topLevelItemCount(); ++i )
    {
        QTreeWidgetItem* it = m_tree->topLevelItem( i );
        bool cur = it->data( cTime, Qt::UserRole ).toInt() == n;
        for( int c=0; c<4; ++c ) it->setFont( c, cur ? bold : normal );
    }
    m_filling = false;
}

void KeyFramePane::editNew( int n )
{
    m_filter->clear();
    for( int i=0; i<m_tree->topLevelItemCount(); ++i )
    {
        QTreeWidgetItem* it = m_tree->topLevelItem( i );
        if( it->data( cTime, Qt::UserRole ).toInt() != n ) continue;
        m_filling = true;
        m_tree->clearSelection();
        m_tree->setCurrentItem( it );
        it->setSelected( true );
        m_filling = false;
        markCurrent( n );
        m_tree->scrollToItem( it );
        m_tree->setFocus();
        m_tree->editItem( it, cNote );
        return;
    }
}

void KeyFramePane::askDelete()
{
    QList<int> ns = selected();
    if( ns.isEmpty() ) return;
    QString what = ( ns.size() == 1 )
        ? tr("Delete key frame at %1?").arg( m_tree->selectedItems().first()->text( cTime ) )
        : tr("Delete these %1 key frames?").arg( ns.size() );
    QMessageBox::StandardButton b = QMessageBox::question( this, tr("Delete key frames"),
        what+"\n\n"+tr("Their files are removed from %1.").arg( KeyFrames::self()->folder() ),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel );
    if( b != QMessageBox::Yes ) return;
    QString err = KeyFrames::self()->remove( ns );
    if( !err.isEmpty() ) qWarning().noquote() << "[keyframes] "+err;
}

void KeyFramePane::contextMenu( const QPoint& pos )
{
    QTreeWidgetItem* it = m_tree->itemAt( pos );
    if( it && !it->isSelected() ){ m_tree->clearSelection(); it->setSelected( true ); m_tree->setCurrentItem( it ); }
    QMenu menu( this );
    int from = 0, to = 0;
    bool range = rangeSelection( &from, &to );
    if( it && !range ) menu.addAction( tr("Go to"), this, [this, it](){ openItem( it ); } );
    if( range ) menu.addAction( tr("Play the range"), this, [from, to](){
        QString err = KeyFrames::self()->playRange( from, to );
        if( !err.isEmpty() ) qWarning().noquote() << "[keyframes] "+err;
    });
    if( it && !range ) menu.addAction( tr("Edit description"), this, [this, it](){ m_tree->editItem( it, cNote ); } );
    if( !selected().isEmpty() )
    {
        QAction* del = menu.addAction( selected().size() > 1 ? tr("Delete %1 key frames…").arg( selected().size() )
                                                             : tr("Delete…"), this, [this](){ askDelete(); } );
        del->setShortcut( QKeySequence( Qt::Key_Backspace ) );
    }
    menu.addSeparator();
    QString folder = KeyFrames::self()->folder();
    QAction* show = menu.addAction( tr("Show folder"), this, [folder](){ QDesktopServices::openUrl( QUrl::fromLocalFile( folder ) ); } );
    show->setEnabled( !folder.isEmpty() && !KeyFrames::self()->list().isEmpty() );
    menu.exec( m_tree->viewport()->mapToGlobal( pos ) );
}

bool KeyFramePane::eventFilter( QObject* obj, QEvent* ev )
{
    if( ev->type() != QEvent::KeyPress ) return QWidget::eventFilter( obj, ev );
    QKeyEvent* ke = static_cast<QKeyEvent*>( ev );
    if( obj == m_tree )                       // While a description is edited the keys go to its editor
    {
        if( ke->key() == Qt::Key_Backspace || ke->key() == Qt::Key_Delete ){ askDelete(); return true; }
        if( ke->key() == Qt::Key_Space && !( ke->modifiers() & ~Qt::KeypadModifier ) )   // Play / pause as elsewhere
        {
            if( !ke->isAutoRepeat() ) CircuitWidget::self()->togglePlay();
            return true;
        }
        if( ( ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter ) && m_tree->currentItem() )
        {
            m_tree->editItem( m_tree->currentItem(), cNote );
            return true;
        }
    }
    if( obj == m_filter && ke->key() == Qt::Key_Down && m_tree->topLevelItemCount() )   // From the filter into the list
    {
        m_tree->setFocus();
        return true;
    }
    return QWidget::eventFilter( obj, ev );
}
