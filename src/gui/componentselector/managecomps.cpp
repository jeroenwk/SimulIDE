/***************************************************************************
 *   Copyright (C) 2023 by santiago González                               *
 *   santigoro@gmail.com                                                   *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, see <http://www.gnu.org/licenses/>.  *
 *                                                                         *
 ***************************************************************************/

#include <QSettings>
#include <QCloseEvent>
#include <QMessageBox>

#include "managecomps.h"
#include "componentselector.h"
#include "mainwindow.h"
#include "uitheme.h"

manCompDialog::manCompDialog( QWidget* parent )
             : QDialog( parent )
{
    setupUi(this);

    m_initialized = false;

    table->verticalHeader()->hide(); //setVisible(False)
    table->setProperty("uiGraySelection", true );   // UiTheme: grey selection
    table->setHorizontalHeaderLabels( QStringList()<<tr("Name")<<tr("ShortCut") );
    table->setColumnWidth( 0, 300 );
    table->setColumnWidth( 1, 90 );

    connect( table, &QTableWidget::itemChanged,
              this, &manCompDialog::slotItemChanged, Qt::UniqueConnection );
}

void manCompDialog::addItem( QTreeWidgetItem* item )
{
    QString nameTr = item->text( 0 );
    QString name   = item->data( 0, Qt::WhatsThisRole ).toString();

    QTableWidgetItem* listItem = new QTableWidgetItem();
    listItem->setFlags( Qt::ItemIsEnabled | Qt::ItemIsUserCheckable );
    listItem->setText( nameTr );

    if( item->isHidden() ) listItem->setCheckState( Qt::Unchecked );
    else                   listItem->setCheckState( Qt::Checked );

    QTableWidgetItem* shortItem = new QTableWidgetItem();
    if( m_shortCuts.contains( name ) ) shortItem->setText( m_shortCuts.value( name ) );

    int row = table->rowCount();
    table->insertRow( row );
    table->setItem( row, 0, listItem );
    table->setItem( row, 1, shortItem );

    m_qtwItem[ listItem ] = item;

    int childCount = item->childCount();
    if( childCount > 0 )
    {
        listItem->setForeground( QBrush( UiTheme::headingColor() ));   // Category: as the component list, no background
        listItem->setFont( UiTheme::headingFont() );

        for( int i=0; i<childCount; i++ ) addItem( item->child( i ) );
    }
    else listItem->setIcon( QIcon(":/blanc.png") );
}

void manCompDialog::initialize()
{
    if( m_initialized ) return;

    m_shortCuts = ComponentSelector::self()->getShortCuts();
    QList<QTreeWidgetItem*> itemList = ComponentSelector::self()->findItems("",Qt::MatchStartsWith);

    for( QTreeWidgetItem* item : itemList ) addItem( item );

    m_initialized = true;
}

void manCompDialog::slotItemChanged( QTableWidgetItem* item )
{
    if( !m_initialized ) return;
    int column = item->column();
    if( column == 0 ) setItemVisible( item, item->checkState() );
    else{                  // Shortcut: one lowercase letter, not used by another part
        QString text = item->text();
        QString key  = text.left( 1 );
        QString why;
        if( key == "h" )
            why = tr("h is the pan key of the keyboard input (h, then the arrows pan the canvas).");
        else if( key == "w" )
            why = tr("w is the wire key of the keyboard input (it wires the two chosen pins).");
        else if( key == "k" )
            why = tr("k is the keyboard input's key for the part selected in the component list.");
        else if( !key.isEmpty() && !( key[0] >= 'a' && key[0] <= 'z' ) )
            why = tr("A part shortcut is one lowercase letter a–z.\n"
                     "Capital letters are the keys of switches and push buttons; "
                     "digits and - = < > . , are used by the keyboard input.");
        else if( !key.isEmpty() )
            for( int row=0; row<table->rowCount(); row++ )
            {
                QTableWidgetItem* other = table->item( row, 1 );
                if( !other || other == item || other->text() != key ) continue;
                why = tr("\"%1\" is already the shortcut of %2.").arg( key ).arg( table->item( row, 0 )->text() );
                break;
            }
        if( !why.isEmpty() ) key = "";
        if( key != text ){
            table->blockSignals( true );
            item->setText( key );
            table->blockSignals( false );
        }
        if( !why.isEmpty() ) QMessageBox::warning( this, tr("Shortcut"), why );
    }
}

void manCompDialog::setItemVisible( QTableWidgetItem* item, bool visible )
{
    QTreeWidgetItem* treeItem = m_qtwItem[ item ];
    if( !treeItem ) return;
    ComponentSelector::self()->hideFromList( treeItem, !visible );

    int childCount = treeItem->childCount();
    if( childCount > 0 )
    {
        for( int i=0; i<childCount; i++ )
        {
            QTableWidgetItem* listItem = m_qtwItem.keys( treeItem->child( i ) ).at(0);

            if( visible ) listItem->setCheckState( Qt::Checked );
            else          listItem->setCheckState( Qt::Unchecked );
}   }   }

void manCompDialog::reject()
{
    this->setVisible( false );

    QHash<QString, QString> shortCuts;

    for( int row=0; row<table->rowCount(); row++ )
    {
        QTableWidgetItem* listItem = table->item( row, 0 );
        bool hidden = ( listItem->checkState() == Qt::Unchecked );
        QString name = m_qtwItem.value( listItem )->data(0, Qt::WhatsThisRole ).toString();
        MainWindow::self()->compSettings()->setValue( name+"/hidden", hidden );

        QString shortCut = table->item( row, 1 )->text();
        if( !shortCut.isEmpty() ) shortCuts[name] = shortCut;
        if( table->item( row, 0 ) && m_qtwItem.value( listItem )->childCount() > 0 ) continue;  // Category
        // Saved even when empty, so a cleared default stays cleared
        QString def = ComponentSelector::defaultShortCuts().value( name );
        if( shortCut == def ) MainWindow::self()->compSettings()->remove( name+"/shortcut" );
        else                  MainWindow::self()->compSettings()->setValue( name+"/shortcut", shortCut );
    }
    ComponentSelector::self()->setShortCuts( shortCuts );
}
