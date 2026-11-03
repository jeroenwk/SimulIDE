/***************************************************************************
 *   SimulIDE command line control interface                              *
 *   Microcontroller and serial commands                                   *
 ***( see copyright.txt file at root folder )*******************************/

#include <QJsonArray>
#include <QMutex>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFile>
#include <QTextStream>

#include "cliapi.h"
#include "clirefs.h"
#include "circuit.h"
#include "simulator.h"
#include "updatable.h"
#include "e-element.h"
#include "mcu.h"
#include "e_mcu.h"
#include "mcuuart.h"
#include "subcircuit.h"
#include "serialterm.h"
#include "serialport.h"
#include "memdata.h"

namespace
{
//----------------------------------------------------------------------------
// Resolving MCUs and UARTs

Mcu* mcuOf( Component* c )
{
    if( Mcu* mcu = dynamic_cast<Mcu*>( c ) ) return mcu;
    if( SubCircuit* sub = dynamic_cast<SubCircuit*>( c ) )   // Boards: Arduino, etc.
        if( Mcu* mcu = dynamic_cast<Mcu*>( sub->getMainComp() ) ) return mcu;
    return nullptr;
}

bool compAlive( Component* c ) { return Circuit::self()->compList()->contains( c ); }

bool mcuAlive( Mcu* mcu )
{
    for( Component* c : *Circuit::self()->compList() ) if( mcuOf( c ) == mcu ) return true;
    return false;
}

Mcu* findMcu( QString ref, Component** top=nullptr )
{
    Component* c = CliRefs::comp( ref );
    Mcu* mcu = mcuOf( c );
    if( !mcu ) throw CliError( c->getUid()+" is not a microcontroller (see: mcu list)");
    if( top ) *top = c;
    return mcu;
}

struct UartRef
{
    Component*   comp;     // Top level component (keeps track of deletion)
    UsartModule* usart;
    bool terminal;         // Host side device: we transmit into the circuit and read what it receives
    QString name;
};

UartRef findUart( CliCtx& c )
{
    QString ref = c.arg( 0, "component");
    Component* comp = CliRefs::comp( ref );
    int num = (int)Cli::parseNum( c.opt("uart","1"), "uart" );

    if( Mcu* mcu = mcuOf( comp ) )
    {
        std::vector<McuUsart*>* usarts = mcu->getEmcu()->usarts();
        if( usarts->empty() ) throw CliError( mcu->device()+" has no UART");
        if( num < 1 || num > (int)usarts->size() )
            throw CliError( QString("uart must be 1..%1").arg( usarts->size() ) );
        return { comp, usarts->at( num-1 ), false, comp->getUid()+" uart"+QString::number( num ) };
    }
    if( UsartModule* u = dynamic_cast<UsartModule*>( comp ) )
    {
        bool terminal = dynamic_cast<SerialTerm*>( comp ) || dynamic_cast<SerialPort*>( comp );
        return { comp, u, terminal, comp->getUid() };
    }
    throw CliError( comp->getUid()+" has no serial port (use an MCU, board, Serial Terminal or Serial Port)");
}

//----------------------------------------------------------------------------
// Feeds queued bytes to a UART, one frame at a time, from simulation events.

class UartFeeder : public eElement
{
    public:
        UartFeeder( Component* c, UsartModule* u, bool transmit )
          : eElement("cliUartFeeder")
          , m_comp( c )
          , m_usart( u )
          , m_transmit( transmit )
        {}

        virtual void initialize() override        // Simulation (re)start: events were cleared
        {
            QMutexLocker lock( &m_mutex );
            m_scheduled = false;
        }

        virtual void runEvent() override          // Simulation thread
        {
            QMutexLocker lock( &m_mutex );
            m_scheduled = false;
            if( m_queue.isEmpty() ) return;

            uint8_t data = (uint8_t)m_queue.at( 0 );
            bool done = m_transmit ? m_usart->cliTransmit( data ) : m_usart->cliInjectRx( data );
            if( done ) m_queue.remove( 0, 1 );
            if( !m_queue.isEmpty() ) schedule();
        }

        void append( QByteArray data )             // GUI thread
        {
            QMutexLocker lock( &m_mutex );
            m_queue.append( data );
        }
        void kick()                                // GUI thread, simulation thread idle
        {
            QMutexLocker lock( &m_mutex );
            if( !m_scheduled && !m_queue.isEmpty() ) schedule();
        }
        int pending()
        {
            QMutexLocker lock( &m_mutex );
            return m_queue.size();
        }
        bool alive() { return compAlive( m_comp ); }
        UsartModule* usart() { return m_usart; }

