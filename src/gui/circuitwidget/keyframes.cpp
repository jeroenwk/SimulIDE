/***************************************************************************
 *   Saved key frames (specs/sim-control.md §5.1)                          *
 ***( see copyright.txt file at root folder )*******************************/

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QMap>
#include <QDebug>
#include <algorithm>

#include "keyframes.h"
#include "simhistory.h"
#include "simulator.h"
#include "simskip.h"
#include "circuit.h"
#include "circuitwidget.h"
#include "infowidget.h"

static const char kMagic[8] = { 'S','I','M','K','F','1','\n','\0' };   // Then a uint32 header length,
static const int  kVersion  = 2;                                       // the JSON header, the state
                                                                       // 2: part state texts (2026-11-22)

KeyFrames* KeyFrames::self()
{
    static KeyFrames* s = new KeyFrames();
    return s;
}

KeyFrames::KeyFrames()
{
    m_poll.setInterval( 15 );
    connect( &m_poll, &QTimer::timeout, this, [this](){ pollRange(); } );
}

QString KeyFrames::timeText( uint64_t ps )
{
    return Simulator::siStr( ps*1e-12, "s" );
}

QString KeyFrames::folder()
{
    QString circ = CircuitWidget::self() ? CircuitWidget::self()->curCirc() : QString();
    if( circ.isEmpty() ) return "";
    QFileInfo fi( circ );
    return fi.absolutePath()+"/"+fi.completeBaseName()+".keyframes";
}

const KeyFrame* KeyFrames::find( int n )
{
    for( const KeyFrame& k : m_list ) if( k.n == n ) return &k;
    return nullptr;
}

void KeyFrames::reload()
{
    stopRange();
    m_list.clear();
    m_folder = folder();
    if( !m_folder.isEmpty() )
    {
        QDir d( m_folder );
        for( const QString& f : d.entryList( { "*.kf1" }, QDir::Files ) )
        {
            KeyFrame kf;
            QString err;
            if( readFile( d.filePath( f ), kf, nullptr, &err ) ) m_list << kf;
            else qWarning().noquote() << "[keyframes] "+f+": "+err;
        }
        std::sort( m_list.begin(), m_list.end(), []( const KeyFrame& a, const KeyFrame& b ){
            return ( a.time != b.time ) ? a.time < b.time : a.n < b.n; });
    }
    emit changed();
}

// Circuit file ---------------------------------------------------------------

bool KeyFrames::readFile( const QString& path, KeyFrame& kf, QByteArray* bytes, QString* error )
{
    QFile f( path );
    if( !f.open( QIODevice::ReadOnly ) ){ *error = "can't read it"; return false; }
    QByteArray all = f.readAll();
    if( all.size() < 12 || memcmp( all.constData(), kMagic, 8 ) ){ *error = "not a key frame file"; return false; }
    uint32_t hl = 0;
    memcpy( &hl, all.constData()+8, 4 );
    if( 12+(qint64)hl > all.size() ){ *error = "truncated"; return false; }
    QJsonParseError pe;
    QJsonDocument doc = QJsonDocument::fromJson( all.mid( 12, hl ), &pe );
    if( !doc.isObject() ){ *error = "bad header: "+pe.errorString(); return false; }
    QJsonObject h = doc.object();
    if( h.value("version").toInt() > kVersion ){ *error = "made by a newer SimulIDE"; return false; }
    kf.version = h.value("version").toInt();
    kf.file  = path;
    kf.n     = QFileInfo( path ).completeBaseName().toInt();
    kf.time  = h.value("time_ps").toString().toULongLong();
    kf.note  = h.value("note").toString();
    kf.saved = QDateTime::fromString( h.value("saved").toString(), Qt::ISODate );
    kf.size  = all.size()-12-hl;
    kf.layout   = h.value("layout").toString();
    kf.elec     = h.value("electrical").toString();
    kf.settings = h.value("settings").toObject();
    if( (qint64)h.value("size").toDouble() != kf.size ){ *error = "truncated state"; return false; }
    if( bytes ) *bytes = all.mid( 12+hl );
    return true;
}

