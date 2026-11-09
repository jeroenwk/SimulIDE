/***************************************************************************
 *   I2C Master: a test bus master driven from the CLI (i2c command)        *
 *   Bug hunt IMP-023: lets the regression tests talk to TWI slave parts.   *
 ***( see copyright.txt file at root folder )*******************************/

// The TWI engine (TwiModule) does the bus timing, as for the MCUs' TWI peripherals; this class
// plays the firmware's part. TwiModule reports each finished step with setTwiState() from inside
// its runEvent() and sets its own state to idle right after, so the next step is started at the
// end of runEvent() (as an MCU would on its next instruction), never from setTwiState() itself.

#include <QDebug>
#include <QMutexLocker>

#include "i2cmaster.h"
#include "simulator.h"
#include "itemlibrary.h"
#include "iopin.h"

#include "doubleprop.h"
#include "boolprop.h"

#define tr(str) simulideTr("I2CMaster",str)
#define I2CM_LOG qDebug() << "[i2c-master]" << getUid()

namespace
{
QString hex( int v ) { return QString("0x%1").arg( v, 2, 16, QChar('0') ).toUpper().replace("0X","0x"); }
}

Component* I2CMaster::construct( QString type, QString id )
{ return new I2CMaster( type, id ); }

LibraryItem* I2CMaster::libraryItem()
{
    return new LibraryItem(
        tr("I2C Master"),
        "Converters",
        "2to3g.png",
        "I2CMaster",
        I2CMaster::construct );
}

I2CMaster::I2CMaster( QString type, QString id )
         : IoComponent( type, id )
         , TwiModule( id )
{
    m_width  = 4;
    m_height = 3;

    init({ "OR01SDA",
           "OR02SCL" });

    m_outPin[0]->setPinMode( openCo );
    TwiModule::setSdaPin( m_outPin[0] );
    m_outPin[1]->setPinMode( openCo );
    TwiModule::setSclPin( m_outPin[1] );

    m_pullups = true;
    m_queued = m_active = m_stateSeen = false;
    m_step = S_IDLE;
    m_idx = 0;

    IoComponent::initState();
    setLabelPos(-16,-36, 0 );

    Simulator::self()->addToUpdateList( this );

    addPropGroup( { tr("Main"), {
        new DoubProp<I2CMaster>("Frequency", tr("I2C Frequency"),"_kHz"
                               , this, &I2CMaster::freqKHz, &I2CMaster::setFreqKHz ),

        new BoolProp<I2CMaster>("Pullups", tr("Internal pull-ups (100 kΩ)"),""
                               , this, &I2CMaster::pullups, &I2CMaster::setPullups ),
    }, groupNoCopy } );
}
I2CMaster::~I2CMaster(){}

void I2CMaster::setPullups( bool p )
{
    m_pullups = p;
    m_outPin[0]->setPullup( p );
    m_outPin[1]->setPullup( p );
}

void I2CMaster::initialize()
{
    TwiModule::initialize();
    QMutexLocker lock( &m_mutex );
    if( m_active ){
        m_xfer.error = "simulation restarted during the transfer";
        m_xfer.done = true;
    }
    m_active = m_stateSeen = false;
    m_step = S_IDLE;
}

void I2CMaster::stamp()
{
    setPullups( m_pullups );
    TwiModule::setMode( TWI_MASTER );
}

void I2CMaster::updateStep()             // GUI thread, simulation worker idle
{
    IoComponent::updateStep();

    QMutexLocker lock( &m_mutex );
    if( !m_queued || !Simulator::self()->isRunning() ) return;
    m_queued = false;
    m_active = true;
    m_stateSeen = false;
    m_step = S_START;
    m_xfer.log.append("S");
    I2CM_LOG << "start: addr" << hex( m_xfer.addr ) << "write" << m_xfer.write.size() << "byte(s), read" << m_xfer.nRead;
    masterStart();
}

void I2CMaster::request( const I2cTransfer& t )
{
    QMutexLocker lock( &m_mutex );
    m_xfer = t;
    m_xfer.done = false;
    m_queued = true;
}

bool I2CMaster::busy()
{
    QMutexLocker lock( &m_mutex );
    return m_queued || m_active;
}

