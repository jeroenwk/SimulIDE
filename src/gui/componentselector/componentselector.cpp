/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QApplication>
#include <QDomDocument>
#include <QMessageBox>
#include <QXmlStreamReader>
#include <QMimeData>
#include <QSettings>
#include <QDebug>
#include <QDrag>
#include <QTreeWidgetItemIterator>
#include <QMenu>
#include <QScrollBar>
#include <QPointer>
#include <QTimer>

#include "componentselector.h"
#include "component.h"
#include "chip.h"
#include "mainwindow.h"
#include "uitheme.h"

ComponentSelector* ComponentSelector::m_pSelf = NULL;

ComponentSelector::ComponentSelector( QWidget* parent )
                 : QTreeWidget( parent )
                 , m_mcDialog( this )
                 , m_itemLibrary()
{
    m_pSelf = this;

    m_mcDialog.setVisible( false );

    setDragEnabled(true);
    setDragDropMode( QAbstractItemView::DragOnly );
    //setAlternatingRowColors(true);
    setIndentation( 12 );
    setProperty("uiGraySelection", true );   // UiTheme: grey selection, not the accent blue
    setRootIsDecorated( true );
    setCursor( Qt::OpenHandCursor );
    headerItem()->setHidden( true );

    float scale = MainWindow::self()->fontScale();
    setIconSize( QSize( 30*scale, 24*scale ));
    //setIconSize( QSize( 36, 24 ));

    LoadLibraryItems();

    setContextMenuPolicy( Qt::CustomContextMenu );

    connect( this, &ComponentSelector::customContextMenuRequested,
             this, &ComponentSelector::slotContextMenu, Qt::UniqueConnection );

    connect( this, &ComponentSelector::itemPressed,
             this, &ComponentSelector::slotItemClicked, Qt::UniqueConnection );
}
ComponentSelector::~ComponentSelector(){}

void ComponentSelector::LoadLibraryItems()
{
    for( LibraryItem* item : m_itemLibrary.items() )
    {
        QString category = item->category();

        QString icon = item->iconfile();
        QString iconFile = MainWindow::self()->getDataFilePath("images/"+icon );
        if( !QFile::exists( iconFile ) ) iconFile = ":/"+icon; // Image not in simulide data folder, use hardcoded image

        if( item->createItemFnPtr() )
        {
            QTreeWidgetItem* catItem = getCategory( category );
            if( catItem ) addItem( item->name(), catItem, iconFile, item->type() );
        }
        else addCategory( item->name(), item->type(), category, iconFile );
    }
}

void ComponentSelector::LoadCompSetAt( QDir compSetDir )
{
    m_compSetDir = compSetDir;

    compSetDir.setNameFilters( QStringList( "*.xml" ) );

    QStringList xmlList = compSetDir.entryList( QDir::Files );

    //if( xmlList.isEmpty() ) return;                  // No comp sets to load

    qDebug() << "\n" << tr("    Loading Component sets at:")<< "\n" << compSetDir.absolutePath()<<"\n";

    for( QString compSetName : xmlList )
    {
        QString compSetFilePath = compSetDir.absoluteFilePath( compSetName );
        if( !compSetFilePath.isEmpty() ) loadXml( compSetFilePath );
    }
    if( compSetDir.cd("test") )
    {
        QStringList dirList = compSetDir.entryList( {"*"}, QDir::Dirs );
        if( !dirList.isEmpty() )
        {
            QTreeWidgetItem* catItem = getCategory("test");
            if( !catItem ) catItem = addCategory("test","test","","" );

            for( QString compName : dirList )
            {
                QString path = compName+"/"+compName;
                QString icon = getIcon( "test", compName );
                QString type;

                if( compSetDir.exists( path+".sim1") )
                {
                    if( icon.isEmpty() ) icon = ":/subc2.png";
                    type = "Subcircuit";
                }
                else if( compSetDir.exists( path+".mcu") )
                {
                    if( icon.isEmpty() ) icon = ":/ic2.png";
                    type = "MCU";
                }
                if( !type.isEmpty() )
                {
                    addItem( compName, catItem, icon, type );
                    m_dirFileList[ compName ] = compSetDir.absoluteFilePath( compName );
                }
            }
        }
    }
    qDebug() << "\n";
}

