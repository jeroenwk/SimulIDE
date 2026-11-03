/***************************************************************************
 *   Copyright (C) 2010 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef CIRCUITWIDGET_H
#define CIRCUITWIDGET_H

#include <QMenu>
#include <QToolBar>
#include <QComboBox>
#include <QVBoxLayout>

#include "circuitview.h"
#include "outpaneltext.h"

class QSplitter;
class QLabel;
class AboutDialog;
class AppDialog;
class InfoWidget;
class DescriptionWidget;
class QTabWidget;

class CircuitWidget : public QWidget
{
    Q_OBJECT

    public:
        CircuitWidget( QWidget *parent );
        ~CircuitWidget();

 static CircuitWidget* self() { return m_pSelf; }

        void clear();

        void setError( QString error );
        void setMsg(QString msg , int type);

        void syncTimebase( uint64_t psPerSec );   // Show the simulation speed in the toolbar
        void syncCurrentFlow( bool on ) { if( currentFlowAct->isChecked() != on ) currentFlowAct->setChecked( on ); }
        QString curCirc() { return m_curCirc; }   // File of the open circuit (empty: unsaved)
        void syncCapCharge( bool on ) { if( capChargeAct->isChecked() != on ) capChargeAct->setChecked( on ); }
        void powerCircOn();
        void powerCircOff();
        void powerCircDebug();
        void pauseDebug();
        void debugPaused();
        void resumeDebug();

        void simDebugMessage( QString msg ) { m_outPane.appendLine( msg.remove("\"") ); }

        QSplitter* splitter() { return m_mainSplitter; }
        void showDescription();       // Refresh the Description tab
        
    public slots:
        bool newCircuit();
        void openRecentFile();
        void openCirc();
        void loadCirc( QString path );
        void saveCirc( QString file );
        void saveCirc();
        void saveCircAs();
        void powerCirc();
        void pauseCirc();
        void settApp();
        void openInfo();
        void about();

    //signals:
    //    void dataAvailable( int uart, const QByteArray &data );
        
    private:
        void createActions();
        void createToolBars();

 static CircuitWidget* m_pSelf;

        enum { MaxRecentFiles = 20 };
        void updateRecentFiles();
        void updateRecentFileActions();

        bool m_paused;

        QVBoxLayout  m_verticalLayout;
        CircuitView  m_circView;
        OutPanelText m_outPane;
        
        QToolBar m_circToolBar;

        QLabel* m_msgLabel;
        InfoWidget* m_infoWidget;
        DescriptionWidget* m_descWidget;
        QTabWidget* m_bottomTabs;     // Description | Messages

        QSplitter* m_mainSplitter;
        QSplitter* m_panelSplitter;

        QAction* recentFileActs[MaxRecentFiles];
        QAction* newCircAct;
        QAction* openCircAct;
        QAction* saveCircAct;
        QAction* saveCircAsAct;
        QAction* zoomFitAct;
        QAction* zoomSelAct;
        QAction* zoomOneAct;
        QAction* powerCircAct;
        QAction* pauseSimAct;
        QAction* currentFlowAct;
        QAction* capChargeAct;
        QComboBox* m_timebase = nullptr;   // Simulated time per real second
        QAction* settAppAct;
        QAction* infoAct;
        QAction* aboutAct;
        QAction* aboutQtAct;
        
        QMenu m_fileMenu;
        QMenu m_infoMenu;
        
        QString m_curCirc;
        QString m_lastCircDir;

        AppDialog*   m_appPropW;
    public:
        AppDialog* appDialog() { return m_appPropW; }   // Settings dialog, if opened (CLI)
    private:
        AboutDialog* m_about;
};

#endif
