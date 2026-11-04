/***************************************************************************
 *   Copyright (C) 2020 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include "datawidget.h"
#include "uitheme.h"
#include "oscope.h"
#include "mainwindow.h"

DataWidget::DataWidget( QWidget* parent, Oscope* oscope )
          : QWidget( parent )
{
    m_oscope = oscope;

    setupUi(this);

    m_chNames.append( channel0 );
    m_chNames.append( channel1 );
    m_chNames.append( channel2 );
    m_chNames.append( channel3 );

    QFont font;
    font.setFamily( MainWindow::self()->defaultFontName() );
    font.setPixelSize( 9 );
    font.setBold( true );
    freq0->setFont( font );
    freq1->setFont( font );
    freq2->setFont( font );
    freq3->setFont( font );
    expandButton->setFont( font );

    for( int i=0; i<4; ++i )
    {
        m_chNames.at( i )->setFont( font );
        m_chNames.at( i )->installEventFilter(this);
        setData( i, "0 Hz" );
    }

    this->adjustSize();

    connect( UiTheme::self(), &UiTheme::changed, this, [this]( bool ){ updateColors(); } );
}

bool DataWidget::eventFilter( QObject*, QEvent* event)
{
    if( event->type() == QEvent::FocusIn ) {
        m_oscope->setSelected( false );
    }
    return false;
}

void DataWidget::on_expandButton_clicked()
{ m_oscope->toggleExpand(); }

void DataWidget::setColor( int ch, QColor c )
{
    m_colors[ch] = c;
    m_chNames.at( ch )->setStyleSheet( UiTheme::self()->chipSheet( c ) );
}

void DataWidget::updateColors()      // Dark or light mode changed
{
    for( auto it = m_colors.begin(); it != m_colors.end(); ++it )
        m_chNames.at( it.key() )->setStyleSheet( UiTheme::self()->chipSheet( it.value() ) );
}

void DataWidget::setData( int ch, QString freq )
{
    switch( ch )
    {
    case 0: freq0->setText( freq ); break;
    case 1: freq1->setText( freq ); break;
    case 2: freq2->setText( freq ); break;
    case 3: freq3->setText( freq );
}   }

void DataWidget::setTunnel( int ch, QString name )
{ m_chNames.at( ch )->setText( name ); }

void DataWidget::on_channel0_editingFinished()
{ m_oscope->channelChanged( 0 , channel0->text() ); }

void DataWidget::on_channel1_editingFinished()
{ m_oscope->channelChanged( 1 , channel1->text() ); }

void DataWidget::on_channel2_editingFinished()
{ m_oscope->channelChanged( 2 , channel2->text() ); }

void DataWidget::on_channel3_editingFinished()
{ m_oscope->channelChanged( 3 , channel3->text() ); }