    private:
        void schedule()
        {
            uint64_t period = m_usart->framePeriod();
            if( period == 0 ) period = 100*1000*1000;  // Baudrate not set yet: retry in 100 us
            m_scheduled = true;
            Simulator::self()->addEvent( period, this );
        }

        Component* m_comp;
        UsartModule* m_usart;
        bool m_transmit;

        QMutex m_mutex;
        QByteArray m_queue;
        bool m_scheduled = false;
};

// Runs work while the simulation thread is idle
class CliTicker : public Updatable
{
    public:
        virtual void updateStep() override
        {
            QList<std::function<void()>> jobs = m_jobs;
            m_jobs.clear();
            for( auto& job : jobs ) job();

            for( UartFeeder* f : m_feeders.values() )
            {
                if( !f->alive() ) { m_feeders.remove( f->usart() ); delete f; continue; }
                f->kick();
            }
        }

        // Run fn now if the simulation is not running, else at the next frame
        void runIdle( std::function<void()> fn )
        {
            if( !Simulator::self()->isRunning() ) fn();
            else m_jobs.append( fn );
        }

        UartFeeder* feeder( UartRef& u )
        {
            checkSim();
            UartFeeder* f = m_feeders.value( u.usart );
            if( !f ){
                f = new UartFeeder( u.comp, u.usart, u.terminal );
                m_feeders[u.usart] = f;
            }
            return f;
        }

        void checkSim()   // Each circuit has its own simulator: drop feeders of the old one
        {
            if( m_sim == Simulator::self() ) return;
            m_sim = Simulator::self();
            for( UartFeeder* f : m_feeders.values() ) delete f;
            m_feeders.clear();
            m_jobs.clear();
            m_sim->addToUpdateList( this );
        }

    private:
        Simulator* m_sim = nullptr;
        QHash<UsartModule*, UartFeeder*> m_feeders;
        QList<std::function<void()>> m_jobs;
};

CliTicker* ticker()
{
    static CliTicker* t = new CliTicker();
    t->checkSim();
    return t;
}

//----------------------------------------------------------------------------
// Helpers

QJsonObject mcuInfo( Mcu* mcu, Component* top=nullptr )
{
    eMcu* e = mcu->getEmcu();
    if( !top ) top = mcu;
    QJsonObject o{
        {"uid",       top->getUid() },
        {"label",     top->idLabel() },
        {"device",    mcu->device() },
        {"main",      mcu->mainMcu() },
        {"program",   mcu->program() },
        {"freq",      mcu->freq() },
        {"flashSize", (int)e->flashSize() },
        {"ramSize",   (int)e->ramSize() },
        {"eepromSize",(int)e->romSize() },
        {"uarts",     (int)e->usarts()->size() } };
    if( mcu->isScripted() ) o["scripted"] = true;
    if( top != mcu ) o["mcu"] = mcu->getUid();       // MCU inside a board
    return o;
}

// Address: number (dec, 0x hex) or register name
int parseAddr( Mcu* mcu, QString a )
{
    bool ok = false;
    int addr = a.startsWith("0x", Qt::CaseInsensitive) ? a.mid(2).toInt( &ok, 16 ) : a.toInt( &ok, 10 );
    if( ok ) return addr;

    eMcu* e = mcu->getEmcu();
    if( e->regExist( a ) ) return e->getRegAddress( a );
    for( QString r : e->regInfo()->keys() )
        if( r.compare( a, Qt::CaseInsensitive ) == 0 ) return e->getRegAddress( r );
    throw CliError("unknown address or register: "+a );
}

QByteArray parseHexBytes( QString s )
{
    QByteArray out;
    s.replace(",", " ");
    for( QString w : s.split(" ", QString::SkipEmptyParts ) )
    {
        if( w.startsWith("0x", Qt::CaseInsensitive ) ) w = w.mid( 2 );
        if( w.size() % 2 ) w.prepend("0");
        for( int i=0; i<w.size(); i+=2 )
        {
            bool ok;
            int v = w.mid( i, 2 ).toInt( &ok, 16 );
            if( !ok ) throw CliError("invalid hex byte: "+w.mid( i, 2 ) );
            out.append( (char)v );
    }   }
    return out;
}

QString hexStr( QByteArray data )
{
    QStringList parts;
    for( char ch : data ) parts.append( QString("%1").arg( (uint8_t)ch, 2, 16, QChar('0') ) );
    return parts.join(" ");
}

QJsonObject serialResult( QByteArray data, bool hex )
{
    QJsonObject o{
        {"text",  QString::fromLatin1( data ) },
        {"bytes", data.size() } };
    if( hex ) o["hex"] = hexStr( data );
    return o;
}
}

