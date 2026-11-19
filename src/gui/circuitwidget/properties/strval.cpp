/***************************************************************************
 *   Copyright (C) 2021 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>

#include "strval.h"
#include "appfonts.h"
#include "component.h"
#include "comproperty.h"
//#include "mainwindow.h"
#include "propdialog.h"

StrVal::StrVal( PropDialog* parent, CompBase* comp, ComProperty* prop )
      : PropVal( parent, comp, prop )
{
    setupUi(this);
}
StrVal::~StrVal() {}

void StrVal::setup( bool )
{
    //float scale = MainWindow::self()->fontScale();
    //QFont font = valLabel->font();
    //font.setPixelSize( 11.0*scale );
    //valLabel->setFont( font );

    QFontMetrics fm( valLabel->font() );
    float scale = AppFonts::legacySpaceWidth( valLabel->font() )/2;
    value->setFixedWidth( 192.0*scale );

    valLabel->setText( m_property->capt() );

    QString help = m_component->propHelp( m_propName );
    if( !help.isEmpty() )                 // A description under the field
    {
        value->setToolTip( help );
        QLayout* old = layout();
        QMargins m = old->contentsMargins();
        QHBoxLayout* row = new QHBoxLayout();
        row->setContentsMargins( 0, 0, 0, 0 );
        row->addWidget( valLabel );
        row->addWidget( value );
        delete old;
        QVBoxLayout* col = new QVBoxLayout( this );
        col->setContentsMargins( m );
        col->setSpacing( 2 );
        col->addLayout( row );
        QLabel* h = new QLabel( help, this );
        h->setWordWrap( true );
        QFont f = h->font();
        f.setPointSizeF( f.pointSizeF()*0.85 );
        h->setFont( f );
        h->setEnabled( false );           // Dimmed, like a hint
        col->addWidget( h );
    }
    updtValues();
    this->adjustSize();
}

void StrVal::on_value_editingFinished()
{
    prepareChange();
    m_property->setValStr( value->text() );
    saveChanges();
}

void StrVal::updtValues()
{
    QString text = m_property->getValStr();
    value->setText( text );
}
