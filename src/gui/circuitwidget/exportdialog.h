/***************************************************************************
 *   Export dialog: circuit as PNG or SVG, with theme, instruments and     *
 *   grid options                                                          *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef EXPORTDIALOG_H
#define EXPORTDIALOG_H

#include <QDialog>

class QComboBox;
class QCheckBox;
class QDoubleSpinBox;

class ExportDialog : public QDialog
{
    Q_OBJECT
    public:
        ExportDialog( QWidget* parent );

        static void run( QWidget* parent );   // Show it; on Export ask for the file and write it
        static void showNonModal( QWidget* parent ); // CLI: view export (deleted when closed)

    private:
        void doExport();
        void formatChanged();

        QComboBox*      m_format;
        QComboBox*      m_theme;
        QCheckBox*      m_meters;
        QCheckBox*      m_grid;
        QCheckBox*      m_embedFonts;
        QDoubleSpinBox* m_scale;
};

#endif
