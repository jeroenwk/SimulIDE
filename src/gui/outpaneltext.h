/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef OUTPANELTEXT_H
#define OUTPANELTEXT_H

#include <QPlainTextEdit>
#include <QSyntaxHighlighter>
#include <QObject>
#include <QTextBlockUserData>

#include "updatable.h"

class OutHighlighter;

class PlainBlock : public QTextBlockUserData      // Block shown in one colour (OutPanelText::appendPlain)
{
    public:
        PlainBlock( QColor c, QString u="" ) : color( c ), uid( u ) {}
        QColor color;
        QString uid;      // Part this line is about (event log): a click selects it
};

class OutPanelText : public QPlainTextEdit, public Updatable
{
    Q_OBJECT
    public:
        OutPanelText( QWidget* parent=0 );
        ~OutPanelText();

        virtual void updateStep() override;

        void appendText( const QString text ) { m_textBuffer.append( text ); if( m_capture ) m_capture->append( text ); }
        void setCapture( QString* c ) { m_capture = c; }    // Command line interface: copy output
        void appendLine( const QString text );
        void appendPlain( const QString text, QColor color, QString uid="" );  // Lines shown in one colour, no highlighting

    protected:
        virtual void mouseReleaseEvent( QMouseEvent* event ) override;

    private:
        QString m_textBuffer;
        QString* m_capture = nullptr;
 
        OutHighlighter* m_highlighter;
};

// CLASS OutHighlighter ****************************************

class OutHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT

    public:
        OutHighlighter( QTextDocument* parent = 0 );
        ~OutHighlighter();

    protected:
        void highlightBlock( const QString &text ) override;

    private:
        struct HighlightingRule
        {
            QRegExp pattern;
            QTextCharFormat format;
        };
        QVector<HighlightingRule> highlightingRules;
};

#endif
