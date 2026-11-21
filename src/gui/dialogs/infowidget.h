/***************************************************************************
 *   Copyright (C) 2023 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef INFOWIDGET_H
#define INFOWIDGET_H

#include <QWidget>
#include <QPalette>

#include "ui_infowidget.h"

class InfoWidget : public QWidget, private Ui::infoWidget
{
    Q_OBJECT

    public:
        InfoWidget( QWidget* parent=0 );

 static InfoWidget* self() { return m_pSelf; }

        void setRate( double rate=0, double simLoad=0, double guiLoad=0, int fps=0 );
        void setCircTime( uint64_t tStep );
        static QString timeText( uint64_t ps );
        static QString speedText( double psPerSec );   // "1 ms/s", "0.423 ms/s"
        static QColor  slowColor() { return QColor( 230, 120, 0 ); }   // Real speed below the set speed   // "2 ms  500 µs": leading zero units left out

        enum simShown { shownStopped=0, shownRunning, shownPaused };
        void setSimState( simShown state );
        void clearLastRun() { m_lastTime = 0; setSimState( shownStopped ); } // New or opened circuit   // Label the time: stopped (reset, greyed), running, paused
        void setTargetSpeed( double s );
        void updtMcu();
        // "real 0.98 s/s (98.0 %) · sim load 12 % · GUI load 3 % · 20 FPS" (last second; "" before one)
        QString rateSummary();

        // A copy of the time in the circuit toolbar
        void setToolbarTime( QLabel* l ) { m_barTime = l; setSimState( m_shown ); }

    public slots:

    private:
        double   m_rate = 0, m_simLoad = 0, m_guiLoad = 0;   // Last values of setRate
        int      m_fps = 0;
        uint64_t m_lastTime = 0;     // Last simulation time shown (ps)
        QPalette m_timePalette;      // Normal colour of the time
        QPalette m_speedPalette;     // Normal colour of the real speed
        double   m_targetPct = 100;  // Set speed, % of real time
        bool     m_slow = false;     // Running below the set speed
        void setSlow( bool slow, double realPsPerSec=0 );
        simShown m_shown = shownStopped;
        QLabel*  m_barTime = nullptr;
        void showBarTime();

 static InfoWidget* m_pSelf;
};

#endif