void ComponentSelector::loadXml( QString setFile )
{
    QFile file( setFile );
    if( !file.open(QFile::ReadOnly | QFile::Text) )
    {
          QMessageBox::warning( 0, "ComponentSelector::loadXml"
                              , tr("Cannot read file %1:\n%2.").arg(setFile).arg(file.errorString()));
          return;
    }
    QXmlStreamReader reader(&file);
    if( reader.readNextStartElement() )
    {
        if( reader.name() != "itemlib" )
        {
            QMessageBox::warning( 0, "ComponentSelector::loadXml"
                                , tr("Error parsing file (itemlib):\n%1.").arg(setFile) );
            file.close();
            return;
        }
        while( reader.readNextStartElement() )
        {
            if( reader.name() != "itemset" ) { reader.skipCurrentElement(); continue;}

            QString icon = "";
            if( reader.attributes().hasAttribute("icon") )
            {
                icon = reader.attributes().value("icon").toString();
                if( !icon.startsWith(":/") )
                    icon = MainWindow::self()->getDataFilePath("images/"+icon);
            }

            QString category = reader.attributes().value("category").toString();
            QStringList catPath = category.split("/");

            QTreeWidgetItem* catItem = NULL;
            QString parent = "";
            category = "";
            while( !catPath.isEmpty() )
            {
                parent = category;
                category = catPath.takeFirst();
                catItem = getCategory( category );
                if( !catItem /*&& !parent.isEmpty()*/ )
                {
                    QString catTr = QObject::tr( category.toLocal8Bit() );
                    catItem = addCategory( catTr, category, parent, icon );
                }
            }

            QString type = reader.attributes().value("type").toString();
            QString folder = reader.attributes().value("folder").toString();

            while( reader.readNextStartElement() )
            {
                if( reader.name() == "item")
                {
                    QString name = reader.attributes().value("name").toString();

                    if( reader.attributes().hasAttribute("icon") )
                    {
                        icon = reader.attributes().value("icon").toString();
                        if( !icon.startsWith(":/") )
                            icon = MainWindow::self()->getDataFilePath("images/"+icon);
                    }
                    else icon = getIcon( folder, name );

                    if( !m_components.contains( name ) )
                    {
                        m_components.append( name );

                        m_xmlFileList[ name ] = setFile;   // Save xml File used to create this item
                        if( reader.attributes().hasAttribute("info") )
                            name += "???"+reader.attributes().value("info").toString();

                        if( catItem ) addItem( name, catItem, icon, type );
                    }
                    reader.skipCurrentElement();
    }   }   }   }
    QString compSetName = setFile.split( "/").last();
    //m_compSetUnique.append( compSetName );
    qDebug() << tr("        Loaded Component set:           ") << compSetName;
}

QString ComponentSelector::getIcon( QString folder, QString name )
{
    QString icon = folder+"/"+name+"/"+name+"_icon.png";
    if( m_compSetDir.exists( icon ) ) icon = m_compSetDir.absoluteFilePath( icon );
    else                              icon = "";
    return icon;
}

void ComponentSelector::addItem( QString caption, QTreeWidgetItem* catItem, QString icon, QString type )
{
    QStringList nameFull = caption.split( "???" );
    QString       nameTr = nameFull.first();
    QString info = "";
    if( nameFull.size() > 1 ) info = "   "+nameFull.last();

    QTreeWidgetItem* item = new QTreeWidgetItem(0);
    float scale = MainWindow::self()->fontScale();
    QFont font;
    font.setFamily( MainWindow::self()->defaultFontName() );
    font.setBold( true );
    font.setPixelSize( 11*scale );

    item->setFlags( QFlag(32) );
    item->setFont( 0, font );
    item->setIcon( 0, QIcon( QPixmap( icon ) ) );
    item->setText( 0, nameTr+info );
    item->setData( 0, Qt::UserRole, type );

    if( type == "Subcircuit" || type == "MCU" )
         item->setData( 0, Qt::WhatsThisRole, nameTr );
    else item->setData( 0, Qt::WhatsThisRole, type );

    catItem->addChild( item );

    QString name = item->data( 0, Qt::WhatsThisRole ).toString();
    bool hidden = MainWindow::self()->compSettings()->value( name+"/hidden" ).toBool();
    hideFromList( item, hidden );

    QSettings* cs = MainWindow::self()->compSettings();
    QString shortCut = cs->contains( name+"/shortcut" ) ? cs->value( name+"/shortcut" ).toString()
                                                         : defaultShortCuts().value( name );
    if( !shortCut.isEmpty() ) m_shortCuts[name] = shortCut;
}

