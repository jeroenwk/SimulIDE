/***************************************************************************
 *   Saved key frames: states of a run kept next to the circuit             *
 *   (specs/sim-control.md §5.1)                                           *
 ***( see copyright.txt file at root folder )*******************************/

// A saved key frame is one keyframe of the run (SimHistory) written to a file with its time and a
// description: <circuit>.keyframes/<n>.kf1 beside the .sim1. Opening one puts the running circuit
// in that state (paused), starting a stopped simulation first; playing a range opens the first
// and runs at the set speed to the last. The GUI pane (KeyFramePane) and the CLI (sim keyframe)
// both work through this class.

#ifndef KEYFRAMES_H
#define KEYFRAMES_H

#include <QObject>
#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QTimer>

struct KeyFrame
{
    int       n = 0;           // Number: its file name, kept when others are deleted
    int       version = 0;     // File format (0: new)
    QString   file;
    uint64_t  time = 0;        // Simulated time, ps
    QString   note;            // Description
    QDateTime saved;
    qint64    size = 0;        // State bytes
    QString   layout;          // SimHistory::layoutSignature: must match to load
    QString   elec;            // Electrical hash of the circuit: differs -> a warning
    QJsonObject settings;      // Simulation settings that change results: differ -> a warning
};

class KeyFrames : public QObject
{
    Q_OBJECT
    public:
 static KeyFrames* self();

        QString folder();                   // "" while the circuit has no file
        void reload();                      // Read the folder (a circuit opened)
        void followCircuit() { if( folder() != m_folder ) reload(); }   // Saved under another name
        const QList<KeyFrame>& list() { return m_list; }   // By time
        const KeyFrame* find( int n );

        QString whyNotSave();               // "" when a key frame can be saved now
        int     save( QString note, QString* error );       // Its number, 0 on error
        QString open( int n );              // Jump there: "" or an error
        QString setNote( int n, QString note );
        QString remove( const QList<int>& ns );
        QString playRange( int from, int to );   // Open from, run at the set speed, pause at to
        bool    rangeActive() { return m_rangeEnd != 0; }
        void    stopRange();
        bool    busy() { return m_busy; }   // Opening or starting a range (its own pause/resume)

 static QString electricalHash();       // The circuit file without positions, labels, wire points
 static QJsonObject simSettings();
 static QString timeText( uint64_t ps );

    signals:
        void changed();                     // The list
        void noteChanged( int n, QString note );
        void opened( int n );
        void rangeEnded( bool reached );

    private:
        KeyFrames();
        bool readFile( const QString& path, KeyFrame& kf, QByteArray* bytes, QString* error );
        bool writeFile( const KeyFrame& kf, const QByteArray& bytes, QString* error );
        void pollRange();

        QList<KeyFrame> m_list;
        QString  m_folder;
        uint64_t m_rangeEnd = 0;
        int      m_rangeTo  = 0;
        bool     m_busy = false;
        QTimer   m_poll;
};

#endif
