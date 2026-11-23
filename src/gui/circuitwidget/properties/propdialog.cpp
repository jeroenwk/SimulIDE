/***************************************************************************
 *   Copyright (C) 2021 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QAbstractSpinBox>

#include <QDebug>

#include <QTabBar>
#include <QLineEdit>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>

#include "propdialog.h"
#include "triggereditor.h"
#include "component.h"
#include "circuit.h"
#include "appfonts.h"
#include "component.h"
#include "circuit.h"

#include "labelval.h"
#include "numval.h"
#include "pathval.h"
#include "strval.h"
#include "textval.h"
#include "enumval.h"
#include "boolval.h"
#include "colorval.h"
#include "mainwindow.h"
#include "uitheme.h"

#include "comproperty.h"
#include "componentselector.h"

PropDialog::PropDialog( QWidget* parent, QString help )
          : QDialog( parent )
{
    setupUi( this );

    setWindowFlags( Qt::Window | Qt::WindowTitleHint | Qt::Tool
                  | Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint );

    m_component = NULL;

    type->setStyleSheet( UiTheme::headingSheet() );

    m_helpExpanded = false;
    mainLayout->removeWidget( helpText );
    helpText->setText( help );
    helpText->adjustSize();
    helpText->setVisible( false );
}

void PropDialog::setComponent( CompBase* comp, bool isComp )
{
    QFontMetrics fm( labelLabel->font() );
    m_scale = AppFonts::legacySpaceWidth( labelLabel->font() )/2.0;
    m_minW  = 265*m_scale;
    m_minH  = 100*m_scale;

    QString title = isComp ? "Uid: " : "";
    this->setWindowTitle( title+comp->getUid() );
    type->setText( "Type: "+comp->itemType() );
    if( isComp ) labelBox->setText( comp->getPropStr("label") );
    else{
        labelLabel->setVisible( false );
        labelBox->setVisible( false );
        showButton->setVisible( false );
        labelShow->setVisible( false );
    }
    tabList->clear();
    m_component = comp;
    showButton->setChecked( comp->getPropStr("Show_id") == "true" );

    int index=0;
    QList<propGroup>* groups = comp->properties();

    for( propGroup group : *groups )
    {
        if( group.flags & groupHidden ) continue;

        bool groupEnabled = true;
        bool isMaincomp = comp->isHidden() && (comp->getPropStr("mainComp") == "true"); // main Compoenent & is in subcircuit
        if( isMaincomp && (group.flags & groupNoCopy) ) groupEnabled = false;

        QList<ComProperty*> propList = group.propList;
        if( !propList.isEmpty() )
        {
            index++;
            QWidget* groupWidget = new QWidget( tabList );
            groupWidget->setLayout( new QVBoxLayout( groupWidget ));
            groupWidget->layout()->setSpacing( 9 );
            groupWidget->setSizePolicy( QSizePolicy::Preferred, QSizePolicy::Preferred );
            groupWidget->setObjectName( group.name );

            for( ComProperty* prop : propList )
            {
                if( prop->flags() & propHidden ) continue; // Property hidden

                if( prop->name() == "" ) // Just a label
                {
                    QString text = prop->capt();
                    if( text == "separator")
                    {
                        QFrame* line = new QFrame;
                        line->setFrameShape( QFrame::HLine );
                        line->setFrameShadow( QFrame::Sunken );
                        groupWidget->layout()->addWidget( line );
                    }else{
                        LabelVal* mp = new LabelVal( this );
                        mp->setLabelVal( text );
                        groupWidget->layout()->addWidget( mp );
                    }
                    continue;
                }
                QString type = prop->type();
                PropVal* mp = NULL;

                if     ( type == "double"  ) mp = new NumVal( this, comp, prop );
                else if( type.startsWith("color") ) mp = new ColorVal( this, comp, prop );
                else if( type == "uint"    ) mp = new NumVal( this, comp, prop );
                else if( type == "int"     ) mp = new NumVal( this, comp, prop );
                else if( type == "string"  ) mp = new StrVal( this, comp, prop );
                else if( type == "path"    ) mp = new PathVal( this, comp, prop );
                else if( type == "textEdit") mp = new TextVal( this, comp, prop );
                else if( type == "enum"    ) mp = new EnumVal( this, comp, prop );
                else if( type == "bool"    ) mp = new BoolVal( this, comp, prop );
                else continue;

                mp->setup( isComp );
                m_propList.append( mp );
                groupWidget->layout()->addWidget( mp );

                bool propEnabled = true;
                if( isMaincomp && (prop->flags() & propNoCopy) ) propEnabled = false;

                mp->setEnabled( groupEnabled && propEnabled );
            }
            tabList->addTab( groupWidget, group.name );
    }   }
    if( isComp )                                              // Triggers (specs/triggers.html §4), before Keyboard
        if( Component* c = dynamic_cast<Component*>( comp ) )
            if( TriggerTab::wanted( c ) ){
                QWidget* tab = ( c->itemType() == "Oscope" ) ? (QWidget*)new ScopeTriggerTab( tabList, c )   // D21
                                                             : (QWidget*)new TriggerTab( tabList, c );
                tabList->addTab( tab, tr("Triggers") );
            }
    if( isComp ) addShortcutRow( comp );                      // Last tab: Keyboard (K9f)
    if( tabList->count() == 0 ) tabList->setVisible( false ); // Hide tab widget if empty
}

PropVal* PropDialog::getPropWidget( QString propName )
{
    for( PropVal* widget : m_propList )
        if( widget->propName() == propName ) return widget;

    return NULL;
}

void PropDialog::showProp( QString name, bool show )
{
    for( PropVal* prop : m_propList )
    {
        if( prop->propName() != name ) continue;
        prop->setHidden( !show );
        if( show ) this->adjustSize();
        break;
    }
}

void PropDialog::on_labelBox_editingFinished()
{
    setUndoable("label", labelBox->text() );
}

void PropDialog::on_showButton_toggled( bool checked )
{
    QString show = checked ? "true" : "false";
    setUndoable("Show_id", show );
}

void PropDialog::on_tabList_currentChanged( int )
{
    if( !m_component ) return;
    updtValues();
    adjustWidgets();
}

void PropDialog::on_helpButton_clicked()
{
    m_helpExpanded = !m_helpExpanded;
    if( m_helpExpanded ) mainLayout->addWidget( helpText );
    else                 mainLayout->removeWidget( helpText );
    helpText->setVisible( m_helpExpanded );

    adjustWidgets();
}

void PropDialog::adjustWidgets()
{
    int h = 0;
    int w = m_minW;
    // Wide enough for every tab title: a part with many groups (e.g. a diode with
    // Capacitance) would otherwise get scroll arrows and a cut-off title
    int tabsW = 0;
    if( !tabList->isHidden() ) tabsW = qMin( tabList->tabBar()->sizeHint().width()+40*m_scale, 700*m_scale );
    if( tabsW > w ) w = tabsW;
    if( helpText->isVisible() ) w += helpText->width()+6;

    QWidget* widget = tabList->currentWidget();
    if( widget && widget->objectName() == "Triggers" )   // A list and a form: its own size
    {
        QSize hint = widget->sizeHint();
        w = qMax( w, hint.width()+24 );
        int contentW = qMax( widget->width(), hint.width() );        // Wrapped texts: their height at this width
        int contentH = widget->layout()->hasHeightForWidth() ? widget->layout()->heightForWidth( contentW ) : hint.height();
        h = qMax( hint.height(), contentH )+90*m_scale;
        this->setMinimumWidth( hint.width()+24 );
        this->setMaximumWidth( 16777215 );
        this->setFixedHeight( h );
        this->adjustSize();
        return;
    }
    if( widget )
    {
        int count = 0;
        for( int i=0; i<widget->layout()->count(); ++i )
        {
            QWidget* w = widget->layout()->itemAt(i)->widget();   // A layout or spacer: none
            if( w && !w->isHidden() ) count++;
        }

        // 24 px per row was sized for Ubuntu; a taller font (Inter) may need more: take the
        // rows' minimum height when it is larger, so nothing is squeezed. Not their size
        // hint: the row widgets ask for much more and the rows would spread out
        int estimate = count*24*m_scale;
        int minRows  = widget->layout()->minimumSize().height();
        h = qMax( estimate, minRows )+90*m_scale;
        qDebug() << "[PropDialog] rows" << count << "estimate" << estimate << "layout min" << minRows
                 << "layout hint" << widget->layout()->sizeHint().height() << "-> height" << h;
    }
    if( h < m_minH ) h = m_minH;

    this->setFixedHeight( h );
    this->setMaximumWidth( w+100*m_scale );
    this->setMinimumWidth( tabsW+( helpText->isVisible() ? helpText->width()+6 : 0 ) );
    this->adjustSize();
    this->setMaximumHeight( h+70*m_scale );
}

void PropDialog::updtValues()
{
    for( PropVal* prop : m_propList ) prop->updtValues();
}

void PropDialog::changed()
{
    Circuit::self()->setChanged();
}

// A typed value is only taken on Enter or when focus moves to another field;
// also take it when the dialog loses focus or is closed, so it is not lost.
bool PropDialog::event( QEvent* e )
{
    QEvent::Type t = e->type();
    if( t == QEvent::WindowDeactivate || t == QEvent::Hide || t == QEvent::Close )
        commitEdit();
    if( t == QEvent::Show && m_shortcutBox )             // The dialog is reused: the shortcut
        m_shortcutBox->setText( ComponentSelector::self()->getShortCuts().value( m_shortcutName ) );  // may have changed
    return QDialog::event( e );
}

void PropDialog::commitEdit()
{
    QWidget* w = focusWidget();
    if( !w ) return;
    if( QAbstractSpinBox* sb = qobject_cast<QAbstractSpinBox*>( w ) ) sb->interpretText();
    else if( w == labelBox ) on_labelBox_editingFinished();
}

void PropDialog::setUndoable( QString prop, QString val ) // Set a property as one undo step
{
    QString old = m_component->getPropStr( prop );
    if( val == old ) return;
    m_component->setPropStr( prop, val );
    Circuit::self()->saveCompChange( m_component->getUid(), prop, old );
}

// ----- The part's keyboard shortcut (K9f, specs/keyboard-input.md) ----------------

// A last tab, Keyboard (seldom used: out of the way of the common fields): the letter
// that places this kind of part at the keyboard cross. Only for parts in the list
void PropDialog::addShortcutRow( CompBase* comp )
{
    ComponentSelector* sel = ComponentSelector::self();
    Component* c = dynamic_cast<Component*>( comp );
    m_shortcutName = ( sel && c ) ? sel->libraryName( c ) : QString();
    if( m_shortcutName.isEmpty() ) return;               // Not in the component list

    QWidget* tab = new QWidget( tabList );                 // Widgets only, as the other tabs
    tab->setObjectName("Keyboard");                      // (adjustWidgets counts them)
    QVBoxLayout* col = new QVBoxLayout( tab );
    col->setSpacing( 6 );
    QWidget* rowWidget = new QWidget( tab );
    QHBoxLayout* row = new QHBoxLayout( rowWidget );
    row->setContentsMargins( 0, 0, 0, 0 );
    QLabel* label = new QLabel( tr("Shortcut:"), tab );
    m_shortcutBox = new QLineEdit( tab );
    m_shortcutBox->setFixedWidth( 34*m_scale );
    m_shortcutBox->setAlignment( Qt::AlignCenter );
    m_shortcutBox->setObjectName("shortcutBox");
    m_shortcutHint = new QLabel( tab );
    m_shortcutHint->setEnabled( false );                 // Dimmed, like a hint
    QFont f = m_shortcutHint->font();
    f.setPointSizeF( f.pointSizeF()*0.85 );
    m_shortcutHint->setFont( f );
    row->addWidget( label );
    row->addWidget( m_shortcutBox );
    row->addWidget( m_shortcutHint, 1 );
    col->addWidget( rowWidget );
    QLabel* about = new QLabel( tr("The key that places this kind of part at the keyboard cursor "
                                   "(one for all parts of this kind)."), tab );
    about->setWordWrap( true );
    about->setEnabled( false );
    about->setFont( f );
    col->addWidget( about );
    tabList->addTab( tab, tr("Keyboard") );

    m_shortcutBox->setText( sel->getShortCuts().value( m_shortcutName ) );
    updateShortcutHint();
    connect( m_shortcutBox, &QLineEdit::textEdited, this, [this]( QString t ){ shortcutEdited( t ); } );
}

void PropDialog::updateShortcutHint()
{
    QString def = ComponentSelector::defaultShortCuts().value( m_shortcutName );
    m_shortcutHint->setText( tr("a–z (h w k reserved)")
                           + ( def.isEmpty() ? QString() : tr(", default %1").arg( def ) ) );
}

// The last letter typed replaces the shortcut; empty clears it. A letter another part
// has: a dialog naming that part, Replace (it loses the letter) or Cancel
void PropDialog::shortcutEdited( QString text )
{
    ComponentSelector* sel = ComponentSelector::self();
    QString cur = sel->getShortCuts().value( m_shortcutName );
    QString key = text.right( 1 ).toLower();             // Capitals are switch keys: lowercase
    auto revert = [&](){ m_shortcutBox->setText( cur ); };

    if( key == cur ){ m_shortcutBox->setText( cur ); return; }
    QString why = ComponentSelector::keyRefusal( key );
    if( !why.isEmpty() )
    {
        QMessageBox::warning( this, tr("Shortcut"), why );
        revert();
        return;
    }
    QString owner = sel->shortcutOwner( key, m_shortcutName );
    if( !owner.isEmpty() )
    {
        QString ownerCap = sel->caption( owner );
        QString myCap    = sel->caption( m_shortcutName );
        QMessageBox box( QMessageBox::Question, tr("Shortcut already used"),
            tr("“%1” is already the shortcut of %2.").arg( key, ownerCap ), QMessageBox::NoButton, this );
        box.setInformativeText( tr("Replace: “%1” will place a %2, and %3 will have no shortcut "
                                   "(give it one in its properties or in Manage Components).")
                                .arg( key, myCap, ownerCap ) );
        QPushButton* replace = box.addButton( tr("Replace"), QMessageBox::AcceptRole );
        box.addButton( QMessageBox::Cancel );
        box.setDefaultButton( QMessageBox::Cancel );
        box.exec();
        if( box.clickedButton() != replace ){ revert(); return; }
    }
    sel->setShortcut( m_shortcutName, key );
    m_shortcutBox->setText( key );
}

