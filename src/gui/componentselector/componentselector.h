/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef COMPONENTSELECTOR_H
#define COMPONENTSELECTOR_H

#include <QDir>
#include <QTreeWidget>

#include "managecomps.h"
#include "itemlibrary.h"

class Component;

class ComponentSelector : public QTreeWidget
{
    public:
        ComponentSelector( QWidget* parent );
        ~ComponentSelector();

 static ComponentSelector* self() { return m_pSelf; }

        void LoadCompSetAt( QDir compSetDir );

        QString getXmlFile( QString compName ) { return m_xmlFileList.value( compName ); }
        QString getFileDir( QString compName ) { return m_dirFileList.value( compName ); }

        void search( QString filter );

        void hideFromList( QTreeWidgetItem* item, bool hide );

        QTreeWidgetItem* getCategory( QString category );
        QStringList getCategories(){ return m_categories.keys(); }

        void setShortCuts( QHash<QString, QString> s ) { m_shortCuts = s; }
        QHash<QString, QString> getShortCuts() { return m_shortCuts; }

        // Keyboard input (K9e): the selected part (type, name as a drag from the list
        // gives them; false: none), and stepping it to the next / previous part shown
        // (categories skipped, its category opened, wrapping). The canvas keeps the focus
        bool selectedPart( QString* type, QString* name );

        // Part shortcuts (one lowercase letter per part, specs/keyboard-input.md K9f),
        // shared by the part's properties dialog, Manage Components and the CLI
 static QString keyRefusal( QString key );          // Why key can't be a part shortcut ("": it can)
        QString libraryName( Component* comp );         // Its name in the list ("": not in the list)
        QString caption( QString name );                // As the list shows it
        QString shortcutOwner( QString key, QString except = "" );   // The part that has key
        void    setShortcut( QString name, QString key, bool save=true );  // "" clears; taken from its owner
        bool    isListed( QString name );               // A part of that name in the list
        QString stepPart( int dir );                     // The new row's text, "" if none
        QString selectedGroup( bool* expanded=nullptr ); // The group row selected ("": none)
        bool    toggleSelectedGroup();                   // Expand / collapse it (false: none)
        void    showItem( QTreeWidgetItem* item );       // Scrolled into the visible rows

        // Keyboard input (K5): default single-key shortcuts (component name -> lowercase letter).
        // A user setting (Manage Components), even empty, replaces the default
 static const QHash<QString, QString>& defaultShortCuts();

    private slots:
        void slotItemClicked( QTreeWidgetItem* item, int );
        void slotContextMenu( const QPoint& );
        void slotManageComponents();

    private:
 static ComponentSelector* m_pSelf;

        void loadXml( QString setFile );

        void addItem( QString caption, QTreeWidgetItem* catItem, QString icon, QString type );

        void LoadLibraryItems();

        QTreeWidgetItem* addCategory( QString nameTr, QString name, QString parent, QString icon );

        QString getIcon( QString folder, QString name );

        QStringList m_components;
        QHash<QString, QTreeWidgetItem*> m_categories;
        QHash<QString, QString> m_catTr;

        QHash<QString, QString> m_xmlFileList;
        QHash<QString, QString> m_dirFileList;
        QHash<QString, QString> m_shortCuts;

        QDir m_compSetDir;

        manCompDialog m_mcDialog;

        ItemLibrary m_itemLibrary;
};

#endif