I2cTransfer I2CMaster::result()
{
    QMutexLocker lock( &m_mutex );
    return m_xfer;
}

void I2CMaster::setTwiState( twiState_t state )   // From TwiModule::runEvent()
{
    TwiModule::setTwiState( state );
    m_seenState = state;
    m_stateSeen = true;
}

void I2CMaster::runEvent()
{
    TwiModule::runEvent();
    if( !m_stateSeen ) return;
    m_stateSeen = false;
    QMutexLocker lock( &m_mutex );
    if( m_active ) nextStep();
}

void I2CMaster::readByte()               // A byte was clocked in: the engine sends (N)ACK next
{
    m_xfer.read.append( m_rxReg );
    TwiModule::readByte();
}

void I2CMaster::nextStep()               // Simulation thread, m_mutex held
{
    auto stop = [this](){
        m_step = S_STOP;
        m_xfer.log.append("P");
        masterStop();
    };
    auto fail = [&]( QString err ){
        I2CM_LOG << "error:" << err;
        if( m_xfer.error.isEmpty() ) m_xfer.error = err;
        stop();
    };
    I2CM_LOG << "step" << m_step << "state" << hex( m_seenState );

    switch( m_step )
    {
        case S_START:
            if( !m_xfer.write.isEmpty() || m_xfer.nRead == 0 ){
                m_step = S_ADDR_W;
                masterWrite( m_xfer.addr << 1, true, true );
            }else{
                m_step = S_ADDR_R;
                masterWrite( (m_xfer.addr << 1) | 1, true, false );
            }
            break;

        case S_RESTART:
            m_step = S_ADDR_R;
            masterWrite( (m_xfer.addr << 1) | 1, true, false );
            break;

        case S_ADDR_W:
            m_xfer.addrAckW = ( m_seenState == TWI_MTX_ADR_ACK );
            m_xfer.log.append("A"+hex( m_xfer.addr << 1 )+( m_xfer.addrAckW ? "+" : "-" ) );
            if( !m_xfer.addrAckW ){ fail("no ACK for address "+hex( m_xfer.addr )+" (write)"); break; }
            if( m_xfer.write.isEmpty() ){ stop(); break; }
            m_idx = 0;
            m_step = S_WRITE;
            masterWrite( m_xfer.write[0], false, true );
            break;

        case S_WRITE:
        {
            bool ack = ( m_seenState == TWI_MTX_DATA_ACK );
            m_xfer.writeAck.append( ack );
            m_xfer.log.append("W"+hex( m_xfer.write[m_idx] )+( ack ? "+" : "-" ) );
            if( !ack ){ fail("no ACK for write byte "+QString::number( m_idx )); break; }
            if( ++m_idx < m_xfer.write.size() ){ masterWrite( m_xfer.write[m_idx], false, true ); break; }
            if( m_xfer.nRead > 0 ){
                m_step = S_RESTART;
                m_xfer.log.append("Sr");
                masterStart();
            }
            else stop();
        } break;

        case S_ADDR_R:
            m_xfer.addrAckR = ( m_seenState == TWI_MRX_ADR_ACK );
            m_xfer.log.append("A"+hex( (m_xfer.addr << 1) | 1 )+( m_xfer.addrAckR ? "+" : "-" ) );
            if( !m_xfer.addrAckR ){ fail("no ACK for address "+hex( m_xfer.addr )+" (read)"); break; }
            m_idx = 0;
            m_step = S_READ;
            masterRead( m_xfer.nRead > 1 );
            break;

        case S_READ:
            if( !m_xfer.read.isEmpty() )
                m_xfer.log.append("R"+hex( m_xfer.read.last() )+( m_seenState == TWI_MRX_DATA_ACK ? "+" : "-" ) );
            if( ++m_idx < m_xfer.nRead ) masterRead( m_idx < m_xfer.nRead-1 );   // NACK the last byte
            else stop();
            break;

        case S_STOP:
            finish();
            break;

        default: break;
    }
}

void I2CMaster::finish()
{
    m_active = false;
    m_step = S_DONE;
    m_xfer.done = true;
    I2CM_LOG << "done:" << m_xfer.log.join(" ") << ( m_xfer.error.isEmpty() ? "" : "error: "+m_xfer.error );
}