bool KeyFrames::writeFile( const KeyFrame& kf, const QByteArray& bytes, QString* error )
{
    QJsonObject h{
        {"version",    kf.version ? kf.version : kVersion },
        {"time_ps",    QString::number( kf.time ) },
        {"time",       timeText( kf.time ) },
        {"note",       kf.note },
        {"saved",      kf.saved.toString( Qt::ISODate ) },
        {"size",       (double)bytes.size() },
        {"layout",     kf.layout },
        {"electrical", kf.elec },
        {"settings",   kf.settings } };
    QByteArray head = QJsonDocument( h ).toJson( QJsonDocument::Indented );
    uint32_t hl = head.size();
    QDir().mkpath( QFileInfo( kf.file ).absolutePath() );
    QString tmp = kf.file+".tmp";
    QFile f( tmp );
    if( !f.open( QIODevice::WriteOnly | QIODevice::Truncate ) ){ *error = "can't write "+tmp; return false; }
    f.write( kMagic, 8 );
    f.write( (const char*)&hl, 4 );
    f.write( head );
    f.write( bytes );
    if( !f.flush() ){ *error = "can't write "+tmp; f.close(); QFile::remove( tmp ); return false; }
    f.close();
    QFile::remove( kf.file );
    if( !QFile::rename( tmp, kf.file ) ){ *error = "can't write "+kf.file; return false; }
    return true;
}

// The circuit without what doesn't change the simulation: positions, rotations, labels, wire
// points, the <circuit> line (its settings are compared on their own)
QString KeyFrames::electricalHash()
{
    if( !Circuit::self() ) return "";
    QString xml = Circuit::self()->toXml();
    static const QRegularExpression first("^\\s*<circuit[^>]*>");
    static const QRegularExpression attr("\\s(Pos|rotation|hflip|vflip|idLabPos|labelrot|valLabPos|valLabRot|"
                                         "Show_id|Show_Val|ShowProp|label|pointList|mainComp)=\"[^\"]*\"");
    static const QRegularExpression trig("<triggers>.*</triggers>\\s*", QRegularExpression::DotMatchesEverythingOption );
    xml.remove( first );
    xml.remove( attr );
    xml.remove( trig );     // A trigger edit is not a change of the circuit (specs/triggers.html §10)
    QByteArray b = xml.toUtf8();
    uint64_t h = 1469598103934665603ull;
    for( char c : b ){ h ^= (uint8_t)c; h *= 1099511628211ull; }
    return QString::number( (qulonglong)h, 16 );
}

QJsonObject KeyFrames::simSettings()
{
    Simulator* sim = Simulator::self();
    if( !sim ) return QJsonObject();
    return QJsonObject{
        {"stepSize",   QString::number( sim->stepSize() ) },
        {"reactStep",  QString::number( sim->reactStep() ) },
        {"slopeSteps", sim->slopeSteps() },
        {"maxNlSteps", (double)sim->maxNlSteps() },
        {"noise",      sim->noise().on } };
}

// Save, open, edit ------------------------------------------------------------

QString KeyFrames::whyNotSave( bool trigger )
{
    Simulator* sim = Simulator::self();
    if( !sim || !sim->isRunning() ) return tr("Start the simulation and pause it to save a key frame");
    if( !trigger && !sim->isPaused() ) return tr("Pause the simulation to save a key frame");
    if( !trigger && ( SimSkip::self()->active() || rangeActive() ) ) return tr("Wait until the skip ends");
    if( sim->simState() == SIM_DEBUG || sim->isPauseDebug() ) return tr("The debugger controls the simulation");
    if( folder().isEmpty() ) return tr("Save the circuit first: key frames are kept next to it");
    QString block = SimHistory::blockers( sim->elementList() );
    if( !block.isEmpty() ) return tr("Key frames are off for this circuit: %1").arg( block );
    return "";
}

