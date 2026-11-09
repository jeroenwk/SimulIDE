/***************************************************************************
 *   SimulIDE command line control interface                              *
 *   i2c: one transfer on the bus through an I2C Master part               *
 ***( see copyright.txt file at root folder )*******************************/

// Bug hunt IMP-023: TWI slave parts (DS1621, DS1307, I2C RAM, I2C to Parallel, SSD1306, ...)
// could only be checked by reading their source. Place an "I2CMaster" part on the bus and
// run: i2c M1 0x48 --write 0xAA --read 2

#include <QDebug>

#include "cliapi.h"
#include "clirefs.h"
#include "circuitwidget.h"
#include "component.h"
#include "i2cmaster.h"
#include "simulator.h"

#define I2C_LOG qDebug() << "[cli:i2c]"

namespace
{
int parseByte( QString s, QString what )
{
    bool ok = false;
    s = s.trimmed();
    int v = s.startsWith("0x", Qt::CaseInsensitive) ? s.mid( 2 ).toInt( &ok, 16 )
          : s.startsWith("0b", Qt::CaseInsensitive) ? s.mid( 2 ).toInt( &ok, 2 )
          : s.toInt( &ok, 10 );
    if( !ok || v < 0 || v > 255 ) throw CliError("invalid "+what+": "+s+" (0-255, as 42, 0x2A or 0b101010)");
    return v;
}
QString hex( int v ) { return QString("0x%1").arg( v, 2, 16, QChar('0') ).toUpper().replace("0X","0x"); }
}

void CliApi::registerI2c()
{
    add("i2c", "i2c <master> <addr> [--write b,b,...] [--read N] [--timeout T]",
        "One I2C transfer through an I2C Master part (Converters/I2C Master) wired to the bus: START, address+W, "
        "the --write bytes, repeated START, address+R, --read N bytes (ACK, NACK on the last), STOP. The address is "
        "7-bit (0x48 for a DS1621 with A0-A2 low). Starts or resumes the simulation and leaves it running. "
        "Returns the bytes read, the ACK of every byte and the bus log (S, A0x90+, W0xAA+, Sr, R0x19-, P; "
        "+ = ACK, - = NACK). --timeout is simulated time (default 100ms).",
        []( CliCtx& c ) -> QJsonValue
    {
        Component* comp = CliRefs::comp( c.arg( 0, "I2C Master part") );
        I2CMaster* m = dynamic_cast<I2CMaster*>( comp );
        if( !m ) throw CliError( comp->getUid()+" is not an I2C Master (comp add I2CMaster)");

        I2cTransfer t;
        t.addr = parseByte( c.arg( 1, "address"), "address");
        if( t.addr > 127 ) throw CliError("address "+hex( t.addr )+" is not 7-bit: give it without the R/W bit ("+hex( t.addr >> 1 )+")");
        if( c.has("write") )
            for( QString b : c.opt("write").split(',', Qt::SkipEmptyParts ) ) t.write.append( parseByte( b, "write byte") );
        if( c.has("read") ){
            bool ok = false;
            t.nRead = c.opt("read").toInt( &ok );
            if( !ok || t.nRead < 1 || t.nRead > 4096 ) throw CliError("--read takes a byte count (1-4096)");
        }
        uint64_t timeout = Cli::parseTime( c.opt("timeout", "100ms") );
        if( m->busy() ) throw CliError( comp->getUid()+" is busy with another transfer");

        Simulator* sim = Simulator::self();
        if( !sim->isRunning() )    CircuitWidget::self()->powerCircOn();
        else if( sim->isPaused() ) CircuitWidget::self()->pauseCirc();   // Toggles: resumes
        if( !sim->isRunning() ) throw CliError("simulation could not start");

        m->request( t );
        I2C_LOG << comp->getUid() << "addr" << hex( t.addr ) << "write" << t.write.size() << "read" << t.nRead
                << "timeout" << Cli::timeStr( timeout );

        CliPendingPtr reply = c.defer();
        int generation = Cli::runGeneration();
        Simulator* owner = sim;
        auto startT = std::make_shared<uint64_t>( 0 );
        QString uid = comp->getUid();
        Cli::pollUntil( [reply, m, uid, generation, owner, timeout, startT]() -> bool
        {
            Simulator* sim = Simulator::self();
            if( sim != owner || Cli::runGeneration() != generation ){ reply->fail("i2c cancelled"); return true; }
            if( !sim->isRunning() ){ reply->fail("simulation stopped during the transfer"); return true; }
            if( *startT == 0 ) *startT = sim->circTime() + 1;
            if( m->busy() )
            {
                if( sim->circTime() > *startT + timeout ){
                    I2C_LOG << uid << "timeout; log so far:" << m->result().log.join(" ");
                    reply->fail("i2c timed out after "+Cli::timeStr( timeout )+" of simulated time (bus log: "
                                +m->result().log.join(" ")+"); check the pull-ups and that SDA/SCL are wired");
                    return true;
                }
                return false;
            }
            I2cTransfer r = m->result();
            QJsonArray read, wack;
            for( uint8_t b : r.read ) read.append( (int)b );
            for( bool a : r.writeAck ) wack.append( a );
            QJsonObject o{ {"addr", hex( r.addr )}, {"ok", r.error.isEmpty()}, {"read", read},
                           {"write_ack", wack}, {"log", r.log.join(" ")} };
            if( !r.write.isEmpty() || r.nRead == 0 ) o["addr_ack_w"] = r.addrAckW;
            if( r.nRead > 0 ) o["addr_ack_r"] = r.addrAckR;
            if( !r.error.isEmpty() ) o["error"] = r.error;
            I2C_LOG << uid << "done:" << r.log.join(" ");
            reply->ok( o );
            return true;
        });
        return QJsonValue();
    });
}
