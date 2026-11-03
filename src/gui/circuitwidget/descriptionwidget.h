/***************************************************************************
 *   Circuit description: Markdown shown in the bottom pane, editable      *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef DESCRIPTIONWIDGET_H
#define DESCRIPTIONWIDGET_H

#include <QWidget>

class QTextBrowser;
class QPlainTextEdit;
class QStackedWidget;
class QToolButton;

class DescriptionWidget : public QWidget
{
    Q_OBJECT
    public:
        DescriptionWidget( QWidget* parent );

        void showCircuit();          // Show the current circuit's description (leaves edit mode)
        bool editing() const;

    private:
        void startEdit();
        void finishEdit( bool keep );

        QStackedWidget* m_stack;
        QTextBrowser*   m_view;
        QPlainTextEdit* m_edit;
        QToolButton*    m_editButton;
        QToolButton*    m_cancelButton;
};

#endif
