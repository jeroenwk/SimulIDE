/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include "outpaneltext.h"
#include "appfonts.h"
#include "mainwindow.h"
#include "simulator.h"
#include "circuit.h"
#include "circuitview.h"
#include "component.h"
#include "uitheme.h"
#include <QMouseEvent>

OutPanelText::OutPanelText( QWidget* parent )
            : QPlainTextEdit( parent )
            , Updatable()
{
    m_textBuffer = "";

    m_highlighter = new OutHighlighter( document() );

    setProperty("uiThemeKeep", true );     // Sets its own colours for both themes
    applyTheme( UiTheme::self()->isDark() );
    connect( UiTheme::self(), &UiTheme::changed, this, &OutPanelText::applyTheme );
    
    QFont font;
    font.setFamily( AppFonts::mono() );
    font.setWeight( 50 );
    font.setFixedPitch(true);
    font.setPixelSize( 13*MainWindow::self()->fontScale() );
    setFont( font );

    this->document()->setDocumentMargin( 9 );

    setMaximumBlockCount( 1000 );
}
OutPanelText::~OutPanelText(){}

void OutPanelText::applyTheme( bool dark )
{
    m_dark = dark;
    QPalette p = palette();
    for( QPalette::ColorGroup g : { QPalette::Active, QPalette::Inactive, QPalette::Disabled } )
    {
        p.setColor( g, QPalette::Base, dark ? QColor( 35, 30, 60 )    : QColor( 255, 255, 255 ) );
        p.setColor( g, QPalette::Text, dark ? QColor( 190, 190, 150 ) : QColor( 45, 45, 40 ) );
    }
    setPalette( p );
    m_highlighter->setLight( !dark );
    m_highlighter->rehighlight();
}

void OutPanelText::appendLine( const QString text )
{
    m_textBuffer.append( text+"\n" );
    if( m_capture ) m_capture->append( text+"\n" );
    if( !Simulator::self() || !Simulator::self()->isRunning() )
    {
        updateStep();
        repaint();
    }
}

void OutPanelText::appendPlain( const QString text, QColor color, QString uid, int64_t time, QColor lightColor )
{
    updateStep();                                  // Keep the order with buffered text
    if( m_capture ) m_capture->append( text+"\n" );

    QTextCursor cursor( document() );
    cursor.movePosition( QTextCursor::End );
    if( time >= 0 )                                // Before event lines with a later time
    {
        QTextBlock b = cursor.block().previous();  // The last line (the end is an empty block)
        QTextBlock at;
        for( int n=0; b.isValid() && n<2000; ++n, b = b.previous() )
        {
            PlainBlock* d = dynamic_cast<PlainBlock*>( b.userData() );
            if( !d || d->time < 0 || d->time <= time ) break;
            at = b;
        }
        if( at.isValid() ) cursor.setPosition( at.position() );
    }
    int first = cursor.blockNumber();
    cursor.insertText( text+"\n" );
    int last = cursor.blockNumber();
    for( int i=first; i<last; ++i )               // Mark the blocks: the highlighter paints them in one colour
    {
        QTextBlock b = document()->findBlockByNumber( i );
        b.setUserData( new PlainBlock( color, uid, time, lightColor ) );
        m_highlighter->rehighlightBlock( b );
    }
    moveCursor( QTextCursor::End );
    if( !Simulator::self() || !Simulator::self()->isRunning() ) repaint();
}

void OutPanelText::mouseReleaseEvent( QMouseEvent* event )
{
    QPlainTextEdit::mouseReleaseEvent( event );
    if( event->button() != Qt::LeftButton || textCursor().hasSelection() ) return;

    QTextBlock block = cursorForPosition( event->pos() ).block();   // An event log line: show its part
    PlainBlock* data = dynamic_cast<PlainBlock*>( block.userData() );
    if( !data || data->uid.isEmpty() ) return;
    for( Component* comp : *Circuit::self()->compList() )
    {
        if( comp->getUid() != data->uid ) continue;
        Circuit::self()->clearSelection();
        comp->setSelected( true );
        CircuitView::self()->centerOn( comp );
        break;
    }
}

void OutPanelText::updateStep()
{
    if( m_textBuffer.isEmpty() ) return;

    moveCursor( QTextCursor::End );
    insertPlainText( m_textBuffer );
    m_textBuffer.clear();

    if( this->document()->characterCount() > 100000 )
        setPlainText( this->toPlainText().right( 90000 ) );

    moveCursor( QTextCursor::End );
}

// CLASS OutHighlighter ***********************************************

OutHighlighter::OutHighlighter( QTextDocument* parent )
              : QSyntaxHighlighter( parent )
{
    HighlightingRule rule;
    QTextCharFormat format;

    format.setForeground( QColor(110, 180, 100) );
    rule.pattern = QRegExp( "/[^\n]*" );
    rule.format = format;
    highlightingRules.append( rule );

    format.setForeground( QColor(140, 150, 240) );
    rule.pattern = QRegExp("\\b[0-9]+\\b");
    rule.format = format;
    highlightingRules.append(rule);

    format.setForeground( QColor(140, 150, 240) );
    rule.pattern = QRegExp("0[xX][0-9a-fA-F]+");
    rule.format = format;
    highlightingRules.append(rule);

    format.setForeground( QColor(50, 200, 100) );
    rule.pattern = QRegExp( "\".*\"" );
    rule.format = format;
    highlightingRules.append( rule );

    format.setForeground( QColor(200, 255, 200) );
    format.setFontWeight( QFont::Bold );
    rule.pattern = QRegExp("\\bSUCCESS\\b");
    rule.format = format;
    highlightingRules.append(rule);
    
    format.setForeground( QColor(255, 255, 200) );
    rule.pattern = QRegExp("SENT:");
    rule.format = format;
    highlightingRules.append(rule);

    format.setForeground( QColor(255, 200, 100) );
    rule.pattern = QRegExp("\\bWARNING\\b");
    rule.format = format;
    highlightingRules.append(rule);

    format.setForeground( QColor(255, 120, 120) );
    rule.pattern = QRegExp("\\bFAIL\\b");
    rule.format = format;
    highlightingRules.append(rule);

    format.setForeground( QColor(100, 50, 0) );
    format.setBackground( QColor(255, 255, 100) );
    rule.pattern = QRegExp("\\bERROR\\b");
    rule.format = format;
    highlightingRules.append(rule);
}
OutHighlighter::~OutHighlighter(){}

void OutHighlighter::highlightBlock( const QString &text )
{
    if( PlainBlock* p = dynamic_cast<PlainBlock*>( currentBlockUserData() ) )
    {
        QTextCharFormat f;
        if( !m_light )                   f.setForeground( p->color );
        else if( p->lightColor.isValid() ) f.setForeground( p->lightColor );
        else                             f.setForeground( UiTheme::onLight( p->color ) );
        setFormat( 0, text.length(), f );
        return;
    }
    QString upText = text;
    upText = upText.toUpper(); // Do case insensitive

    for( const HighlightingRule &rule : highlightingRules )
    {
        QRegExp expression( rule.pattern );
        int index = expression.indexIn( upText );
        while( index >= 0 )
        {
            int length = expression.matchedLength();
            QTextCharFormat format = rule.format;
            if( m_light && !format.hasProperty( QTextFormat::BackgroundBrush ) )   // Colours made for the dark background
                format.setForeground( UiTheme::onLight( format.foreground().color() ) );
            setFormat( index, length, format );
            index = expression.indexIn( upText, index + length );
}   }   }

#include "moc_outpaneltext.cpp"
