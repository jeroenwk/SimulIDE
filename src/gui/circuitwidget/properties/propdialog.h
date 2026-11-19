/***************************************************************************
 *   Copyright (C) 2021 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef PROPDIALOG_H
#define PROPDIALOG_H

#include <QDialog>

#include "ui_propdialog.h"

class CompBase;
class PropVal;
class QLineEdit;
class QLabel;

class PropDialog : public QDialog, private Ui::PropDialog
{
    Q_OBJECT
    
    public:
        PropDialog( QWidget* parent=0, QString help="" );

        void setComponent( CompBase* comp, bool isComp=true );
        void showProp( QString name, bool show );
        void updtValues();

        void changed();

        PropVal* getPropWidget( QString propName );

        int  tabCount() { return tabList->count(); }                       // CLI: view props --tab
        void showTab( int i ) { tabList->setCurrentIndex( i ); }
        QString tabName( int i ) { return tabList->tabText( i ); }

    public slots:
        void on_labelBox_editingFinished();
        void on_showButton_toggled( bool checked );
        void on_tabList_currentChanged( int );
        void on_helpButton_clicked();
        void adjustWidgets();

    protected:
        bool event( QEvent* e ) override;

    private:
        void commitEdit();
        void setUndoable( QString prop, QString val );

        // The part's keyboard shortcut (K9f): one lowercase letter for its kind of part
        void addShortcutRow( CompBase* comp );
        void shortcutEdited( QString text );
        void updateShortcutHint();
        QString    m_shortcutName;          // The part's name in the component list
        QLineEdit* m_shortcutBox = nullptr;
        QLabel*    m_shortcutHint = nullptr;

        CompBase* m_component;

        bool m_helpExpanded;

        float m_scale;
        int m_minW;
        int m_minH;

        QList<PropVal*> m_propList;
};

#endif
