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

        enum simShown { shownStopped=0, shownRunning, shownPaused };
        void setSimState( simShown state );
        void clearLastRun() { m_lastTime = 0; setSimState( shownStopped ); } // New or opened circuit   // Label the time: stopped (reset, greyed), running, paused
        void setTargetSpeed( double s );
        void updtMcu();

    public slots:

    private:
        uint64_t m_lastTime = 0;     // Last simulation time shown (ps)
        QPalette m_timePalette;      // Normal colour of the time
        simShown m_shown = shownStopped;

 static InfoWidget* m_pSelf;
};

#endif
