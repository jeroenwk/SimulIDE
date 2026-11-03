/***************************************************************************
 *   Scripts pane: pick, describe, run and stop scenario scripts           *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

// Scenario scripts drive the open window through the CLI (Python, usually with
// tools/simctl.py). The pane lists the scripts next to the open circuit (its folder and a
// scripts/ subfolder: .py files that use simctl or carry the line "# simulide-script"),
// shows the script's description (its docstring) and runs it with python3, its output
// live below. Stop ends the script, cancels a pending "sim run --for" and pauses.

#ifndef SCRIPTPANE_H
#define SCRIPTPANE_H

#include <QWidget>
#include <QProcess>
#include <QElapsedTimer>
#include <QJsonObject>

class QComboBox;
class QLineEdit;
class QToolButton;
class QPushButton;
class QLabel;
class QPlainTextEdit;
class OutPanelText;

class ScriptPane : public QWidget
{
    Q_OBJECT
    public:
        ScriptPane( QWidget* parent );
        ~ScriptPane();

        static ScriptPane* self() { return m_pSelf; }

        void refresh();                           // Rescan next to the open circuit (not while running)
        QStringList scripts() const { return m_files; }

        bool run( QString file, QString args, QString &error );
        void stop();
        bool running() const;
        QJsonObject status() const;

        static QString describe( const QString &file );   // Python docstring (or leading # comments)
        static bool isScript( const QString &file );
        static QString python();

    signals:
        void started();
        void finished( int exitCode );

    private:
        void addFile( QString file, bool select );
        void showSelected();
        void browse();
        void readOutput();
        void processFinished( int code, QProcess::ExitStatus st );
        void updateState();

        QStringList m_files;
        QString     m_lastDir;

        QComboBox*      m_list;
        QToolButton*    m_openButton;
        QToolButton*    m_refreshButton;
        QLineEdit*      m_args;
        QPushButton*    m_runButton;
        QPushButton*    m_stopButton;
        QLabel*         m_state;
        QPlainTextEdit* m_desc;
        OutPanelText*   m_out;

        QProcess*     m_proc;
        QString       m_runFile;
        QString       m_output;        // Whole output of the last run (CLI script status)
        QString       m_stateStr;      // idle / running / finished / failed / stopped
        int           m_exitCode;
        bool          m_stopping;
        QElapsedTimer m_clock;
        qint64        m_lastMs;

        static ScriptPane* m_pSelf;
};

#endif