const QHash<QString, QString>& ComponentSelector::defaultShortCuts()
{
    static const QHash<QString, QString> keys = {
        {"Oscope", "o"},      {"Amperimeter", "a"},   {"Voltimeter", "v"}, {"FreqMeter", "f"},
        {"Fixed Voltage", "x"}, {"Battery", "y"},     {"WaveGen", "n"},    {"Ground", "g"},
        {"Push", "b"},        {"Switch", "s"},        {"Resistor", "r"},   {"Potentiometer", "p"},
        {"elCapacitor", "e"}, {"Capacitor", "c"},     {"Inductor", "i"},   {"Diode", "d"},
        {"Zener", "z"},       {"Mosfet", "m"},        {"Jfet", "j"},       {"BJT", "t"},
        {"UJT", "q"},         {"opAmp", "u"},         {"Led", "l"},
    };
    return keys;
}

QTreeWidgetItem* ComponentSelector::getCategory( QString category )
{
    QTreeWidgetItem* catItem = NULL;
    if( m_categories.contains( category ) ) catItem = m_categories.value( category );
    else{
        category = m_catTr.value( category );
        if( !category.isEmpty() && m_categories.contains( category ) )
            catItem = m_categories.value( category );
    }
    return catItem;
}

QTreeWidgetItem* ComponentSelector::addCategory( QString nameTr, QString name, QString parent, QString icon )
{
    QTreeWidgetItem* catItem = NULL;

    QFont font = UiTheme::headingFont();
    float fontScale = MainWindow::self()->fontScale();
    bool expanded = false;

    if( parent.isEmpty() )                              // Is Main Category
    {
        catItem = new QTreeWidgetItem( this );
        catItem->setIcon( 0, QIcon(":/null-0.png") );
        catItem->setTextColor( 0, UiTheme::headingColor() );   // Light purple in dark mode
        expanded = true;
    }else{
        catItem = new QTreeWidgetItem(0);
        catItem->setIcon( 0, QIcon( QPixmap( icon ) ) );
        font.setPixelSize( 12*fontScale );
    }
    catItem->setFlags( QFlag(32) );

    catItem->setFont( 0, font );
    catItem->setText( 0, nameTr );
    catItem->setChildIndicatorPolicy( QTreeWidgetItem::ShowIndicator );
    catItem->setData( 0, Qt::WhatsThisRole, name );
    m_categories.insert( name, catItem );
    m_catTr.insert( nameTr, name );

    if( parent.isEmpty() ) addTopLevelItem( catItem ); // Is root category or root category doesn't exist
    else if( m_categories.contains( parent ) )
        m_categories.value( parent )->addChild( catItem );

    if( MainWindow::self()->compSettings()->contains(name+"/collapsed") )
        expanded = !MainWindow::self()->compSettings()->value( name+"/collapsed" ).toBool();
    catItem->setExpanded( expanded );
    catItem->setData( 0, Qt::UserRole+2, expanded );

    bool hidden = MainWindow::self()->compSettings()->value( name+"/hidden" ).toBool();
    hideFromList( catItem, hidden );

    return catItem;
}

void ComponentSelector::slotItemClicked( QTreeWidgetItem* item, int  )
{
    if( !item ) return;

    QString type = item->data( 0, Qt::UserRole ).toString();
    if( type == "" ) return;

    QString name = item->data (0, Qt::WhatsThisRole ).toString(); //item->text(0);
    QMimeData* mimeData = new QMimeData;
    mimeData->setText( name );
    mimeData->setHtml( type );              // esto hay que revisarlo

    QDrag* drag = new QDrag( this );
    drag->setMimeData( mimeData );
    drag->exec( Qt::CopyAction | Qt::MoveAction, Qt::CopyAction );
}

