/***************************************************************************
 *   Export dialog: circuit as PNG or SVG                                  *
 ***( see copyright.txt file at root folder )*******************************/

#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QSettings>

#include "exportdialog.h"
#include "circuitexport.h"
#include "instrumentview.h"
#include "mainwindow.h"
#include "circuit.h"
#include "theme.h"

ExportDialog::ExportDialog( QWidget* parent )
            : QDialog( parent )
{
    setWindowTitle( tr("Export Circuit as Image") );
    QSettings* s = MainWindow::self()->settings();

    m_format = new QComboBox( this );
    m_format->addItem("PNG", "png");
    m_format->addItem("SVG", "svg");
    m_format->setCurrentIndex( s->value("Export/format").toString() == "svg" ? 1 : 0 );

    m_theme = new QComboBox( this );              // Default: what the canvas shows now
    m_theme->addItem( tr("Current (%1)").arg( Theme::currentPreset() ), QString() );
    for( QString p : Theme::presets() ) m_theme->addItem( p, p );

    m_meters = new QCheckBox( tr("Scopes, meters and probes with their leads"), this );
    m_meters->setChecked( InstrumentView::self()->shown() );   // Default: as on the canvas

    m_grid = new QCheckBox( tr("Grid"), this );
    m_grid->setChecked( false );

    m_scale = new QDoubleSpinBox( this );
    m_scale->setRange( 0.5, 8 );
    m_scale->setSingleStep( 0.5 );
    m_scale->setDecimals( 1 );
    m_scale->setSuffix(" ×");
    m_scale->setValue( s->value("Export/scale", 2 ).toDouble() );
    m_scale->setToolTip( tr("PNG resolution: pixels per circuit unit (2 = sharp on HiDPI screens)") );

    QFormLayout* form = new QFormLayout;
    form->addRow( tr("Format:"),  m_format );
    form->addRow( tr("Theme:"),   m_theme );
    form->addRow( tr("Show:"),    m_meters );
    form->addRow( "",             m_grid );
    form->addRow( tr("PNG scale:"), m_scale );

    QDialogButtonBox* buttons = new QDialogButtonBox( QDialogButtonBox::Cancel, this );
    QPushButton* exportBtn = buttons->addButton( tr("Export..."), QDialogButtonBox::AcceptRole );
    exportBtn->setDefault( true );
    connect( buttons, &QDialogButtonBox::accepted, this, &ExportDialog::doExport );
    connect( buttons, &QDialogButtonBox::rejected, this, &QDialog::reject );
    connect( m_format, QOverload<int>::of( &QComboBox::currentIndexChanged ), this, &ExportDialog::formatChanged );
    formatChanged();

    form->addRow( buttons );
    setLayout( form );
}

void ExportDialog::formatChanged()
{
    m_scale->setEnabled( m_format->currentData().toString() == "png" );
}

void ExportDialog::doExport()
{
    QSettings* s = MainWindow::self()->settings();
    ExportOptions opt;
    opt.format = m_format->currentData().toString();
    opt.theme  = m_theme->currentData().toString();
    opt.meters = m_meters->isChecked();
    opt.grid   = m_grid->isChecked();
    opt.scale  = m_scale->value();

    QString circPath = Circuit::self()->getFilePath();
    QString dir = QFileInfo( circPath ).absolutePath();      // Next to the circuit,
    if( circPath.isEmpty() ) dir = s->value("Export/dir").toString(); // else where the last export went
    QString name = QFileInfo( circPath ).completeBaseName();
    if( name.isEmpty() ) name = "circuit";
    QString suggested = dir.isEmpty() ? name+"."+opt.format : dir+"/"+name+"."+opt.format;

    QString filter = ( opt.format == "svg" ) ? "SVG (*.svg)" : "PNG (*.png)";
    QString file = QFileDialog::getSaveFileName( this, tr("Export Circuit as Image"), suggested, filter );
    if( file.isEmpty() ) return;
    if( QFileInfo( file ).suffix().isEmpty() ) file += "."+opt.format;

    QString error;
    if( !CircuitExport::write( file, opt, nullptr, &error ) )
    {
        QMessageBox::warning( this, tr("Export Circuit as Image"), error );
        return;
    }
    s->setValue("Export/format", opt.format );
    s->setValue("Export/scale",  opt.scale );
    s->setValue("Export/dir",    QFileInfo( file ).absolutePath() );
    accept();
}

void ExportDialog::run( QWidget* parent )
{
    ExportDialog dialog( parent );
    dialog.exec();
}
