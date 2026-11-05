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

class QCheckBox;
class QSplitter;
class QLabel;
class AboutDialog;
class AppDialog;
class InfoWidget;
class DescriptionWidget;
class ScriptPane;
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
        void eventMessage( QString text, int level, QString uid="", int64_t time=-1 );  // time: in time order among event lines  // Event log line in Messages
        void syncEventLog();                                        // Checkboxes from EventLog
        void syncPartStatus( bool on ) { if( partStatusAct->isChecked() != on ) partStatusAct->setChecked( on ); }
        void syncInstruments( bool on ) { if( instrumentsAct->isChecked() != on ) instrumentsAct->setChecked( on ); }
        void powerCircOn();
        void powerCircOff();
        void powerCircDebug();
        void pauseDebug();
        void debugPaused();
        void resumeDebug();

        void simDebugMessage( QString msg ) { m_outPane.appendLine( msg.remove("\"") ); }

        QSplitter* splitter() { return m_mainSplitter; }
        void showDescription();       // Refresh the Description tab
        void printDescription();      // Description in white in Messages (on load, before "Circuit Loaded")
        
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
        QWidget*   m_msgWidget;
        QCheckBox* m_logPartsBox = nullptr;
        QCheckBox* m_logCapsBox = nullptr;
        
        QToolBar m_circToolBar;

        QLabel* m_msgLabel;
        InfoWidget* m_infoWidget;
        DescriptionWidget* m_descWidget;
        ScriptPane* m_scriptPane;
        QTabWidget* m_bottomTabs;     // Description | Messages

        QSplitter* m_mainSplitter;
        QSplitter* m_panelSplitter;

        QAction* recentFileActs[MaxRecentFiles];
        QAction* newCircAct;
        QAction* openCircAct;
        QAction* saveCircAct;
        QAction* saveCircAsAct;
        QAction* undoAct;
        QAction* redoAct;
        QAction* zoomFitAct;
        QAction* zoomSelAct;
        QAction* zoomOneAct;
        QAction* powerCircAct;
        QAction* pauseSimAct;
        QAction* currentFlowAct;
        QAction* capChargeAct;
        QAction* partStatusAct;
        QAction* instrumentsAct;
        QAction* exportAct;
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