int KeyFrames::save( QString note, QString* error, bool trigger )
{
    QString why = whyNotSave( trigger );
    if( !why.isEmpty() ){ *error = why; return 0; }
    if( folder() != m_folder ) reload();      // Saved under another name since
    Simulator* sim = Simulator::self();
    sim->holdWorker();
    QString err;
    QByteArray bytes = SimHistory::saveBytes( sim, &err );
    if( bytes.isEmpty() ){ *error = "the state can't be saved now: "+err; return 0; }

    int n = 1;
    QDir d( m_folder );
    for( const QString& f : d.entryList( { "*.kf1" }, QDir::Files ) ) n = std::max( n, QFileInfo( f ).completeBaseName().toInt()+1 );
    for( const KeyFrame& k : m_list ) n = std::max( n, k.n+1 );

    KeyFrame kf;
    kf.n     = n;
    kf.file  = d.filePath( QString("%1.kf1").arg( n, 4, 10, QChar('0') ) );
    kf.time  = sim->circTime();
    kf.note  = note;
    kf.saved = QDateTime::currentDateTime();
    kf.size  = bytes.size();
    kf.layout   = SimHistory::layoutSignature( sim );
    kf.elec     = electricalHash();
    kf.settings = simSettings();
    if( !writeFile( kf, bytes, error ) ) return 0;

    int i = 0;
    while( i < m_list.size() && ( m_list.at( i ).time < kf.time || ( m_list.at( i ).time == kf.time && m_list.at( i ).n < n ) ) ) i++;
    m_list.insert( i, kf );
    qInfo().noquote() << "Key frame "+QString::number( n )+" saved at "+timeText( kf.time );
    emit changed();
    return n;
}

QString KeyFrames::setNote( int n, QString note )
{
    for( KeyFrame& k : m_list )
    {
        if( k.n != n ) continue;
        if( k.note == note ) return "";
        QByteArray bytes;
        KeyFrame disk;
        QString err;
        if( !readFile( k.file, disk, &bytes, &err ) ) return err;
        disk.note = note;
        if( !writeFile( disk, bytes, &err ) ) return err;
        k.note = note;
        emit noteChanged( n, note );
        return "";
    }
    return "no key frame "+QString::number( n );
}

QString KeyFrames::remove( const QList<int>& ns )
{
    QStringList failed;
    for( int n : ns )
    {
        for( int i=0; i<m_list.size(); ++i )
        {
            if( m_list.at( i ).n != n ) continue;
            if( QFile::remove( m_list.at( i ).file ) || !QFile::exists( m_list.at( i ).file ) ) m_list.removeAt( i );
            else failed << QString::number( n );
            break;
        }
    }
    QDir d( m_folder );                       // The folder goes with the last one
    if( !m_folder.isEmpty() && d.exists() && d.entryList( QDir::NoDotAndDotDot | QDir::AllEntries ).isEmpty() )
        QDir().rmdir( m_folder );
    emit changed();
    return failed.isEmpty() ? "" : "can't delete key frame "+failed.join(", ");
}

