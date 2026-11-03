/***************************************************************************
 *   Circuit description: Markdown shown in the bottom pane, editable      *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QTextBrowser>
#include <QPlainTextEdit>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QHBoxLayout>

#include "descriptionwidget.h"
#include "circuit.h"

DescriptionWidget::DescriptionWidget( QWidget* parent )
                 : QWidget( parent )
{
    setObjectName("DescriptionWidget");

    m_view = new QTextBrowser( this );
    m_view->setOpenExternalLinks( true );
    m_view->setPlaceholderText( tr("No description. Edit to add one (Markdown): what the circuit shows and how to use it.") );

    m_edit = new QPlainTextEdit( this );
    m_edit->setPlaceholderText( tr("What the circuit shows, how to use it, what to expect. Markdown: # heading, **bold**, - list, `code`.") );

    m_stack = new QStackedWidget( this );
    m_stack->addWidget( m_view );
    m_stack->addWidget( m_edit );

    m_editButton = new QToolButton( this );
    m_editButton->setText( tr("Edit") );
    m_editButton->setToolTip( tr("Edit the circuit description (saved with the circuit)") );
    m_editButton->setAutoRaise( true );
    connect( m_editButton, &QToolButton::clicked, [this](){ if( editing() ) finishEdit( true ); else startEdit(); } );

    m_cancelButton = new QToolButton( this );
    m_cancelButton->setText( tr("Cancel") );
    m_cancelButton->setAutoRaise( true );
    m_cancelButton->hide();
    connect( m_cancelButton, &QToolButton::clicked, [this](){ finishEdit( false ); } );

    QVBoxLayout* buttons = new QVBoxLayout();
    buttons->setContentsMargins( 2, 2, 2, 2 );
    buttons->addWidget( m_editButton );
    buttons->addWidget( m_cancelButton );
    buttons->addStretch();

    QHBoxLayout* layout = new QHBoxLayout( this );
    layout->setContentsMargins( 0, 0, 0, 0 );
    layout->setSpacing( 0 );
    layout->addWidget( m_stack );
    layout->addLayout( buttons );
}

bool DescriptionWidget::editing() const { return m_stack->currentWidget() == m_edit; }

void DescriptionWidget::showCircuit()
{
    QString d = Circuit::self() ? Circuit::self()->description() : QString();
    m_view->setMarkdown( d );
    m_stack->setCurrentWidget( m_view );
    m_editButton->setText( tr("Edit") );
    m_cancelButton->hide();
}

void DescriptionWidget::startEdit()
{
    m_edit->setPlainText( Circuit::self() ? Circuit::self()->description() : QString() );
    m_stack->setCurrentWidget( m_edit );
    m_editButton->setText( tr("Done") );
    m_cancelButton->show();
    m_edit->setFocus();
}

void DescriptionWidget::finishEdit( bool keep )
{
    if( keep && Circuit::self() ) Circuit::self()->setDescription( m_edit->toPlainText() );
    showCircuit();
}
