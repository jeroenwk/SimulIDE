/***************************************************************************
 *   Copyright (C) 2020 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include "datalawidget.h"
#include "uitheme.h"
#include "logicanalizer.h"
#include "mainwindow.h"

DataLaWidget::DataLaWidget( QWidget* parent, PlotBase* pb )
            : QWidget( parent )
{
    m_plotBase = pb;

    setupUi(this);

    m_chNames.append( channel0 );
    m_chNames.append( channel1 );
    m_chNames.append( channel2 );
    m_chNames.append( channel3 );
    m_chNames.append( channel4 );
    m_chNames.append( channel5 );
    m_chNames.append( channel6 );
    m_chNames.append( channel7 );

    QFont font = m_chNames.at(0)->font();
    font.setFamily( MainWindow::self()->defaultFontName() );
    font.setPixelSize( 9 );
    font.setBold( true );
    expandButton->setFont( font );

    for( QLineEdit* ch : m_chNames )
    {
        ch->setFont( font );
        ch->installEventFilter(this);
    }

    this->adjustSize();

    connect( UiTheme::self(), &UiTheme::changed, this, [this]( bool ){ updateColors(); } );
}

bool DataLaWidget::eventFilter( QObject*, QEvent* event)
{
    if( event->type() == QEvent::FocusIn ) {
        m_plotBase->setSelected( false );
    }
    return false;
}

void DataLaWidget::on_expandButton_clicked()
{ m_plotBase->toggleExpand(); }

void DataLaWidget::setColor( int ch, QColor c )
{
    m_colors[ch] = c;
    m_chNames.at( ch )->setStyleSheet( UiTheme::self()->chipSheet( c ) );
}

void DataLaWidget::updateColors()      // Dark or light mode changed
{
    for( auto it = m_colors.begin(); it != m_colors.end(); ++it )
        m_chNames.at( it.key() )->setStyleSheet( UiTheme::self()->chipSheet( it.value() ) );
}

void DataLaWidget::setTunnel( int ch, QString name )
{ m_chNames.at( ch )->setText( name ); }

void DataLaWidget::on_channel0_editingFinished()
{ m_plotBase->channelChanged( 0 , channel0->text() ); }

void DataLaWidget::on_channel1_editingFinished()
{ m_plotBase->channelChanged( 1 , channel1->text() ); }

void DataLaWidget::on_channel2_editingFinished()
{ m_plotBase->channelChanged( 2 , channel2->text() ); }

void DataLaWidget::on_channel3_editingFinished()
{ m_plotBase->channelChanged( 3 , channel3->text() ); }

void DataLaWidget::on_channel4_editingFinished()
{ m_plotBase->channelChanged( 4 , channel4->text() ); }

void DataLaWidget::on_channel5_editingFinished()
{ m_plotBase->channelChanged( 5 , channel5->text() ); }

void DataLaWidget::on_channel6_editingFinished()
{ m_plotBase->channelChanged( 6 , channel6->text() ); }

void DataLaWidget::on_channel7_editingFinished()
{ m_plotBase->channelChanged( 7 , channel7->text() ); }