void CliApi::registerMcu()
{
    add("mcu list", "mcu list", "Microcontrollers in the circuit (including those inside boards).",
        []( CliCtx& ) -> QJsonValue
    {
        QJsonArray rows;
        for( Component* c : *Circuit::self()->compList() )
        {
            Mcu* mcu = mcuOf( c );
            if( !mcu ) continue;
            rows.append( mcuInfo( mcu, c ) );
        }
        return rows;
    });

    add("mcu info", "mcu info <comp>", "Device, firmware, clock and memory sizes of an MCU or board.",
        []( CliCtx& c ) -> QJsonValue
    {
        Component* top;
        Mcu* mcu = findMcu( c.arg( 0, "component"), &top );
        return mcuInfo( mcu, top );
    });

    add("mcu load", "mcu load <comp> <firmware>", "Load firmware (.hex, .elf, ...) into an MCU or board. Stops the simulation.",
        []( CliCtx& c ) -> QJsonValue
    {
        Component* top;
        Mcu* mcu = findMcu( c.arg( 0, "component"), &top );
        QString file = CliApi::self()->resolvePath( c.arg( 1, "firmware file") );
        if( !QFileInfo::exists( file ) ) throw CliError("file not found: "+file );
        if( !mcu->load( file ) ) throw CliError("could not load firmware: "+file );
        Circuit::self()->setChanged();
        return mcuInfo( mcu, top );
    });

    add("mcu reload", "mcu reload <comp>", "Reload the current firmware file (after recompiling). Stops the simulation.",
        []( CliCtx& c ) -> QJsonValue
    {
        Component* top;
        Mcu* mcu = findMcu( c.arg( 0, "component"), &top );
        QString file = mcu->program();
        if( file.isEmpty() ) throw CliError("no firmware loaded in "+mcu->getUid() );
        if( !mcu->load( file ) ) throw CliError("could not reload firmware: "+file );
        return mcuInfo( mcu, top );
    });

    add("mcu reset", "mcu reset <comp>", "Reset the MCU (the simulation keeps running).",
        []( CliCtx& c ) -> QJsonValue
    {
        Component* top;
        Mcu* mcu = findMcu( c.arg( 0, "component"), &top );
        if( !Simulator::self()->isRunning() ) throw CliError("simulation is not running");
        CliPendingPtr reply = c.defer();
        ticker()->runIdle( [mcu, top, reply](){
            if( !mcuAlive( mcu ) ) { reply->fail("MCU was removed"); return; }
            mcu->getEmcu()->hardReset( true );
            mcu->getEmcu()->hardReset( false );
            reply->ok( mcuInfo( mcu, top ) );
        });
        return QJsonValue();
    });

    add("mcu main", "mcu main <comp>", "Make this the main MCU (the one the editor and MCU monitor attach to).",
        []( CliCtx& c ) -> QJsonValue
    {
        Component* top;
        Mcu* mcu = findMcu( c.arg( 0, "component"), &top );
        mcu->slotmain();
        return mcuInfo( mcu, top );
    });

    add("mcu regs", "mcu regs <comp> [--filter TEXT]", "Registers with address and current value.",
        []( CliCtx& c ) -> QJsonValue
    {
        Component* top;
        Mcu* mcu = findMcu( c.arg( 0, "component"), &top );
        eMcu* e = mcu->getEmcu();
        QString filter = c.opt("filter");
        QMap<int, QString> byAddr;
        for( QString r : e->regInfo()->keys() )
            if( filter.isEmpty() || r.contains( filter, Qt::CaseInsensitive ) )
                byAddr.insertMulti( e->regInfo()->value( r ).address, r );

        QJsonArray rows;
        for( int addr : byAddr.uniqueKeys() )
            for( QString r : byAddr.values( addr ) )
            {
                int v = ( addr < (int)e->ramSize() ) ? e->getRamValue( addr ) : 0;
                rows.append( QJsonObject{
                    {"name",  r },
                    {"addr",  QString("0x%1").arg( addr, 4, 16, QChar('0') ) },
                    {"value", v },
                    {"hex",   QString("0x%1").arg( v, 2, 16, QChar('0') ) },
                    {"bin",   QString("%1").arg( v, 8, 2, QChar('0') ) } } );
            }
        return rows;
    });

    add("mcu mem", "mcu mem <comp> <addr|reg> [count] [--space ram|flash|eeprom] [--set v,v,...]",
        "Read (or write with --set) MCU memory. Addresses accept decimal, 0x hex or a register name.",
        []( CliCtx& c ) -> QJsonValue
    {
        Component* top;
        Mcu* mcu = findMcu( c.arg( 0, "component"), &top );
        eMcu* e = mcu->getEmcu();
        QString space = c.opt("space","ram").toLower();
        int addr = parseAddr( mcu, c.arg( 1, "address") );

        int size;
        if     ( space == "ram"    ) size = e->ramSize();
        else if( space == "flash"  ) size = e->flashSize();
        else if( space == "eeprom" ) size = e->romSize();
        else throw CliError("unknown memory space: "+space+" (ram, flash, eeprom)");
        if( addr < 0 || addr >= size ) throw CliError( QString("address out of range: %1 (%2 size %3)").arg( addr ).arg( space ).arg( size ) );

        if( c.has("set") )
        {
            QList<int> vals;
            for( QString v : c.opt("set").split(",", QString::SkipEmptyParts ) )
            {
                v = v.trimmed();
                bool ok;
                int n = v.startsWith("0x", Qt::CaseInsensitive) ? v.mid(2).toInt( &ok, 16 ) : v.toInt( &ok, 10 );
                if( !ok ) throw CliError("invalid value: "+v );
                vals.append( n );
            }
            if( addr+vals.size() > size ) throw CliError("write goes past the end of "+space );

            CliPendingPtr reply = c.defer();
            ticker()->runIdle( [=](){
                if( !mcuAlive( mcu ) ) { reply->fail("MCU was removed"); return; }
                for( int i=0; i<vals.size(); ++i )
                {
                    if     ( space == "ram"   ) e->setRamValue( addr+i, vals[i] );
                    else if( space == "flash" ) e->setFlashValue( addr+i, vals[i] );
                    else                        e->setRomValue( addr+i, vals[i] );
                }
                reply->ok( QJsonObject{ {"written", vals.size() } } );
            });
            return QJsonValue();
        }
        int count = (int)Cli::parseNum( c.argOr( 2, "1"), "count");
        if( count < 1 ) throw CliError("count must be >= 1");
        count = qMin( count, size-addr );

        QJsonArray vals;
        QStringList hex;
        for( int i=0; i<count; ++i )
        {
            int v;
            if     ( space == "ram"   ) v = e->getRamValue( addr+i );
            else if( space == "flash" ) v = e->getFlashValue( addr+i );
            else                        v = e->getRomValue( addr+i );
            vals.append( v );
            hex.append( QString::number( v, 16 ).rightJustified( space == "flash" ? 4:2, '0') );
        }
        return QJsonObject{
            {"space",  space },
            {"addr",   addr },
            {"values", vals },
            {"hex",    hex.join(" ") } };
    });

    add("mcu eeprom", "mcu eeprom <comp> [--load FILE | --save FILE | --set v,v,...]",
        "Show, load, save or set the EEPROM contents. Files: .data (comma separated), .hex or .bin.",
        []( CliCtx& c ) -> QJsonValue
    {
        Component* top;
        Mcu* mcu = findMcu( c.arg( 0, "component"), &top );
        eMcu* e = mcu->getEmcu();
        if( e->romSize() == 0 ) throw CliError( mcu->device()+" has no EEPROM");
        QVector<int>* eep = e->eeprom();

        if( c.has("load") )
        {
            QString file = CliApi::self()->resolvePath( c.opt("load") );
            if( !QFileInfo::exists( file ) ) throw CliError("file not found: "+file );
            QVector<int> data( eep->size(), 0xFF );
            if( !MemData::loadFile( &data, file, false, 8 ) )
                throw CliError( QString("could not load %1 (bad format, or larger than the %2 byte EEPROM)").arg( file ).arg( data.size() ) );
            e->setEeprom( &data );
        }
        else if( c.has("set") )
        {
            QVector<int> data = *eep;
            int i = 0;
            for( QString v : c.opt("set").split(",") )
            {
                if( i >= data.size() ) break;
                v = v.trimmed();
                bool ok;
                int n = v.startsWith("0x", Qt::CaseInsensitive) ? v.mid(2).toInt( &ok, 16 ) : v.toInt( &ok, 10 );
                if( !ok ) throw CliError("invalid value: "+v );
                data[i++] = n;
            }
            e->setEeprom( &data );
        }
        else if( c.has("save") )
        {
            QString file = CliApi::self()->resolvePath( c.opt("save") );
            QFile out( file );
            if( !out.open( QIODevice::WriteOnly | QIODevice::Text ) ) throw CliError("cannot write "+file );
            QTextStream ts( &out );
            for( int i=0; i<eep->size(); ++i )
            {
                ts << eep->at( i );
                ts << ( ( i+1 == eep->size() ) ? "\n" : ( (i % 16) == 15 ? ",\n" : "," ) );
            }
            return QJsonObject{ {"saved", file }, {"size", eep->size() } };
        }
        QJsonArray vals;
        for( int v : *eep ) vals.append( v );
        return QJsonObject{ {"size", eep->size() }, {"values", vals } };
    });

    //------------------------------------------------------------------------
    // Serial

    add("serial send", "serial send <comp> [--uart N] [--hex] [--cr|--lf|--crlf] [--] <text...>",
        "Send bytes to a UART: into an MCU/board's RX, or out of a Serial Terminal/Port into the circuit. "
        "Bytes are delivered one frame at a time while the simulation runs.",
        []( CliCtx& c ) -> QJsonValue
    {
        UartRef u = findUart( c );
        QStringList words = c.pos.mid( 1 );
        for( QString k : c.sets.keys() ) words.append( k+"="+c.sets.value( k ).toString() );
        QString text = words.join(" ");

        QByteArray data = c.flag("hex") ? parseHexBytes( text ) : text.toLatin1();
        if     ( c.flag("crlf") ) data.append("\r\n");
        else if( c.flag("cr")   ) data.append('\r');
        else if( c.flag("lf")   ) data.append('\n');
        if( data.isEmpty() ) throw CliError("nothing to send");

        UartFeeder* f = ticker()->feeder( u );
        f->append( data );
        if( !Simulator::self()->isRunning() ) CliApi::self()->addMessage("simulation is not running: bytes are queued until it starts");
        return QJsonObject{ {"uart", u.name }, {"queued", data.size() }, {"pending", f->pending() } };
    });

    add("serial read", "serial read <comp> [--uart N] [--wait T] [--until TEXT] [--timeout T] [--peek] [--hex] [--rx|--tx]",
        "Read bytes captured from a UART since the last read: what an MCU transmitted, or what a Serial Terminal/Port received. "
        "--wait T waits up to T (wall clock) for any data; --until waits for TEXT (default timeout 5s). "
        "Logs are cleared when the simulation starts.",
        []( CliCtx& c ) -> QJsonValue
    {
        UartRef u = findUart( c );
        bool rx = u.terminal;
        if( c.flag("rx") ) rx = true;
        if( c.flag("tx") ) rx = false;
        bool peek = c.flag("peek");
        bool hex  = c.flag("hex");
        QByteArray until = c.opt("until").toLatin1();

        int waitMs = 0;
        if( c.has("wait") )         waitMs = (int)( Cli::parseTime( c.opt("wait") )/1000000000 );
        else if( !until.isEmpty() ) waitMs = 5000;
        if( c.has("timeout") )      waitMs = (int)( Cli::parseTime( c.opt("timeout") )/1000000000 );

        QByteArray data = u.usart->takeLog( rx, peek );
        bool done = until.isEmpty() ? ( !data.isEmpty() || waitMs == 0 ) : data.contains( until );
        if( done || waitMs == 0 ) return serialResult( data, hex );

        if( !Simulator::self()->isRunning() || Simulator::self()->isPaused() )
            CliApi::self()->addMessage("simulation is paused or stopped: waiting for data that may not come");

        CliPendingPtr reply = c.defer();
        auto buf = std::make_shared<QByteArray>( data );
        auto elapsed = std::make_shared<QElapsedTimer>();
        elapsed->start();
        Component* comp = u.comp;
        UsartModule* usart = u.usart;
        Cli::pollUntil( [=]() -> bool
        {
            if( !compAlive( comp ) ) { reply->fail("component was removed"); return true; }
            if( peek ) *buf = usart->takeLog( rx, true );
            else       buf->append( usart->takeLog( rx, false ) );

            bool ready = until.isEmpty() ? !buf->isEmpty() : buf->contains( until );
            if( ready ) { reply->ok( serialResult( *buf, hex ) ); return true; }
            if( elapsed->elapsed() >= waitMs )
            {
                if( until.isEmpty() ) reply->ok( serialResult( *buf, hex ) );
                else reply->fail("timed out waiting for \""+QString::fromLatin1( until )+"\". Received: "
                                 +QString::fromLatin1( *buf ).left( 200 ) );
                return true;
            }
            return false;
        }, 20 );
        return QJsonValue();
    });

    add("serial clear", "serial clear <comp> [--uart N]", "Discard bytes captured so far.",
        []( CliCtx& c ) -> QJsonValue
    {
        UartRef u = findUart( c );
        u.usart->clearLogs();
        return QJsonValue( u.name+": cleared");
    });
}
