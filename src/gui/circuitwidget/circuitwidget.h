/***************************************************************************
 *   Copyright (C) 2010 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef CIRCUITWIDGET_H
#define CIRCUITWIDGET_H

#include <QMenu>
#include <QSet>
#include <QToolBar>
#include <QComboBox>
#include <QVBoxLayout>
#include <QGridLayout>

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
        void toggleNoise( bool on );   // Toolbar noise button: restarts a running simulation
        void updateNoise();     // Toolbar NOISE label: shown while transient noise is on
        // Speed lock (specs/sim-control.md §3.9): toggle beside the speed box, saved with the circuit
        void setSpeedLock( bool on );
        void updateLock();      // Each frame: the "step ×N" badge and the toggle

        void syncTimebase( uint64_t psPerSec );   // Show the simulation speed in the toolbar
        void syncSkip( uint64_t ps );             // Show the skip interval in the toolbar (0 = 1 step)
        QToolBar* simToolBar() { return &m_simToolBar; }
        void updtBackAvail( bool announce );
        void backOffWarning( QString block );    // ⏮ greyed out where going back is off; announce: warn in Messages
        void skipFollowSpeed();                   // From the set speed (specs/sim-control.md §3.3)
        void skipForSpeed( double psPerSec, bool hysteresis );   // From a speed: set or real
        void setSkipInterval( uint64_t ps );      // Set it (toolbar, keys): saved with the circuit
        void skipForward( int count );            // → / ⏭: pause and skip count × the interval
        void skipBack( int count );               // ← / ⏮: pause and go back count × the interval
        void simKeysBack();                       // Esc on an idle canvas: the simulation keys again
        // Saved key frames (specs/sim-control.md §5.1)
        void saveKeyFrame();                      // ⌘K / toolbar: save, show it in the pane, edit its description
        bool playRange();                         // Play with 2+ key frames selected: true when it played the range
        void togglePlay();                        // Space (also from the Key frames list)
        void setKeyFramesSplit( bool split );     // The pane in the tabs or beside them
        void showKeyFrames();                     // The pane in sight (its tab in front)
        bool keyFramesSplit() { return m_keySplit; }
        // Running slower than the set speed: the speed box shows the real speed in orange (realPsPerSec 0: off)
        void showRealSpeed( double realPsPerSec );
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

        // Messages: also show debug logs ("[tag] ..." qDebug lines); saved in the user settings
        static bool verboseLog() { return m_verboseLog; }
        OutPanelText* outPane() { return &m_outPane; }
        void setVerboseLog( bool v );

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
        double m_skipPreset = 0;                  // The speed the skip interval was set from
        QWidget*   m_msgWidget;
        QCheckBox* m_logPartsBox = nullptr;
        QCheckBox* m_logCapsBox = nullptr;
        QCheckBox* m_verboseBox = nullptr;
 static bool m_verboseLog;
        
        QToolBar m_circToolBar;      // File, edit, zoom
        QToolBar m_simToolBar;       // Stopped: play only, beside the first. On: speed, skip, run buttons, time, NOISE, below it
        QToolBar m_infoBar;          // Info menu: right end of the first row
        QWidget*     m_toolRow  = nullptr;
        QGridLayout* m_toolGrid = nullptr;
        int  m_toolRows = 0;
        void layoutToolBars();       // One row when both fit, else two; two while the simulation is on
        void setSimExpanded( bool on );   // The whole simulation bar (on) or only the play button
        bool m_simExpanded = false;
        QAction* m_simCloseAct = nullptr;  // × after the time, stopped only
        void updtSimClose();
        QList<QAction*> m_simRunItems;    // Hidden while stopped
        bool eventFilter( QObject* obj, QEvent* ev ) override;
        bool keysToSim();                    // Focus outside the canvas, text fields, lists, editable combos
        bool simKey( class QKeyEvent* ev );  // Space, Esc, arrows: true when used
        void logRunInfo( QString what );  // Speed, loads, FPS (the old info pane) in Messages
        void updtKeyOwner();                 // Frame the skip box while the simulation has the keys
        bool m_simHasKeys = false;

        QSet<QString> m_loggedMsgs;  // Status messages (setMsg) already in Messages this run
        QFont m_badgeFont;           // Text of the NOISE badge
        QLabel* m_noiseLabel = nullptr;   // Text of the NOISE badge (state, scale, window)
        QWidget* m_noiseBox = nullptr;    // The badge: white noise icon + m_noiseLabel on orange
        QAction* m_lockAct = nullptr;     // Speed lock toggle
        QLabel*  m_lockLabel = nullptr;   // "step ×8" on orange while the lock coarsens the step
        QAction* m_lockBadgeAct = nullptr;
        QAction* m_noiseAct = nullptr;
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
        QAction* skipFwdAct = nullptr;
        QAction* skipBackAct = nullptr;
        QAction* saveKeyAct = nullptr;
        class KeyFramePane* m_keyPane = nullptr;
        bool m_keySplit = false;
        void updtKeyFrameAct();
        QAction* currentFlowAct;
        QAction* capChargeAct;
        QAction* partStatusAct;
        QAction* instrumentsAct;
        QAction* noiseOnAct = nullptr;
        QAction* exportAct;
        class SpeedBox* m_timebase = nullptr;   // Simulated time per real second
        class SpeedBox* m_skipBox = nullptr;    // Skip interval
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