void ComponentSelector::slotContextMenu( const QPoint& point )
{
    QMenu menu;

    QAction* manageComponents = menu.addAction( QIcon(":/fileopen.png"),tr("Manage Components") );
    connect( manageComponents, &QAction::triggered,
             this, &ComponentSelector::slotManageComponents, Qt::UniqueConnection );

    menu.exec( mapToGlobal(point) );
}

void ComponentSelector::slotManageComponents()
{
    m_mcDialog.initialize();
    m_mcDialog.setVisible( true );
}

void ComponentSelector::hideFromList( QTreeWidgetItem* item, bool hide )
{
    item->setData( 0, Qt::UserRole+1, hide );
    item->setHidden( hide );
}

void ComponentSelector::search( QString filter )
{
    QList<QTreeWidgetItem*>    cList = findItems( filter, Qt::MatchContains|Qt::MatchRecursive, 0 );
    QList<QTreeWidgetItem*> allItems = findItems( "", Qt::MatchContains|Qt::MatchRecursive, 0 );

    for( QTreeWidgetItem* item : allItems )
    {
        item->setHidden( true );

        if( item->childCount() > 0  )
        {
            item->setExpanded( item->data( 0, Qt::UserRole+2 ).toBool() );
            continue;
        }
        if( !cList.contains( item ) ) continue;

        bool hidden = item->data( 0, Qt::UserRole+1 ).toBool();
        while( item ){
            item->setHidden( hidden );
            if( item->childCount() > 0 && !hidden && !filter.isEmpty() ) item->setExpanded( true );
            item = item->parent();
        }
    }
}

static bool isPartItem( QTreeWidgetItem* item )
{ return item && !item->data( 0, Qt::UserRole ).toString().isEmpty(); }

static bool shownItem( QTreeWidgetItem* item )   // Not hidden (Manage Components, search)
{
    for( QTreeWidgetItem* i = item; i; i = i->parent() ) if( i->isHidden() ) return false;
    return true;
}

bool ComponentSelector::selectedPart( QString* type, QString* name )
{
    QTreeWidgetItem* item = currentItem();
    if( !isPartItem( item ) || !shownItem( item ) ) return false;
    *type = item->data( 0, Qt::UserRole ).toString();
    *name = item->data( 0, Qt::WhatsThisRole ).toString();
    return true;
}

QString ComponentSelector::selectedGroup( bool* expanded )
{
    QTreeWidgetItem* item = currentItem();
    if( !item || isPartItem( item ) || !shownItem( item ) ) return "";
    if( expanded ) *expanded = item->isExpanded();
    return item->text( 0 ).trimmed();
}

bool ComponentSelector::toggleSelectedGroup()
{
    QTreeWidgetItem* item = currentItem();
    if( !item || isPartItem( item ) || !shownItem( item ) ) return false;
    item->setExpanded( !item->isExpanded() );
    showItem( item );
    return true;
}

// The next / previous row the list shows, parts and groups alike: a collapsed group is
// not opened (its parts are not shown, so they are skipped). Wraps; none yet: the first /
// last row. The canvas keeps the keyboard
QString ComponentSelector::stepPart( int dir )
{
    QList<QTreeWidgetItem*> rows;                    // Every row shown, in list order
    QTreeWidgetItemIterator it( this );
    for( ; *it; ++it )
    {
        QTreeWidgetItem* i = *it;
        if( !shownItem( i ) ) continue;
        bool open = true;                            // Inside a collapsed group: not shown
        for( QTreeWidgetItem* p = i->parent(); p; p = p->parent() ) if( !p->isExpanded() ) open = false;
        if( open ) rows.append( i );
    }
    if( rows.isEmpty() ) return "";

    int i = rows.indexOf( currentItem() );
    int n = rows.size();
    if( i < 0 ) i = ( dir > 0 ) ? 0 : n-1;
    else        i = ( ( i+dir ) % n + n ) % n;
    QTreeWidgetItem* item = rows.at( i );
    setCurrentItem( item );
    item->setSelected( true );
    showItem( item );
    return item->text( 0 ).trimmed();
}

