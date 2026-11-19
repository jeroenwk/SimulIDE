/***************************************************************************
 *   Wire colours dialog (specs/wire-colors.md): line colour, band colour, *
 *   band opacity and width for one or several wires. Changes show at once *
 *   (preview); OK commits them as one undo step, Cancel restores them.    *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef WIRESTYLEDIALOG_H
#define WIRESTYLEDIALOG_H

#include <QDialog>
#include <QColor>
#include <QMap>
#include <QStringList>

class Connector;
class QPushButton;
class QSlider;
class QSpinBox;
class QLabel;

class WireStyleDialog : public QDialog
{
    public:
        // Opens it (modal) for these wires. true: changes committed
 static bool edit( QList<Connector*> wires, QWidget* parent = nullptr );

        // Sets the given properties ("color", "bandColor", "bandWidth") on the wires as
        // one undo step (also the CLI). Wires already at the value are left out
 static void apply( QList<Connector*> wires, QMap<QString,QString> props );

    private:
        WireStyleDialog( QList<Connector*> wires, QWidget* parent );

        void pickLine();
        void pickBand();
        void setLine( QColor c );
        void setBand( QColor c );
        void opacityMoved( int pct );
        void widthMoved( int w );

        void preview();            // The touched fields on every wire
        void restore();            // Every wire as it was
        void refreshSwatches();

        QList<Connector*> m_wires;
        QMap<QString,QStringList> m_orig;   // Property -> value per wire, as opened

        QColor m_line;             // Invalid: none
        QColor m_band;             // Invalid: none (alpha = opacity)
        int    m_width;
        int    m_opacity;          // %, kept for a band set again after None
        QStringList m_touched;     // Properties the user changed

        QPushButton* m_lineButton;
        QPushButton* m_bandButton;
        QSlider*     m_opacitySlider;
        QLabel*      m_opacityLabel;
        QSlider*     m_widthSlider;
        QSpinBox*    m_widthSpin;
        bool         m_updating = false;
};

#endif