QString KeyFrames::open( int n )
{
    const KeyFrame* p = find( n );
    if( !p ) return "no key frame "+QString::number( n );
    KeyFrame kf;
    QByteArray bytes;
    QString err;
    if( !readFile( p->file, kf, &bytes, &err ) ) return err;
    if( kf.version < kVersion )
        return "key frame "+QString::number( n )+" was saved by an earlier build (format "+QString::number( kf.version )
              +", now "+QString::number( kVersion )+"): go to that time and save it again";

    Simulator* sim = Simulator::self();
    if( !sim ) return "no circuit";
    if( SimSkip::self()->active() ) SimSkip::self()->cancel();
    if( sim->simState() == SIM_DEBUG || sim->isPauseDebug() ) return "the debugger controls the simulation";
    if( m_rangeEnd && !m_busy ) stopRange();

    m_busy = true;
    if( !sim->isRunning() )                 // Started, paused at once: the circuit's elements exist
    {
        CircuitWidget::self()->powerCircOn();
        if( !sim->isRunning() ){ m_busy = false; return "the simulation could not start"; }
    }
    if( !sim->isPaused() ) CircuitWidget::self()->pauseCirc();
    m_busy = false;
    sim->holdWorker();

    QString block = SimHistory::blockers( sim->elementList() );
    if( !block.isEmpty() ) return "key frames are off for this circuit: "+block;
    if( kf.layout != SimHistory::layoutSignature( sim ) )
        return "key frame "+QString::number( n )+" is of another circuit (its parts or nodes differ): not loaded";

    QStringList changed;
    if( !kf.elec.isEmpty() && kf.elec != electricalHash() ) changed << "parts' properties or wiring";
    QJsonObject now = simSettings();
    static const QMap<QString,QString> names{ {"stepSize", "step size"}, {"reactStep", "reactive step"},
        {"slopeSteps", "slope steps"}, {"maxNlSteps", "non-linear limit"}, {"noise", "noise"} };
    auto shown = []( const QString& key, const QJsonValue& v ) -> QString {
        if( key == "stepSize" || key == "reactStep" ) return timeText( v.toString().toULongLong() );
        if( v.isBool() ) return v.toBool() ? "on" : "off";
        return v.toVariant().toString();
    };
    for( auto it = kf.settings.begin(); it != kf.settings.end(); ++it )
        if( now.value( it.key() ) != it.value() )
            changed << names.value( it.key(), it.key() )+" "+shown( it.key(), it.value() )+" → "+shown( it.key(), now.value( it.key() ) );

    err = SimHistory::self()->loadBytes( sim, bytes, kf.time );
    if( !err.isEmpty() ) return err;
    if( InfoWidget::self() ) InfoWidget::self()->setCircTime( sim->circTime() );
    if( !changed.isEmpty() )
        qWarning().noquote() << "[keyframes] Key frame "+QString::number( n )+" was saved before the circuit changed ("
                                +changed.join("; ")+"): the state is the old one, the circuit goes on with the new values";
    emit opened( n );
    return "";
}

// Range play ----------------------------------------------------------------------

QString KeyFrames::playRange( int from, int to )
{
    const KeyFrame* a = find( from );
    const KeyFrame* b = find( to );
    if( !a || !b ) return "no key frame "+QString::number( a ? to : from );
    if( a->time > b->time ) std::swap( a, b );
    uint64_t end = b->time;
    int toN = b->n;
    if( end <= a->time ) return "the range is empty (both at "+timeText( end )+")";

    QString err = open( a->n );
    if( !err.isEmpty() ) return err;
    Simulator* sim = Simulator::self();
    m_rangeEnd = end;
    m_rangeTo  = toN;
    sim->setStopAt( end );                    // Runs at the set speed, holds at the end
    m_busy = true;
    CircuitWidget::self()->pauseCirc();       // Resumes
    m_busy = false;
    m_poll.start();
    qDebug() << "[keyframes] range" << a->time << "→" << end << "ps";
    return "";
}

void KeyFrames::stopRange()
{
    if( !m_rangeEnd ) return;
    m_poll.stop();
    m_rangeEnd = 0;
    Simulator* sim = Simulator::self();
    if( sim ) sim->setStopAt( 0 );
    emit rangeEnded( false );
}

void KeyFrames::pollRange()
{
    Simulator* sim = Simulator::self();
    if( !sim || !sim->isRunning() ){ stopRange(); return; }
    if( sim->circTime() < m_rangeEnd )
    {
        if( sim->isPaused() ) stopRange();    // Paused by the user: the range ends there
        return;
    }
    m_poll.stop();
    m_rangeEnd = 0;
    if( !sim->isPaused() ) CircuitWidget::self()->pauseCirc();
    sim->setStopAt( 0 );
    emit rangeEnded( true );
}