// Scrolls the list until item's row is inside it. scrollToItem() left it below the
// visible rows while stepping with : (the scroll bar did not move); the scroll bar is
// stepped by hand instead, one row (or a few pixels) at a time, until the row fits
void ComponentSelector::showItem( QTreeWidgetItem* item )
{
    scrollToItem( item );
    QScrollBar* sb = verticalScrollBar();
    int h = viewport()->height();
    for( int guard=0; guard<1000; ++guard )
    {
        QRect r = visualItemRect( item );
        if     ( r.bottom() > h && sb->value() < sb->maximum() ) sb->setValue( sb->value()+1 );
        else if( r.top() < 0    && sb->value() > sb->minimum() ) sb->setValue( sb->value()-1 );
        else break;
    }
}

// ----- Part shortcuts (K9f) ---------------------------------------------------

QString ComponentSelector::keyRefusal( QString key )
{
    if( key.isEmpty() ) return "";
    if( key.size() != 1 || key[0] < 'a' || key[0] > 'z' )
        return tr("A part shortcut is one lowercase letter a–z.\n"
                  "Capital letters are the keys of switches and push buttons; "
                  "digits and - = + < > : / . , are used by the keyboard input.");
    if( key == "h" ) return tr("h is the pan key of the keyboard input (h, then the arrows pan the canvas).");
    if( key == "w" ) return tr("w is the wire key of the keyboard input (it wires the two chosen pins).");
    if( key == "k" ) return tr("k is the keyboard input's key for the part selected in the component list.");
    return "";
}

QString ComponentSelector::libraryName( Component* comp )
{
    if( !comp || comp->isHidden() || comp->itemType() == "Node" ) return "";
    QString name = comp->itemType();
    if( name == "Subcircuit" || name == "MCU" )          // Listed by their device name
        if( Chip* chip = dynamic_cast<Chip*>( comp ) ) name = chip->name();
    return isListed( name ) ? name : QString();
}

bool ComponentSelector::isListed( QString name )
{
    QTreeWidgetItemIterator it( this );
    for( ; *it; ++it )
        if( isPartItem( *it ) && (*it)->data( 0, Qt::WhatsThisRole ).toString() == name ) return true;
    return false;
}

QString ComponentSelector::caption( QString name )
{
    QTreeWidgetItemIterator it( this );
    for( ; *it; ++it )
        if( isPartItem( *it ) && (*it)->data( 0, Qt::WhatsThisRole ).toString() == name )
            return (*it)->text( 0 ).trimmed();
    return name;
}

QString ComponentSelector::shortcutOwner( QString key, QString except )
{
    if( key.isEmpty() ) return "";
    for( auto it = m_shortCuts.constBegin(); it != m_shortCuts.constEnd(); ++it )
        if( it.value() == key && it.key() != except ) return it.key();
    return "";
}

// Saved even when empty, so a cleared default stays cleared (as Manage Components does)
static void saveShortcut( QString name, QString key )
{
    QSettings* cs = MainWindow::self()->compSettings();
    if( key == ComponentSelector::defaultShortCuts().value( name ) ) cs->remove( name+"/shortcut" );
    else                                                             cs->setValue( name+"/shortcut", key );
}

// save false: in memory only (a headless CLI server shares the user's settings)
void ComponentSelector::setShortcut( QString name, QString key, bool save )
{
    QString owner = shortcutOwner( key, name );
    if( !owner.isEmpty() )                               // Replaced: the owner loses it
    {
        m_shortCuts.remove( owner );
        if( save ) saveShortcut( owner, "" );
    }
    if( key.isEmpty() ) m_shortCuts.remove( name );
    else                m_shortCuts[ name ] = key;
    if( save ) saveShortcut( name, key );
    qDebug() << "[kbd] part shortcut" << name << "=" << key << ( owner.isEmpty() ? "" : "taken from "+owner );
}

