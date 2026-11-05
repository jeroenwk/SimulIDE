/***************************************************************************
 *   Copyright (C) 2021 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QColorDialog>
#include <QToolButton>

#include "colorval.h"
#include "appfonts.h"
#include "component.h"
#include "comproperty.h"
#include "propdialog.h"
#include "theme.h"
//#include "mainwindow.h"

ColorVal::ColorVal( PropDialog* parent, CompBase* comp, ComProperty* prop )
        : PropVal( parent, comp, prop )
{
    setupUi(this);
    m_blocked = true;
}
ColorVal::~ColorVal(){}

void ColorVal::setup( bool )
{
    //float scale = MainWindow::self()->fontScale();
    //QFont font = valLabel->font();
    //font.setPixelSize( 11.0*scale );
    //valLabel->setFont( font );
    //colorW->setFont( font );
    //valueBox->setFont( font );

    QFontMetrics fm( colorW->font() );
    float scale = AppFonts::legacySpaceWidth( colorW->font() )/2;
    colorW->setFixedWidth( 170.0*scale );

    valLabel->setText( m_property->capt() );

    QString type = m_property->type();
    if( type.startsWith("color_") ) m_keyword = type.mid( 6 );

    colorW->setProperty("uiThemeKeep", true );   // A colour swatch: shown as is in dark mode
    colorW->setReadOnly( true );
    colorW->installEventFilter(this);
    showValue( m_property->getValStr() );

    if( !m_keyword.isEmpty() )                   // Button back to "no own colour"
    {
        QToolButton* b = new QToolButton( this );
        b->setText( m_keyword == "theme" ? tr("Theme") : tr("None") );
        b->setToolTip( m_keyword == "theme" ? tr("Use the theme colour") : tr("No colour") );
        horizontalLayout->insertWidget( horizontalLayout->indexOf( colorW )+1, b );
        connect( b, &QToolButton::clicked, this, [this]()
        {
            if( m_blocked || !m_component ) return;
            prepareChange();
            m_component->setPropStr( m_propName, m_keyword );
            showValue( m_keyword );
            saveChanges();
        });
    }

    this->adjustSize();

    m_blocked = false;
}

bool ColorVal::eventFilter( QObject* object, QEvent* event)
{
    if( object == colorW && event->type() == QEvent::MouseButtonPress ) {
        changeColor();
    }
    return false;
}

void ColorVal::changeColor()
{
    if( m_blocked ) return;
    if( !m_component ) return;

    QColor start = m_color.isValid() ? m_color : Theme::idLabel();
    QColor color = QColorDialog::getColor( start, this );
    if(!color.isValid() ) return;
    m_color = color;

    prepareChange();
    m_component->setPropStr( m_propName, m_color.name() ); /// FIXME
    showValue( m_color.name() );

    saveChanges();
}

void ColorVal::showValue( QString val )
{
    QPalette pal = colorW->palette();
    if( !m_keyword.isEmpty() && val == m_keyword )   // No own colour: say so instead of a swatch
    {
        m_color = QColor();
        pal.setColor( QPalette::Base, palette().color( QPalette::Base ) );
        colorW->setPalette( pal );
        colorW->setText( m_keyword == "theme" ? tr("theme") : tr("none") );
        return;
    }
    m_color = QColor( val );
    pal.setColor( QPalette::Base, m_color );
    colorW->setPalette( pal );
    colorW->setText("");
}

void ColorVal::updtValues()
{

}

