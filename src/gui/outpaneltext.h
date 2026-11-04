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
        PlainBlock( QColor c, QString u="", int64_t t=-1 ) : color( c ), uid( u ), time( t ) {}
        QColor color;
        QString uid;      // Part this line is about (event log): a click selects it
        int64_t time;     // Simulated time of an event log line (ps), -1 for other text
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
        // Lines shown in one colour, no highlighting. With a simulated time (event log), the line
        // goes in time order among the event lines above it: a line found later (a capacitor's
        // "charged", dated back to when it happened) is not left below later events
        void appendPlain( const QString text, QColor color, QString uid="", int64_t time=-1 );

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
